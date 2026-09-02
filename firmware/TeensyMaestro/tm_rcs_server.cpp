/*
  tm_rcs_server.cpp

  TeensyMaestro — Community Edition (CE)
  SPDX-License-Identifier: CC-BY-NC-SA-3.0
  SPDX-FileCopyrightText: 2025 TNX QSO

  A community-maintained edition with open-source utilities
  for ham radio enthusiasts, focusing on FlexRadio® and Wavelog integrations.

  Based on the original TeensyMaestro by Len Koppl (KD0RC),
  which integrates the FlexRadio 6000 library by IW7DMH.
  Portions of this work remain © Len Koppl and © IW7DMH as noted.

  See LICENSE for full license text and NOTICE for attributions.
  Creative Commons BY-NC-SA 3.0: https://creativecommons.org/licenses/by-nc-sa/3.0/
  
*/

#include "tm_rcs_server.h"
#include <NativeEthernet.h>
#include <stdarg.h>

// Config globals are defined in TeensyMaestro.ino
extern bool     CFG_RCS_Enable;
extern uint16_t CFG_RCS_TCP_Port;

// Backend actions (implemented in Process_Buttons.ino)
bool TM_RCS_RequestPTT(bool on);
bool TM_RCS_RequestTune(bool on);

// Switch SmartSDR TX audio source between MIC and DAX.
// Implemented in Process_Buttons.ino.
bool TM_RCS_RequestDax(bool on);

// Global profile and frequency control for the QSY verb.
// Implemented in Process_Buttons.ino.
bool TM_RCS_RequestProfileLoad(const char* name);
bool TM_RCS_ProfileApplied();
bool TM_RCS_RequestFreq(long freqHz);
int  TM_RCS_ProfileForBandMode(int meters, const char* mode, String& out);

// ===== Debug switches for Remote Command Server =====
#ifndef RCS_DEBUG_ENABLE
#define RCS_DEBUG_ENABLE 1
#endif

#if RCS_DEBUG_ENABLE
inline void RCS_DEBUG(const __FlashStringHelper* s) { Serial.print(s); }
inline void RCS_DEBUG(const char* s)                { Serial.print(s); }
inline void RCS_DEBUGLN(const __FlashStringHelper* s){ Serial.println(s); }
inline void RCS_DEBUGLN(const char* s)              { Serial.println(s); }
inline void RCS_DEBUGF(const char* fmt, ...) {
  char buf[96];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  Serial.print(buf);
}
#else
inline void RCS_DEBUG(const __FlashStringHelper*) {}
inline void RCS_DEBUG(const char*) {}
inline void RCS_DEBUGLN(const __FlashStringHelper*) {}
inline void RCS_DEBUGLN(const char*) {}
inline void RCS_DEBUGF(const char*, ...) {}
#endif

namespace {

  // Server + single active client
  EthernetServer* g_rcsServer = nullptr;
  EthernetClient  g_rcsClient;

  // Small line buffer in RAM1
  static constexpr size_t RCS_LINE_BUF_SIZE = 96;
  char    g_lineBuf[RCS_LINE_BUF_SIZE];
  size_t  g_lineLen = 0;

  bool    g_warnedNoServer = false;

  // Trim in-place helper (left/right whitespace)
  void trimInPlace(char* buf) {
    if (!buf) return;

    size_t len = strlen(buf);
    if (len == 0) return;

    // Trim right
    size_t end = len;
    while (end > 0) {
      char c = buf[end - 1];
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
        --end;
      } else {
        break;
      }
    }
    buf[end] = '\0';
    len = end;

    // Trim left
    size_t start = 0;
    while (start < len) {
      char c = buf[start];
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
        ++start;
      } else {
        break;
      }
    }
    if (start > 0) {
      // Shift left
      size_t i = 0;
      while (start + i <= len) {
        buf[i] = buf[start + i];
        ++i;
      }
    }
  }

  // Convert ASCII string to uppercase in-place
  void toUpperInPlace(char* buf) {
    if (!buf) return;
    for (size_t i = 0; buf[i] != '\0'; ++i) {
      char c = buf[i];
      if (c >= 'a' && c <= 'z') {
        buf[i] = char(c - 'a' + 'A');
      }
    }
  }

// ===== Deferred QSY sequence =====
  //
  // A QSY cannot complete synchronously: loading a global profile takes time
  // on the radio side and the main loop must not block, since CW keying is
  // driven from the same cooperative loop. The verb handler validates, arms
  // this state machine and replies OK immediately. The reply therefore
  // confirms acceptance, not completion.

  enum QsyState {
    QSY_IDLE = 0,
    QSY_WAIT_PROFILE,   // waiting for the radio to report the profile as current
    QSY_SETTLE          // profile applied, letting the status burst subside
  };

  // Maximum time to wait for the radio to confirm the profile load.
  // On timeout the frequency is set anyway rather than dropping the QSY.
  static constexpr uint32_t QSY_PROFILE_TIMEOUT_MS = 4000;

  // Quiet period between profile confirmation and setting the frequency.
  // A profile load rewrites slice state, so the frequency must be applied
  // after the resulting status updates have arrived.
  static constexpr uint32_t QSY_SETTLE_MS = 750;

  QsyState  g_qsyState  = QSY_IDLE;
  uint32_t  g_qsyTimer  = 0;   // rollover safe deadline, compare with (int32_t)
  long      g_qsyFreqHz = 0;

  // Band edges used only to select a profile, not to police transmit
  // privileges. CheckInBand() handles legality separately.
  struct BandRange {
    long lowHz;
    long highHz;
    int  meters;
  };

  static const BandRange QSY_BANDS[] = {
    {  1800000L,  2000000L, 160 },
    {  3500000L,  3800000L,  80 },
    {  5351500L,  5366500L,  60 },
    {  7000000L,  7200000L,  40 },
    { 10100000L, 10150000L,  30 },
    { 14000000L, 14350000L,  20 },
    { 18068000L, 18168000L,  17 },
    { 21000000L, 21450000L,  15 },
    { 24890000L, 24990000L,  12 },
    { 28000000L, 29700000L,  10 },
    { 50000000L, 54000000L,   6 }
  };

  // Returns the band in meters, or -1 when the frequency is outside every
  // band that has profile mappings.
  int bandForFreq(long hz) {
    const size_t n = sizeof(QSY_BANDS) / sizeof(QSY_BANDS[0]);
    for (size_t i = 0; i < n; ++i) {
      if (hz >= QSY_BANDS[i].lowHz && hz <= QSY_BANDS[i].highHz) {
        return QSY_BANDS[i].meters;
      }
    }
    return -1;
  }

  // Translate a spot or radio mode string onto one of the four profile
  // buckets used by the touchscreen selector. The input is already
  // uppercased by handleCommandLine(). Returns nullptr for unknown modes,
  // so the caller can report an error instead of silently defaulting.
  const char* modeBucket(const char* mode) {
    if (strcmp(mode, "CW")   == 0) return "CW";
    if (strcmp(mode, "CWU")  == 0) return "CW";
    if (strcmp(mode, "CWL")  == 0) return "CW";

    if (strcmp(mode, "SSB")  == 0) return "SSB";
    if (strcmp(mode, "USB")  == 0) return "SSB";
    if (strcmp(mode, "LSB")  == 0) return "SSB";

    if (strcmp(mode, "FM")   == 0) return "FM";
    if (strcmp(mode, "NFM")  == 0) return "FM";

    if (strcmp(mode, "DIGU") == 0) return "DIGU";
    if (strcmp(mode, "DIGL") == 0) return "DIGU";
    if (strcmp(mode, "DATA") == 0) return "DIGU";
    if (strcmp(mode, "FT8")  == 0) return "DIGU";
    if (strcmp(mode, "FT4")  == 0) return "DIGU";
    if (strcmp(mode, "RTTY") == 0) return "DIGU";
    if (strcmp(mode, "PSK")  == 0) return "DIGU";

    return nullptr;
  }

  // Handle the argument part of: QSY <freq_hz> <mode>
  // arg is already trimmed and uppercased.
  //
  // Any error reply means nothing was sent to the radio, so the caller is
  // free to fall back to a plain frequency and mode change over CAT.
  String handleQsy(const char* arg) {
    char* endp = nullptr;
    long freqHz = strtol(arg, &endp, 10);
    if (endp == arg) {
      return String("ERR invalid_freq");
    }

    const char* p = endp;
    while (*p == ' ' || *p == '\t') {
      ++p;
    }
    if (*p == '\0') {
      return String("ERR missing_mode");
    }

    const int meters = bandForFreq(freqHz);
    if (meters < 0) {
      return String("ERR unknown_band");
    }

    const char* bucket = modeBucket(p);
    if (bucket == nullptr) {
      return String("ERR unknown_mode");
    }

    String profName;
    const int rc = TM_RCS_ProfileForBandMode(meters, bucket, profName);
    if (rc == -1) {
      return String("ERR no_mapping");
    }
    if (rc != 0) {
      return String("ERR no_profile");
    }

    // A QSY arriving while another is pending supersedes it, since the
    // newer request reflects the current operator intent.
    if (!TM_RCS_RequestProfileLoad(profName.c_str())) {
      return String("ERR rig_not_controllable");
    }

    g_qsyFreqHz = freqHz;
    g_qsyTimer  = millis() + QSY_PROFILE_TIMEOUT_MS;
    g_qsyState  = QSY_WAIT_PROFILE;

    RCS_DEBUGF("RCS: QSY armed freq=%ld band=%dm profile='%s'\n",
               freqHz, meters, profName.c_str());

    return String("OK");
  }

  // Advance the deferred QSY sequence. Called once per main loop iteration.
  // Must never block.
  void tickQsy() {
    if (g_qsyState == QSY_IDLE) {
      return;
    }

    const uint32_t now = millis();

    switch (g_qsyState) {
      case QSY_WAIT_PROFILE:
        if (TM_RCS_ProfileApplied()) {
          g_qsyTimer = now + QSY_SETTLE_MS;
          g_qsyState = QSY_SETTLE;
          RCS_DEBUGLN(F("RCS: QSY profile applied, settling"));
        } else if ((int32_t)(now - g_qsyTimer) > 0) {
          // No confirmation from the radio. Set the frequency anyway
          // rather than silently dropping the QSY.
          RCS_DEBUGLN(F("RCS: QSY profile timeout, setting freq anyway"));
          g_qsyState = QSY_IDLE;
          TM_RCS_RequestFreq(g_qsyFreqHz);
        }
        break;

      case QSY_SETTLE:
        if ((int32_t)(now - g_qsyTimer) > 0) {
          g_qsyState = QSY_IDLE;
          if (!TM_RCS_RequestFreq(g_qsyFreqHz)) {
            RCS_DEBUGLN(F("RCS: QSY freq failed, no usable TX slice"));
          } else {
            RCS_DEBUGF("RCS: QSY freq set to %ld\n", g_qsyFreqHz);
          }
        }
        break;

      default:
        g_qsyState = QSY_IDLE;
        break;
    }
  }

  // Execute a single command line and build a response.
  // line: null-terminated ASCII, already trimmed.
  // Returns a small String with either "OK" or "ERR ...".
  String handleCommandLine(const char* line) {
    if (!line) return String("ERR internal");

    char buf[RCS_LINE_BUF_SIZE];
    strncpy(buf, line, sizeof(buf));
    buf[sizeof(buf) - 1] = '\0';

    trimInPlace(buf);
    toUpperInPlace(buf);

    if (buf[0] == '\0') {
      return String("ERR empty");
    }

    // Split on first space: VERB [ARG]
    char* verb = buf;
    char* arg  = nullptr;

    char* p = buf;
    while (*p != '\0') {
      if (*p == ' ' || *p == '\t') {
        *p = '\0';
        arg = p + 1;
        break;
      }
      ++p;
    }
    if (arg) {
      trimInPlace(arg);
      toUpperInPlace(arg);
    }

    if (arg) {
      RCS_DEBUGF("RCS: CMD verb='%s' arg='%s'\n", verb, arg);
    } else {
      RCS_DEBUGF("RCS: CMD verb='%s'\n", verb);
    }

    // PTT commands
    if (strcmp(verb, "PTT") == 0) {
      if (!arg || arg[0] == '\0') {
        return String("ERR missing_arg");
      }

      bool ok = false;

      if (strcmp(arg, "ON") == 0) {
        ok = TM_RCS_RequestPTT(true);
      } else if (strcmp(arg, "OFF") == 0) {
        ok = TM_RCS_RequestPTT(false);
      } else {
        return String("ERR invalid_arg");
      }

      if (!ok) {
        // Rig not controllable in current mode
        return String("ERR rig_not_controllable");
      }
      return String("OK");
    }

    // TUNE commands
    if (strcmp(verb, "TUNE") == 0) {
      if (!arg || arg[0] == '\0') {
        return String("ERR missing_arg");
      }

      bool ok = false;

      if (strcmp(arg, "ON") == 0) {
        ok = TM_RCS_RequestTune(true);
      } else if (strcmp(arg, "OFF") == 0) {
        ok = TM_RCS_RequestTune(false);
      } else {
        return String("ERR invalid_arg");
      }

      if (!ok) {
        return String("ERR rig_not_controllable");
      }
      return String("OK");
    }

    // DAX commands
    // Usage: "DAX ON" or "DAX OFF"
    if (strcmp(verb, "DAX") == 0) {
      if (!arg || arg[0] == '\0') {
        return String("ERR missing_arg");
      }

      bool ok = false;

      if (strcmp(arg, "ON") == 0) {
        ok = TM_RCS_RequestDax(true);
      } else if (strcmp(arg, "OFF") == 0) {
        ok = TM_RCS_RequestDax(false);
      } else {
        return String("ERR invalid_arg");
      }

      if (!ok) {
        // Rig not controllable in current mode
        return String("ERR rig_not_controllable");
      }
      return String("OK");
    }

    // QSY commands
    // Usage: "QSY <freq_hz> <mode>"
    if (strcmp(verb, "QSY") == 0) {
      if (!arg || arg[0] == '\0') {
        return String("ERR missing_arg");
      }
      return handleQsy(arg);
    }

    // Simple health check
    if (strcmp(verb, "PING") == 0) {
      return String("OK PING");
    }

    return String("ERR unknown_command");
  }

  // Process bytes from current client and close after one full command.
  void pollClient() {
    if (!g_rcsServer) return;

    // Accept new client if none or disconnected
    if (!g_rcsClient || !g_rcsClient.connected()) {
      if (g_rcsClient) {
        g_rcsClient.stop();
      }
      EthernetClient c = g_rcsServer->available();
      if (c) {
        g_rcsClient = c;
        g_lineLen   = 0;
        RCS_DEBUGLN(F("RCS: client connected"));
      }
      return;
    }

    // At this point we have a connected client
    while (g_rcsClient.connected() && g_rcsClient.available() > 0) {
      int bi = g_rcsClient.read();
      if (bi < 0) break;
      char ch = static_cast<char>(bi);

      // Treat CR or LF as end-of-line
      if (ch == '\r' || ch == '\n') {
        if (g_lineLen == 0) {
          // Ignore empty lines
          continue;
        }

        // Null-terminate current line
        if (g_lineLen >= RCS_LINE_BUF_SIZE) {
          g_lineLen = RCS_LINE_BUF_SIZE - 1;
        }
        g_lineBuf[g_lineLen] = '\0';

        RCS_DEBUGF("RCS: line='%s'\n", g_lineBuf);

        // Handle the command
        String resp = handleCommandLine(g_lineBuf);

        // Send response + CRLF
        resp += "\r\n";
        g_rcsClient.write(resp.c_str(), resp.length());

        RCS_DEBUGF("RCS: reply='%s'\n", resp.c_str());

        // Reset buffer and close connection (one command per connection)
        g_lineLen = 0;
        g_rcsClient.stop();
        RCS_DEBUGLN(F("RCS: client disconnected"));
        break;  // exit loop; client is closed
      }

      // Append character to line buffer if space remains
      if (g_lineLen < (RCS_LINE_BUF_SIZE - 1)) {
        g_lineBuf[g_lineLen++] = ch;
      } else {
        // Line too long, discard and respond with error
        const char* err = "ERR line_too_long\r\n";
        g_rcsClient.write(err, strlen(err));
        g_lineLen = 0;
        g_rcsClient.stop();
        RCS_DEBUGLN(F("RCS: line too long, client disconnected"));
        break;
      }
    }
  }

} // namespace

// -------- Public API --------

void TM_RCS::begin() {
  RCS_DEBUGLN(F("RCS: begin()"));
  RCS_DEBUGF("RCS: CFG_RCS_Enable=%u, CFG_RCS_TCP_Port=%u\n",
             (unsigned)CFG_RCS_Enable,
             (unsigned)CFG_RCS_TCP_Port);

  if (!CFG_RCS_Enable) {
    RCS_DEBUGLN(F("RCS: disabled by config, not starting server"));
    return;
  }

  if (!g_rcsServer) {
    static EthernetServer server(CFG_RCS_TCP_Port);
    g_rcsServer = &server;
    g_rcsServer->begin();
    RCS_DEBUGF("RCS: server started on port %u\n", (unsigned)CFG_RCS_TCP_Port);
  }
}

void TM_RCS::poll() {
  if (!g_rcsServer) {
    if (!g_warnedNoServer) {
      RCS_DEBUGLN(F("RCS: poll() called but server not initialized"));
      g_warnedNoServer = true;
    }
    return;
  }
  tickQsy();
  pollClient();
}

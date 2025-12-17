/*
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

#include "tm_time.h"
#include "tm_netutil.h"
#include <NativeEthernetUdp.h>
#include <AceTime.h>
#include <zonedbx/zone_registry.h>
using ace_time::TimeZone;
using ace_time::ZonedDateTime;
using ace_time::TimeOffset;
using ace_time::acetime_t;
using ace_time::OffsetDateTime;

extern ace_time::TimeZone utcTz;
extern ace_time::TimeZone localTz;
static const ace_time::TimeZone* s_utcTzPtr   = &utcTz;
static const ace_time::TimeZone* s_localTzPtr = &localTz;

void TMTime_attachZones(const ace_time::TimeZone* utc, const ace_time::TimeZone* local) {
  s_utcTzPtr   = utc  ? utc  : &utcTz;
  s_localTzPtr = local? local: &localTz;
}

static long DetectUnixToAceDelta();

// ----------------- NTP specifics -----------------
static EthernetUDP s_udp;
static const int NTP_PACKET_SIZE = 48;
static uint8_t s_packetBuffer[NTP_PACKET_SIZE];

static bool     s_enabled = false;
static bool     s_hasTime = false;
static time_t   s_epochUTC = 0;
static uint32_t s_lastSyncMs = 0;
static uint32_t s_lastTickMs = 0;
static uint32_t s_resyncIntervalMs = 3600UL * 1000UL;
static uint8_t  s_maxTries = 3;
static String   s_ntpServer = "pool.ntp.org";
static uint16_t s_localPort = 2390;

// Single owned definition of the clock format (default 24h with seconds)
TMTimeClockFmt CFG_ClockFmt = TM_FMT_24_HHMMSS;

// Single owned definitions of helpers
bool TMTime_is12h()       { return (CFG_ClockFmt == TM_FMT_12_HHMM_AP); }
bool TMTime_showSeconds() { return (CFG_ClockFmt == TM_FMT_24_HHMMSS); }

// ----------------- Small date/time helpers -----------------

// Millisecond-based epoch tick (avoids drift between syncs)
static void tick_epoch()
{
  const uint32_t now = millis();
  const uint32_t delta = now - s_lastTickMs;
  if (delta >= 1000) {
    if (s_hasTime) {
      s_epochUTC += (delta / 1000);
    }
    s_lastTickMs += (delta / 1000) * 1000;
  }
}

static void prepareNtpPacket(uint8_t* buf)
{
  memset(buf, 0, NTP_PACKET_SIZE);
  buf[0] = 0b11100011;  // LI=3 (unsync tolerated), Version=4, Mode=3 (client)
  buf[1] = 0;           // Stratum
  buf[2] = 6;           // Poll
  buf[3] = 0xEC;        // Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  buf[12] = 49;
  buf[13] = 0x4E;
  buf[14] = 49;
  buf[15] = 52;
}

static bool sendNtpRequest()
{
  IPAddress ntpIP;
  if (!TM_ResolveHost(s_ntpServer.c_str(), ntpIP)) {
    debugf("NTP: resolve '%s' FAILED\n", s_ntpServer.c_str());
    return false;
  }
  debugf("NTP: resolve '%s' -> %u.%u.%u.%u\n",
         s_ntpServer.c_str(), ntpIP[0], ntpIP[1], ntpIP[2], ntpIP[3]);

  prepareNtpPacket(s_packetBuffer);

  int bp = s_udp.beginPacket(ntpIP, 123);
  if (bp == 0) {
    debugf("NTP: beginPacket(%u.%u.%u.%u,123) FAILED\n",
           ntpIP[0], ntpIP[1], ntpIP[2], ntpIP[3]);
    return false;
  }
  int wr = 0; // always defined
#if DEBUG
    wr = s_udp.write(s_packetBuffer, NTP_PACKET_SIZE);
#else
    s_udp.write(s_packetBuffer, NTP_PACKET_SIZE);
    (void)wr; // silence unused when debug macros compile away
#endif
  int ep = s_udp.endPacket();
  debugf("NTP: write=%d, endPacket=%d\n", wr, ep);
  return (ep > 0);
}

static bool receiveNtp(time_t& out_epochUTC)
{
  int size = s_udp.parsePacket();
  if (size < NTP_PACKET_SIZE) {
    if (size > 0) debugf("NTP: short packet = %d bytes\n", size);
    return false;
  }
  s_udp.read(s_packetBuffer, NTP_PACKET_SIZE);

  // NTP seconds since 1900-01-01, Unix epoch since 1970-01-01
  const unsigned long seventyYears = 2208988800UL;
  unsigned long highWord = word(s_packetBuffer[40], s_packetBuffer[41]);
  unsigned long lowWord  = word(s_packetBuffer[42], s_packetBuffer[43]);
  unsigned long secsSince1900 = (highWord << 16) | lowWord;
  if (secsSince1900 < seventyYears) {
    debugln("NTP: bogus epoch (< 1970)");
    return false;
  }

  out_epochUTC = (time_t)(secsSince1900 - seventyYears);
  return true;
}

void TMTime_begin(const char* ntp_server,
                  uint16_t local_udp_port,
                  uint32_t resync_interval_ms,
                  uint8_t max_tries)
{
  s_ntpServer = ntp_server ? String(ntp_server) : String("pool.ntp.org");
  s_localPort = local_udp_port;
  s_resyncIntervalMs = resync_interval_ms ? resync_interval_ms : (3600UL * 1000UL);
  s_maxTries = max_tries ? max_tries : 3;

  s_udp.stop(); // ensure socket is closed before (re)bind
  bool begun = s_udp.begin(s_localPort);
  debugf("NTP: UDP.begin(%u) -> %s\n", s_localPort, begun ? "OK" : "FAIL");
  if (!begun) {
    // Try a neighboring port in case the pool/socket is stuck
    s_localPort++;
    begun = s_udp.begin(s_localPort);
    debugf("NTP: UDP.begin(%u) retry -> %s\n", s_localPort, begun ? "OK" : "FAIL");
  }

  s_enabled   = begun;
  s_hasTime   = false;
  s_epochUTC  = 0;
  s_lastSyncMs= 0;
  s_lastTickMs= millis();
}

void TMTime_loop()
{
  tick_epoch();
  if (!s_enabled) return;

  static bool printed = false;
  if (!printed && TMTime_hasTime()) {
    TMTime_debugPrint();
    printed = true;
  }

  const uint32_t now = millis();

  // First time or periodic resync
  const bool needSync = !s_hasTime || (now - s_lastSyncMs >= s_resyncIntervalMs);
  if (!needSync) return;

  // Non-blocking state across loops
  static uint8_t  tries    = 0;
  static uint32_t lastKick = 0;

  // Kick a new request if idle for a short while
  if (tries == 0 && (now - lastKick) >= 50) {
    if (!sendNtpRequest()) {
      // If beginPacket failed repeatedly, consider rebind
      s_udp.stop();
      if (s_udp.begin(s_localPort) == 0) {
        s_localPort++;
        s_udp.begin(s_localPort);
      }
    }
    lastKick = now;
    tries = 1;
    return;
  }

  // Check for reply
  time_t epoch;
  if (receiveNtp(epoch)) {
    s_epochUTC   = epoch;
    s_hasTime    = true;
    s_lastSyncMs = now;
    s_lastTickMs = now;
    debugf("NTP: sync OK, epoch=%lu\n", (unsigned long)s_epochUTC);
    tries = 0;
    return;
  }

  // No reply yet: resend with spacing
  if ((now - lastKick) >= 400) {
    if (tries < s_maxTries) {
      sendNtpRequest();
      tries++;
      lastKick = now;
    } else {
      // Give up for this cycle; schedule next attempt at the next interval
      debugln("NTP: giving up this cycle");
      tries = 0;
      s_lastSyncMs = now; // back off
    }
  }
}

bool TMTime_hasTime() {
  return (s_hasTime && (s_epochUTC > 0));
}

time_t TMTime_nowUTC() { return s_epochUTC; }

time_t TMTime_toLocalEpoch(time_t utc_epoch) {
  if (!s_localTzPtr || s_localTzPtr->isError()) return utc_epoch;

  // Convert Unix->AceTime seconds using runtime delta
  const long delta = DetectUnixToAceDelta();
  const ace_time::acetime_t aceEpoch =
      (ace_time::acetime_t)((long)utc_epoch - delta);

  const ace_time::OffsetDateTime odt = s_localTzPtr->getOffsetDateTime(aceEpoch);
  return (time_t)odt.toEpochSeconds();
}

int32_t TMTime_localOffsetSeconds(time_t utc_epoch) {
  if (!s_localTzPtr || s_localTzPtr->isError()) return 0;

  const long delta = DetectUnixToAceDelta();
  const ace_time::acetime_t aceEpoch =
      (ace_time::acetime_t)((long)utc_epoch - delta);

  const ace_time::OffsetDateTime odtLoc = s_localTzPtr->getOffsetDateTime(aceEpoch);
  return (int32_t)odtLoc.timeOffset().toSeconds();
}

// Runtime detection of AceTime epoch year to derive Unix<->AceTime delta.
// We compute the calendar year at AceTime epoch second 0.
// If it's 1970 → delta=0; 2000 → 946684800; 2050 → 2524608000.
// For unexpected years, we compute the seconds from 1970->year using leap-year rules.
static long DetectUnixToAceDelta() {
  static bool   inited = false;
  static long   delta  = 0;
  if (inited) return delta;

  // Build an OffsetDateTime at AceTime second 0 with zero offset
  const ace_time::OffsetDateTime odt0 =
      ace_time::OffsetDateTime::forEpochSeconds(
    (ace_time::acetime_t)0, ace_time::TimeOffset::forSeconds(0));
  const int y = odt0.year();

  if (y == 1970) {
    delta = 0L;
  } else if (y == 2000) {
    delta = 946684800L;    // 1970 -> 2000
  } else if (y == 2050) {
    delta = 2524608000L;   // 1970 -> 2050
  } else {
    // Generic: seconds from 1970-01-01 to y-01-01
    long sec = 0L;
    for (int yr = 1970; yr < y; ++yr) {
      const bool leap = ((yr % 4 == 0) && (yr % 100 != 0)) || (yr % 400 == 0);
      sec += (leap ? 366L : 365L) * 86400L;
    }
    delta = sec;
  }

  inited = true;
  return delta;
}

// Formats according to a small strftime-like subset into 'out' (NUL-terminated).
// Supported: %%, %a, %b, %d, %-d, %m, %-m, %Y, %H, %-H, %M, %-M, %S, %-S, %I, %-I, %p
// Timezone: prefers localTz if valid, else UTC tz. Input epoch is UTC seconds.
void TMTime_strftime(const char* fmt, char* out, size_t n, time_t utc_epoch) {
  if (!out || n == 0) return;
  out[0] = '\0';
  if (!fmt || !*fmt) fmt = "%H:%M";

  // --- FIX: convert Unix seconds -> AceTime seconds before building ODT ---
  const long delta = DetectUnixToAceDelta();
  const ace_time::acetime_t aceEpoch =
      (ace_time::acetime_t)((long)utc_epoch - delta);

  // Resolve date/time fields from AceTime using configured zones
  const bool haveLocal = (s_localTzPtr && !s_localTzPtr->isError());
  const OffsetDateTime odt = haveLocal
      ? s_localTzPtr->getOffsetDateTime(aceEpoch)
      : s_utcTzPtr->getOffsetDateTime(aceEpoch);

  // English abbreviations (fixed, non-locale)
  static const char* WD_ABBR[7]  = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
  static const char* MON_ABBR[12]= {"Jan","Feb","Mar","Apr","May","Jun",
                                    "Jul","Aug","Sep","Oct","Nov","Dec"};

  auto safe_len = [&](const char* s) -> size_t {
    return s ? strlen(s) : 0;
  };

  auto appendChar = [&](char c) {
    size_t len = strnlen(out, n);
    if (len + 1 < n) { out[len] = c; out[len + 1] = '\0'; }
  };

  auto appendStr = [&](const char* s) {
    if (!s) return;
    size_t len = strnlen(out, n);
    size_t sl  = safe_len(s);
    if (len < n) {
      size_t copy = (sl <= (n - 1 - len)) ? sl : (n - 1 - len);
      memcpy(out + len, s, copy);
      out[len + copy] = '\0';
    }
  };

  auto append2Pad = [&](int v) {
    char b[3];
    b[0] = '0' + ((v / 10) % 10);
    b[1] = '0' + (v % 10);
    b[2] = '\0';
    appendStr(b);
  };

  auto appendUInt = [&](int v) {
    // Append positive int without padding
    char buf[12];
    int idx = sizeof(buf) - 1;
    buf[idx--] = '\0';
    if (v == 0) { buf[idx] = '0'; appendStr(&buf[idx]); return; }
    unsigned int x = (v < 0) ? (unsigned int)(-v) : (unsigned int)v;
    while (x && idx >= 0) { buf[idx--] = (char)('0' + (x % 10)); x /= 10; }
    appendStr(&buf[idx + 1]);
  };

  auto append4Year = [&](int v) {
    char b[5];
    b[0] = '0' + ((v / 1000) % 10);
    b[1] = '0' + ((v / 100) % 10);
    b[2] = '0' + ((v / 10) % 10);
    b[3] = '0' + (v % 10);
    b[4] = '\0';
    appendStr(b);
  };

  auto hour12 = [&](int h24) -> int {
    int h = h24 % 12;
    return (h == 0) ? 12 : h;
  };

  const int Y = odt.year();
  const int m = odt.month();       // 1..12
  const int d = odt.day();         // 1..31
  const int H = odt.hour();        // 0..23
  const int M = odt.minute();      // 0..59
  const int S = odt.second();      // 0..59
  int wd = odt.dayOfWeek();        // 1=Mon .. 7=Sun
  wd = (wd == 7) ? 0 : wd;         // map to 0=Sun .. 6=Sat for WD_ABBR

  for (const char* p = fmt; *p; ++p) {
    if (*p != '%') { appendChar(*p); continue; }
    ++p;
    if (!*p) break;

    // Optional '-' flag to disable zero-padding: e.g. %-d, %-m, %-H, %-M, %-S, %-I
    bool no_pad = false;
    if (*p == '-') {
      no_pad = true;
      ++p;
      if (!*p) break;
    }

    switch (*p) {
      case '%': appendChar('%'); break;

      // Weekday and month abbreviations (English)
      case 'a': appendStr(WD_ABBR[(wd >= 0 && wd < 7) ? wd : 0]); break;
      case 'b': appendStr(MON_ABBR[(m >= 1 && m <= 12) ? (m - 1) : 0]); break;

      // Day of month
      case 'd':
        if (no_pad) appendUInt(d);
        else        append2Pad(d);
        break;

      // Month number
      case 'm':
        if (no_pad) appendUInt(m);
        else        append2Pad(m);
        break;

      // Year (4-digit)
      case 'Y': append4Year(Y); break;

      // 24-hour, minute, second
      case 'H': { int v = H; if (no_pad) appendUInt(v); else append2Pad(v); } break;
      case 'M': { int v = M; if (no_pad) appendUInt(v); else append2Pad(v); } break;
      case 'S': { int v = S; if (no_pad) appendUInt(v); else append2Pad(v); } break;

      // 12-hour and AM/PM
      case 'I': { int v = hour12(H); if (no_pad) appendUInt(v); else append2Pad(v); } break;
      case 'p': appendStr((H < 12) ? "AM" : "PM"); break;

      default:
        // Unknown tokens: emit literally for robustness
        appendChar('%');
        if (no_pad) appendChar('-');
        appendChar(*p);
        break;
    }
  }
}

void TMTime_debugPrint()
{
#if DEBUG
  if (!TMTime_hasTime() || s_epochUTC == 0) {
    debugln("TMTime: not synced yet (epoch=0) — skipping debug print");
    return;
  }

  // Convert Unix -> AceTime seconds once for this print using runtime-detected delta.
  const long delta = DetectUnixToAceDelta();
  const ace_time::acetime_t aceEpoch =
      (ace_time::acetime_t)((long)s_epochUTC - delta);

  const bool utcErr = (!s_utcTzPtr || s_utcTzPtr->isError());
  const bool locErr = (!s_localTzPtr || s_localTzPtr->isError());

  const ace_time::OffsetDateTime odtUtc = utcErr
    ? ace_time::OffsetDateTime::forEpochSeconds(
          aceEpoch,
          ace_time::TimeOffset::forSeconds(0))
    : s_utcTzPtr->getOffsetDateTime(aceEpoch);

  const ace_time::OffsetDateTime odtLoc = locErr
      ? odtUtc
      : s_localTzPtr->getOffsetDateTime(aceEpoch);

  // Correct local UTC offset from zone (do NOT diff epochs; they represent the same instant)
  const long off_s = (long)odtLoc.timeOffset().toSeconds();

  // Short "HH:MM" suffixes
  auto fmt_hhmm = [](char out[6], uint8_t hh, uint8_t mm) {
    out[0] = '0' + (hh / 10);
    out[1] = '0' + (hh % 10);
    out[2] = ':';
    out[3] = '0' + (mm / 10);
    out[4] = '0' + (mm % 10);
    out[5] = '\0';
  };
  char utc_hhmm[6]; fmt_hhmm(utc_hhmm, (uint8_t)odtUtc.hour(), (uint8_t)odtUtc.minute());
  char loc_hhmm[6]; fmt_hhmm(loc_hhmm, (uint8_t)odtLoc.hour(), (uint8_t)odtLoc.minute());

  debugf("TMTime: synced=yes, epoch=%lu\n", (unsigned long)s_epochUTC);

  Serial.print("        utc=");
  odtUtc.printTo(Serial);                  // e.g., 2025-10-02T08:49:19+00:00
  Serial.print("  (");
  Serial.print(utc_hhmm);
  Serial.print(" UTC)\n");

  Serial.print("        local=");
  odtLoc.printTo(Serial);                  // e.g., 2025-10-02T10:49:19+02:00
  Serial.print("  (");
  Serial.print(loc_hhmm);
  Serial.print(' ');
  if (!locErr) s_localTzPtr->printShortTo(Serial); else Serial.print("UTC");
  Serial.print("), offset_s=");
  Serial.print(off_s);
  Serial.print('\n');
#else
  (void)s_epochUTC;
#endif
}

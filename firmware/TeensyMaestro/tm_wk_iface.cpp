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

#include "tm_wk_iface.h"
#include "tm_wk_debug.h"
#include <Arduino.h>
#include "tm_wk_proto.h"

// Keyer runtime flags from Keyer.ino (C++ linkage)
extern volatile bool MsgActive;  // active message context
extern volatile bool KeyDown;    // key line asserted

// RAM2 TXQ capacity helper from Keyer.ino
extern uint16_t Keyer_TxQ_CapacityBytes(void);

uint16_t TM_KeyerAdapter::txqCapacityBytes() const {
  return Keyer_TxQ_CapacityBytes();
}

extern "C" {
// Return count of bytes currently in the keyer's internal TX queue.
  uint16_t Keyer_TxQ_Used(void);
  void WK_OnCharEcho(uint8_t ch);
}

// Use existing Keyer.ino primitives (queue based, RAM2-backed)
extern bool   Keyer_TxQ_EnqueueRaw(const char* data, size_t len);
extern void   Keyer_ServiceQueue();
extern void   Keyer_Apply_Wpm(int newWpm, bool preserveBaseline);

// Keyer runtime flags from Keyer.ino (C++ linkage)
extern volatile bool MsgActive;          // active message context
extern volatile bool KeyDown;            // key line asserted
extern volatile bool g_KeyerTimingActive; // element or inter-element timing active
extern volatile bool StraightKeyActive;  // straight key held (if you want this covered)
extern volatile bool ElementWait;

// Hard abort hook implemented in Keyer.ino (C++ function, but we declare it normally)
extern void Keyer_AbortNow(void);

extern byte DotPin;                 // e.g. byte DotPin  = 30;
extern byte DashPin;                // e.g. byte DashPin = 31;
// Do NOT extern KeyOutPin (const gives internal linkage). Use a local define instead.
#ifndef TM_WK_KEYOUT_PIN
#define TM_WK_KEYOUT_PIN 33
#endif
extern String KeyerOut;             // e.g. String KeyerOut = "LOCAL";

// Speed/timing used by Keyer.ino
extern volatile int  WPM;           // e.g. volatile int  WPM        = 24;
extern volatile long ElementLen;    // e.g. volatile long ElementLen = 1;

extern volatile int  CWVal;
extern volatile int  CWValSave;

TM_KeyerAdapter::TM_KeyerAdapter() {
  WK_DEBUGLN(F("WK: KeyerAdapter init"));
}

TM_KeyerAdapter::~TM_KeyerAdapter() = default;

// --- Transport / control ---

void TM_KeyerAdapter::startPTT() {
  // No dedicated PTT in current CW path; key line handles TX
  WK_DEBUGLN(F("WK: startPTT (no-op)"));
}

void TM_KeyerAdapter::stopPTT() {
  WK_DEBUGLN(F("WK: stopPTT (no-op)"));
}

void TM_KeyerAdapter::keyImmediate(bool down) {
  // LOCAL: drive key line directly; ETHERNET path can be added later
  if (KeyerOut == "LOCAL") {
    digitalWrite(TM_WK_KEYOUT_PIN, down ? HIGH : LOW);
  } else {
    // TODO: implement ETHERNET key down/up if needed (Flex "cw key ..." API)
  }
#if WK_INFO_TRACE
  WK_DEBUGF("WK: keyImmediate=%u\n", (unsigned)down);
#endif
}

void TM_KeyerAdapter::abortNow() {
  // Hard stop keying NOW (kills active element/timers in Keyer.ino)
  Keyer_AbortNow();

#if WK_INFO_TRACE
  WK_DEBUGLN(F("WK: abortNow()"));
#endif
}

void TM_KeyerAdapter::clearTextQueue() {
  // Defensive: make sure ESC-like behavior always hard-stops keying first.
  abortNow();

  // Ensure key line is up and PTT is released (safe no-ops if already idle)
  keyImmediate(false);
  stopPTT();

  // Drain by servicing the keyer a few times, or until empty, with a small timeout
  const uint32_t t0 = millis();
  while (Keyer_TxQ_Used() > 0 && (uint32_t)(millis() - t0) < 50) {
    Keyer_ServiceQueue();
    delay(1);
  }

  // One last nudge just in case a final element got scheduled
  for (int i = 0; i < 2; ++i) {
    Keyer_ServiceQueue();
    delay(1);
  }

  WK_DEBUGLN(F("WK: clearTextQueue() best-effort drain complete"));
}

uint16_t TM_KeyerAdapter::txqUsed() const {
  // This is the function the linker is currently complaining about.
  // With this definition present, the vtable entry is satisfied.
  return Keyer_TxQ_Used();
}

// --- Text enqueue (allocation-free normalization to RAM2 queue) ---

size_t TM_KeyerAdapter::enqueueText(const char* text, size_t len) {
  if (!text || len == 0) return 0;

  // Streaming normalizer: writes normalized 7-bit ASCII into a small stack buffer,
  // flushes to RAM2 queue in chunks. No dynamic allocations here.
  char   outBuf[96];
  size_t outLen   = 0;
  size_t accepted = 0;

  bool lastWasSpace = false;

  // Track a pending UTF-8 lead byte 0xC3 (ÅÄÖÜ åäöü)
  bool utf8LeadC3 = false;

  auto flush_chunk = [&](bool force) -> bool {
    if (outLen == 0) return true;
    // Enqueue to RAM2-backed queue; will split further internally if needed
    bool ok = Keyer_TxQ_EnqueueRaw(outBuf, outLen);
#if WK_INFO_TRACE
    WK_DEBUGF("WK: enqueue %u bytes => %s\n",
              (unsigned)outLen, ok ? "TxQ(RAM2)" : "DROP (TxQ full)");
#endif
    outLen = 0;
    return ok || !force; // if force==true and !ok => propagate failure
  };

  for (size_t i = 0; i < len; ++i) {
    uint8_t u = (uint8_t)text[i];

    // 1) Drop CR/LF early
    if (u == '\r' || u == '\n') continue;

    // 2) UTF-8 mapping: C3 xx -> placeholders ^ { } ~
    if (!utf8LeadC3) {
      if (u == 0xC3) { utf8LeadC3 = true; continue; }
    } else {
      char repl = 0;
      switch (u) {
        case 0x85: case 0xA5: repl = '^'; break; // Å/å
        case 0x84: case 0xA4: repl = '{'; break; // Ä/ä
        case 0x96: case 0xB6: repl = '}'; break; // Ö/ö
        case 0x9C: case 0xBC: repl = '~'; break; // Ü/ü
        default: break;
      }
      if (repl) {
        if (!(lastWasSpace && repl == ' ')) {
          if (outLen == sizeof(outBuf)) {
            if (!flush_chunk(true)) return accepted; // queue full
          }
          outBuf[outLen++] = repl;
          accepted++;
          lastWasSpace = (repl == ' ');
        }
      }
      utf8LeadC3 = false; // consume the trail byte regardless
      continue;
    }

    // 3) Latin-1 / CP1252 single-byte mapping -> placeholders
    if      (u == 0xC5 || u == 0xE5) { u = '^'; } // Å/å
    else if (u == 0xC4 || u == 0xE4) { u = '{'; } // Ä/ä
    else if (u == 0xD6 || u == 0xF6) { u = '}'; } // Ö/ö
    else if (u == 0xDC || u == 0xFC) { u = '~'; } // Ü/ü

    // 4) Protocol is 7-bit ASCII: after our mappings, drop any remaining high-bit bytes
    if (u & 0x80) {
#if WK_INFO_TRACE
      WK_DEBUGF("WK: drop hi byte 0x%02X\n", (unsigned)u);
#endif
      continue;
    }

    // 5) TAB -> space; compress spaces
    if (u == '\t') u = ' ';
    if (u == ' ') {
      if (lastWasSpace) continue;
      lastWasSpace = true;
    } else {
      lastWasSpace = false;
    }

    // 6) Append printable ASCII into local buffer
    if (outLen == sizeof(outBuf)) {
      if (!flush_chunk(true)) return accepted; // queue full
    }
    outBuf[outLen++] = (char)u;
    accepted++;
  }

  // Flush any residual bytes (non-forced: ok to skip if queue full)
  flush_chunk(false);

  // Kick the service so next chunk starts if idle
  if (accepted > 0) Keyer_ServiceQueue();

  return accepted;
}

// --- Parameters ---

uint8_t TM_KeyerAdapter::getWpm() const {
  // Report the actual current keyer WPM
  return (uint8_t)WPM;
}

void TM_KeyerAdapter::setWpm(uint8_t wpm)
{
  Keyer_Apply_Wpm((int)wpm, /*preserveBaseline=*/true);  // host should not change CWValSave
#if WK_INFO_TRACE
  WK_DEBUGF("WK: setWpm=%u (ElementLen=%ld) [host path, baseline preserved]\n",
            (unsigned)wpm, (long)ElementLen);
#endif
}

void TM_KeyerAdapter::setWeightPermille(uint16_t per_mille) {
  // TODO: map to your keyer's weighting
  (void)per_mille;
#if WK_INFO_TRACE
  WK_DEBUGF("WK: setWeight=%u permille\n", (unsigned)per_mille);
#endif
}

void TM_KeyerAdapter::setDitDahRatio(uint8_t ratio) {
  // TODO: map to your keyer's dit/dah ratio
  (void)ratio;
#if WK_INFO_TRACE
  WK_DEBUGF("WK: setRatio=%u\n", ratio);
#endif
}

void TM_KeyerAdapter::setSidetone(bool on) {
  // TODO: enable/disable sidetone
  (void)on;
#if WK_INFO_TRACE
  WK_DEBUGF("WK: setSidetone=%u\n", (unsigned)on);
#endif
}

// --- Status queries ---

bool TM_KeyerAdapter::isBusy() const {
  // Treat BUSY as “host should not assume idle”:
  // - timing-critical keyer activity (best signal)
  // - currently key-down (redundant but cheap)
  // - currently in element/space wait
  // - message engine active
  // - queued raw bytes pending
  if (g_KeyerTimingActive) return true;
  if (KeyDown)            return true;
  if (ElementWait)        return true;
  if (MsgActive)          return true;
  return (txqUsed() > 0);
}

bool TM_KeyerAdapter::isPaddlePressed() const {
  // Paddles use INPUT_PULLUP, active low
  int d = digitalRead(DotPin);
  int h = digitalRead(DashPin);
  return (d == LOW) || (h == LOW);
}

bool TM_KeyerAdapter::isKeyDown() const {
  // Reflect current TX key line state (HIGH = keyed)
  return digitalRead(TM_WK_KEYOUT_PIN) == HIGH;
}

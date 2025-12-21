/*
  tm_wk_iface.cpp

  TeensyMaestro — Community Edition (CE)
  SPDX-License-Identifier: CC-BY-NC-SA-3.0
  SPDX-FileCopyrightText: 2025 TNX QSO

  A community-maintained edition with open-source utilities
  for ham radio enthusiasts, focusing on FlexRadio® and Wavelog integrations.
*/

#include "tm_wk_iface.h"
#include "tm_wk_debug.h"
#include "TM_Keyer_Engine.h"

// Reference to the global engine instance created in TeensyMaestro.ino
extern TM_Keyer_Engine g_keyerEngine;

// External hardware references (for immediate/panic actions)
extern byte DotPin;
extern byte DashPin;
extern String KeyerOut;
#ifndef TM_WK_KEYOUT_PIN
#define TM_WK_KEYOUT_PIN 33
#endif

// --- TM_KeyerAdapter Implementation ---

TM_KeyerAdapter::TM_KeyerAdapter() {
  WK_DEBUGLN(F("WK: KeyerAdapter init (Engine V2)"));
}

TM_KeyerAdapter::~TM_KeyerAdapter() = default;

void TM_KeyerAdapter::startPTT() {
  // WinKey command <18><1> (Buffered PTT On)
  // We queue this event so it happens in sync with text.
  g_keyerEngine.enqueuePtt(true);
}

void TM_KeyerAdapter::stopPTT() {
  // WinKey command <18><0> (Buffered PTT Off)
  g_keyerEngine.enqueuePtt(false);
}

void TM_KeyerAdapter::keyImmediate(bool down) {
  // Direct hardware control (bypass engine queue)
  // Used for test/diagnostics primarily.
  if (KeyerOut == "LOCAL") {
    digitalWrite(TM_WK_KEYOUT_PIN, down ? HIGH : LOW);
  }
}

void TM_KeyerAdapter::abortNow() {
  g_keyerEngine.abortNow();
  WK_DEBUGLN(F("WK: abortNow() triggered"));
}

void TM_KeyerAdapter::clearTextQueue() {
  // Use the new graceful clear
  g_keyerEngine.clearQueue();
  WK_DEBUGLN(F("WK: clearTextQueue() -> Engine clearQueue()"));
}

uint16_t TM_KeyerAdapter::txqUsed() const {
  return g_keyerEngine.getQueueSize();
}

uint16_t TM_KeyerAdapter::txqCapacityBytes() const {
  // Return a safe constant
  return 256; 
}

// --- Text Enqueue ---

size_t TM_KeyerAdapter::enqueueText(const char* text, size_t len) {
  if (!text || len == 0) return 0;

#if WK_INFO_TRACE
  Serial.print("WK_IFACE: enqueueText len="); Serial.println(len);
#endif

  size_t accepted = 0;

  for (size_t i = 0; i < len; ++i) {
    uint8_t u = (uint8_t)text[i];

    // 1. Filter: Drop CR/LF
    if (u == '\r' || u == '\n') continue;

    // 2. Filter: UTF-8 / Latin-1 mapping placeholders
    if      (u == 0xC5 || u == 0xE5) { u = '^'; } // Å/å 
    else if (u == 0xC4 || u == 0xE4) { u = '{'; } // Ä/ä
    else if (u == 0xD6 || u == 0xF6) { u = '}'; } // Ö/ö
    else if (u == 0xDC || u == 0xFC) { u = '~'; } // Ü/ü

    // 3. Drop high-bit junk if not mapped
    if (u & 0x80) continue; 

#if WK_INFO_TRACE
    Serial.print("WK_IFACE: Push char '"); Serial.print((char)u); Serial.println("'");
#endif

    // 4. Enqueue to Engine
    if (g_keyerEngine.enqueueChar((char)u)) {
      accepted++;
    } else {
      // Queue full
#if WK_INFO_TRACE
      Serial.println("WK_IFACE: Queue FULL!");
#endif
      break;
    }
  }

  return accepted;
}

// --- Parameters ---

uint8_t TM_KeyerAdapter::getWpm() const {
  return g_keyerEngine.getWpm();
}

void TM_KeyerAdapter::setWpm(uint8_t wpm) {
  // Queue the speed change to ensure sync
  g_keyerEngine.enqueueWpm(wpm);
  
#if WK_INFO_TRACE
  WK_DEBUGF("WK: enqueueWpm=%u\n", (unsigned)wpm);
#endif
}

void TM_KeyerAdapter::setWeightPermille(uint16_t per_mille) {
  // Map 1000 = 100% -> 50 (standard 1:1 in WinKey)
  uint8_t w = 50;
  if (per_mille > 0) w = per_mille / 20; 
  g_keyerEngine.setWeighting(w);
}

void TM_KeyerAdapter::setDitDahRatio(uint8_t ratio) {
  g_keyerEngine.setRatio(ratio);
}

void TM_KeyerAdapter::setSidetone(bool on) {
  (void)on; 
}

// --- Status ---

bool TM_KeyerAdapter::isBusy() const {
  return g_keyerEngine.isBusy();
}

bool TM_KeyerAdapter::isPaddlePressed() const {
  // Direct hardware check (Engine doesn't own the pins yet)
  int d = digitalRead(DotPin);
  int h = digitalRead(DashPin);
  return (d == LOW) || (h == LOW);
}

bool TM_KeyerAdapter::isKeyDown() const {
  return g_keyerEngine.isTransmitting();
}

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

extern TM_Keyer_Engine g_keyerEngine;
extern String KeyerOut;
#ifndef TM_WK_KEYOUT_PIN
#define TM_WK_KEYOUT_PIN 33
#endif

TM_KeyerAdapter::TM_KeyerAdapter() {
  WK_DEBUGLN(F("WK: KeyerAdapter init (Engine V2)"));
}

TM_KeyerAdapter::~TM_KeyerAdapter() = default;

void TM_KeyerAdapter::startPTT() {
  g_keyerEngine.enqueuePtt(true);
}

void TM_KeyerAdapter::stopPTT() {
  g_keyerEngine.enqueuePtt(false);
}

void TM_KeyerAdapter::keyImmediate(bool down) {
  if (KeyerOut == "LOCAL") {
    digitalWrite(TM_WK_KEYOUT_PIN, down ? HIGH : LOW);
  }
}

void TM_KeyerAdapter::abortNow() {
  g_keyerEngine.abortNow();
}

void TM_KeyerAdapter::clearTextQueue() {
  g_keyerEngine.clearQueue();
}

uint16_t TM_KeyerAdapter::txqUsed() const {
  return g_keyerEngine.getQueueSize();
}

uint16_t TM_KeyerAdapter::txqCapacityBytes() const {
  return 256; 
}

size_t TM_KeyerAdapter::enqueueText(const char* text, size_t len) {
  if (!text || len == 0) return 0;
  size_t accepted = 0;
  for (size_t i = 0; i < len; ++i) {
    uint8_t u = (uint8_t)text[i];
    if (u == '\r' || u == '\n') continue;
    if      (u == 0xC5 || u == 0xE5) { u = '^'; } 
    else if (u == 0xC4 || u == 0xE4) { u = '{'; } 
    else if (u == 0xD6 || u == 0xF6) { u = '}'; } 
    else if (u == 0xDC || u == 0xFC) { u = '~'; } 
    if (u & 0x80) continue; 
    if (g_keyerEngine.enqueueChar((char)u)) {
      accepted++;
    } else {
      break;
    }
  }
  return accepted;
}

uint8_t TM_KeyerAdapter::getWpm() const {
  return g_keyerEngine.getWpm();
}

void TM_KeyerAdapter::setWpm(uint8_t wpm) {
  g_keyerEngine.enqueueWpm(wpm);
}

void TM_KeyerAdapter::setWeightPermille(uint16_t per_mille) {
  uint8_t w = 50;
  if (per_mille > 0) w = per_mille / 20; 
  g_keyerEngine.setWeighting(w);
}

void TM_KeyerAdapter::setDitDahRatio(uint8_t ratio) {
  g_keyerEngine.setRatio(ratio);
}

void TM_KeyerAdapter::setKeyCompensation(uint8_t ms) {
    g_keyerEngine.setCompensation(ms);
}

void TM_KeyerAdapter::setFirstExtension(uint8_t ms) {
    g_keyerEngine.setFirstExtension(ms);
}

void TM_KeyerAdapter::setFarnsworth(uint8_t wpm) {
    g_keyerEngine.setFarnsworth(wpm);
}

void TM_KeyerAdapter::setSidetone(bool on) {
    (void)on; 
}

bool TM_KeyerAdapter::isBusy() const {
  return g_keyerEngine.isBusy();
}

bool TM_KeyerAdapter::isPaddlePressed() const {
  return g_keyerEngine.isPaddleActive();
}

bool TM_KeyerAdapter::isKeyDown() const {
  return g_keyerEngine.isTransmitting();
}
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

#pragma once
/* === TM TOUCH HELPERS (header) ===
 * Centralized raw→pixel mapping for XPT2046 so the rest of the code works in screen pixels.
 * Matches TFT landscape mode. All comments in English.
 */

#include <Arduino.h>
#include <XPT2046_Touchscreen.h> 

// ---- Screen geometry (match your TFT setup) ----
static constexpr uint16_t TM_TOUCH_SCREEN_W = 480;
static constexpr uint16_t TM_TOUCH_SCREEN_H = 320;

// ---- Calibration (raw min/max) ----
#ifndef TM_TPX_MIN
  #define TM_TPX_MIN  250
#endif
#ifndef TM_TPX_MAX
  #define TM_TPX_MAX  3720
#endif
#ifndef TM_TPY_MIN
  #define TM_TPY_MIN  300
#endif
#ifndef TM_TPY_MAX
  #define TM_TPY_MAX  3810
#endif

// ---- Touch filtering ----
#ifndef TM_TOUCH_Z_MIN
  #define TM_TOUCH_Z_MIN 60   // ignore lighter touches/noise
#endif

struct TM_TouchPoint {
  uint16_t x;   // pixel X (0..W-1), left→right
  uint16_t y;   // pixel Y (0..H-1), top→bottom
  uint16_t z;   // raw Z
  bool     valid;
};

static inline uint16_t TM_clamp_u16(int v, int lo, int hi) {
  if (v < lo) return (uint16_t)lo;
  if (v > hi) return (uint16_t)hi;
  return (uint16_t)v;
}

static inline uint16_t TM_map_u16(int v, int in_min, int in_max, int out_min, int out_max) {
  if (in_max == in_min) return (uint16_t)out_min;
  long num = (long)(v - in_min) * (long)(out_max - out_min);
  long den = (long)(in_max - in_min);
  long res = (long)out_min + num / den;
  return (uint16_t)TM_clamp_u16((int)res, out_min, out_max);
}

// --- Externals owned by the sketch (no extra dependencies here) -----------
extern bool GotTouch;
extern XPT2046_Touchscreen touch;

// Read one sample and map to pixels. Returns .valid=false if no meaningful touch.
// Assumes touch.setRotation(3) so axes align with TFT: rx→pixel Y, ry→pixel X (no inversion).
static inline TM_TouchPoint TM_ReadTouchMapped() {
  TM_TouchPoint out{0,0,0,false};

  if (!GotTouch) return out;
  if (!touch.touched()) return out;

  uint16_t rx, ry;
  uint8_t  rz;
  touch.readData(&rx, &ry, &rz);
  out.z = rz;

  if (rz < TM_TOUCH_Z_MIN) {
    return out; // too light / noise
  }

  // Invert both axes so pixel (0,0) is top-left, (W-1,H-1) is bottom-right.
  const uint16_t px = TM_map_u16(rx, TM_TPX_MAX, TM_TPX_MIN, 0, TM_TOUCH_SCREEN_W - 1);
  const uint16_t py = TM_map_u16(ry, TM_TPY_MAX, TM_TPY_MIN, 0, TM_TOUCH_SCREEN_H - 1);

  out.x = px;
  out.y = py;
  out.valid = true;
  return out;
}

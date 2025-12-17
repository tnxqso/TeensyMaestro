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

/*
  Shared encoder helpers (header-only)
  Used by: tm_enc_agc_bw_handlers.h, tm_enc_vfo_vol_handlers.h
  Behavior-only utilities, no side effects beyond encoder write-back on clamp.
*/

#include "tm_project.hpp"

// Quantized step check
template <typename EncT>
static inline bool enc_is_on_step(EncT& enc, const int steps) {
  return (enc.read() % steps) == 0;
}

// Quantized read
template <typename EncT>
static inline int enc_read_quantized(EncT& enc, const int steps) {
  return enc.read() / steps;
}

// Quantized write
template <typename EncT>
static inline void enc_write_quantized(EncT& enc, const int v, const int steps) {
  enc.write(v * steps);
}

// Clamp to [minV, maxV] and mirror clamped value back to encoder
template <typename EncT>
static inline int clamp_and_sync_encoder(EncT& enc, int value, const int minV, const int maxV, const int steps) {
  if (value < minV) {
    value = minV;
    enc_write_quantized(enc, minV, steps);
  } else if (value > maxV) {
    value = maxV;
    enc_write_quantized(enc, maxV, steps);
  }
  return value;
}

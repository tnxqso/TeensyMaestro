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
// BandPlan.h — RAM2-only band plan
// Single working table in RAM2 (DMAMEM) with compile-time initializer.
// No separate FLASH defaults and no init function.

#include <Arduino.h>

#define BandTable_Max 38  // authoritative row count

// Column mapping for the 10 entries per row
enum {
  LC1_START = 0, LC1_END = 1,
  LC2_START = 2, LC2_END = 3,
  LC3_START = 4, LC3_END = 5,
  LC4_START = 6, LC4_END = 7,
  LC5_START = 8, LC5_END = 9
};

// RAM2 working table (mutable).
// Initialized at startup from the firmware image; no extra code needed.
extern int32_t BandTable[BandTable_Max][10];

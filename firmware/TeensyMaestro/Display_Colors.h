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
#include <stdint.h>

/*
  Display_Colors.h
  Centralized 16-bit RGB565 color constants, plus a simple boot UI theme.
  Works with ST7796_t3 and similar GFX-style drivers.

  Important
  - Some projects define COLOR_* as macros already, for example mapping to ST77XX_*.
    To avoid redefinition conflicts, each COLOR_* here is only defined if it is not
    already defined as a macro.
  - BootTheme uses COLOR_* names, regardless of whether they are macros or constexprs.
*/

// ---- Core RGB565 palette ----
// Define only if not provided by the project as macros.
#ifndef COLOR_BLACK
static constexpr uint16_t COLOR_BLACK      = 0x0000;
#endif
#ifndef COLOR_NAVY
static constexpr uint16_t COLOR_NAVY       = 0x000F;
#endif
#ifndef COLOR_DARKGREEN
static constexpr uint16_t COLOR_DARKGREEN  = 0x03E0;
#endif
#ifndef COLOR_DARKCYAN
static constexpr uint16_t COLOR_DARKCYAN   = 0x03EF;
#endif
#ifndef COLOR_MAROON
static constexpr uint16_t COLOR_MAROON     = 0x7800;
#endif
#ifndef COLOR_PURPLE
static constexpr uint16_t COLOR_PURPLE     = 0x780F;
#endif
#ifndef COLOR_OLIVE
static constexpr uint16_t COLOR_OLIVE      = 0x7BE0;
#endif
#ifndef COLOR_LIGHTGREY
static constexpr uint16_t COLOR_LIGHTGREY  = 0xC618;
#endif
#ifndef COLOR_DARKGREY
static constexpr uint16_t COLOR_DARKGREY   = 0x7BEF;
#endif
#ifndef COLOR_BLUE
static constexpr uint16_t COLOR_BLUE       = 0x001F;
#endif
#ifndef COLOR_GREEN
static constexpr uint16_t COLOR_GREEN      = 0x07E0;
#endif
#ifndef COLOR_CYAN
static constexpr uint16_t COLOR_CYAN       = 0x07FF;
#endif
#ifndef COLOR_RED
static constexpr uint16_t COLOR_RED        = 0xF800;
#endif
#ifndef COLOR_MAGENTA
static constexpr uint16_t COLOR_MAGENTA    = 0xF81F;
#endif
#ifndef COLOR_YELLOW
static constexpr uint16_t COLOR_YELLOW     = 0xFFE0;
#endif
#ifndef COLOR_WHITE
static constexpr uint16_t COLOR_WHITE      = 0xFFFF;
#endif
#ifndef COLOR_ORANGE
static constexpr uint16_t COLOR_ORANGE     = 0xFD20;
#endif
#ifndef COLOR_GREENYELLOW
static constexpr uint16_t COLOR_GREENYELLOW= 0xAFE5;
#endif
#ifndef COLOR_PINK
static constexpr uint16_t COLOR_PINK       = 0xFC9F;
#endif

// ---- Utility: pack 8-bit RGB to RGB565 ----
static inline constexpr uint16_t RGB888_to_565(uint8_t r, uint8_t g, uint8_t b) {
  return (static_cast<uint16_t>(r & 0xF8) << 8) |
         (static_cast<uint16_t>(g & 0xFC) << 3) |
         (static_cast<uint16_t>(b >> 3));
}

// ---- Boot UI theme defaults ----
// Theme aliases can be tuned without changing UI code.
// These use COLOR_* which may be macros or constexprs.
namespace BootTheme {
  static constexpr uint16_t Background    = COLOR_NAVY;
  static constexpr uint16_t PrimaryText   = COLOR_YELLOW;
  static constexpr uint16_t SecondaryText = COLOR_LIGHTGREY;
  static constexpr uint16_t Separator     = COLOR_DARKGREY;
  static constexpr uint16_t Accent        = COLOR_ORANGE;
  static constexpr uint16_t Ok            = COLOR_GREEN;
  static constexpr uint16_t Warn          = COLOR_ORANGE;
  static constexpr uint16_t Error         = COLOR_RED;
}

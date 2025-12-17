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
  Display_Fonts.h
  Centralized font declarations for TeensyMaestro Community Edition display code.

  Provides extern references to all fonts used by ST77xx_t3 / ST7796_t3 drivers.
  Include this header wherever text rendering is needed.

  The actual font definitions come from:
    - <st7735_t3_font_Arial.h>
    - <ST7735_t3_font_ArialBold.h>
    - <st7735_t3_font_ComicSansMS.h>
*/

#include <ST7796_t3.h>
#include <st7735_t3_font_Arial.h>
#include <ST7735_t3_font_ArialBold.h>
//#include <st7735_t3_font_ComicSansMS.h>

// ---- Font aliases used across TeensyMaestro Community Edition ----
// Keep these names consistent with your display code (e.g., SplashScreen, widgets)

extern const ILI9341_t3_font_t Arial_14;
extern const ILI9341_t3_font_t Arial_20_Bold;
extern const ILI9341_t3_font_t Arial_24_Bold;
extern const ILI9341_t3_font_t Arial_28_Bold;

//extern const ILI9341_t3_font_t ComicSansMS_14;
//extern const ILI9341_t3_font_t ComicSansMS_20;
//extern const ILI9341_t3_font_t ComicSansMS_24;
//extern const ILI9341_t3_font_t ComicSansMS_28;

// ---- Optional convenience macros ----
#define FONT_TITLE       Arial_28_Bold
//#define FONT_SUBTITLE    ComicSansMS_20
#define FONT_BODY        Arial_14
//#define FONT_SMALL       ComicSansMS_14

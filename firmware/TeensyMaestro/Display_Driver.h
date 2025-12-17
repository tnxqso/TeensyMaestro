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
#include <Arduino.h>
#include <ST7796_t3.h>
#include "Display_Colors.h"
#include "Display_Fonts.h"

// -------------------------------------------------------------------
// Pin ownership:
// The sketch defines these as 'const uint8_t' (or 'const byte') values.
// We only declare them here so other TUs can link against them.
// Example in your .ino:
//   const byte TFT_DC   = 9;
//   const byte TFT_RST  = 8;
//   const byte TFT_CS   = 10;
// -------------------------------------------------------------------
extern const uint8_t TFT_CS;
extern const uint8_t TFT_DC;
extern const uint8_t TFT_RST;

// --- Driver alias
using TFT_Driver = ST7796_t3;

// --- Global driver instance (defined in Display_Driver.cpp)
extern TFT_Driver tft;

namespace Display {
  // Put all display-bus lines in a known safe state and force a panel reset.
  // Call this once at the very beginning of setup(), before any drawing.
  inline void preInit(uint8_t touchCS /* e.g. TS_CS */) {
    // Chip selects HIGH = deasserted
    pinMode(TFT_CS, OUTPUT);  digitalWrite(TFT_CS, HIGH);
    pinMode(touchCS, OUTPUT); digitalWrite(touchCS, HIGH);

    // If a hardware RESET pin is available, pulse it so the controller forgets old state
    if (TFT_RST != 255) {
      pinMode(TFT_RST, OUTPUT);
      digitalWrite(TFT_RST, HIGH);
      delay(10);
      digitalWrite(TFT_RST, LOW);
      delay(20);
      digitalWrite(TFT_RST, HIGH);
      delay(120);   // give the controller time to come back
    } else {
      // No dedicated reset line: add a small delay so power-on settles
      delay(50);
    }
  }

  // Centralized init after preInit(). No rotation or CS confusion now.
  inline void init() {
    tft.init(320, 480);              // ST7796_t3 uses init(width, height)
    tft.setRotation(3);              // your standard landscape orientation
    // Draw over any residual content the hard way once, then apply theme
    //tft.fillScreen(COLOR_BLACK);
    tft.fillScreen(BootTheme::Background);
    tft.setTextColor(BootTheme::PrimaryText);
    tft.setCursor(0, 0);
  }
}

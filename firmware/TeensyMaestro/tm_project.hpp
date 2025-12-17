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
  TeensyMaestro Community Edition umbrella include for IntelliSense & build order hygiene
  Purpose:
    - Help IntelliSense/clangd "see" the same symbols/types the Arduino build sees,
      WITHOUT adding fragile extern stubs or changing runtime behavior.
    - Keep this umbrella minimal and safe. All includes are conditional via __has_include.

  Notes:
    - This header is intentionally permissive and side-effect free.
    - It relies on your existing project headers to declare globals, enums, menus, colors, etc.
    - If a header doesn’t exist in your variant, it’s silently skipped.
    - If you add new modules later, just extend the list below.
*/

#if __has_include(<Arduino.h>)
  #include <Arduino.h>
#endif

// ----------------------------- Core project API -------------------------------

#if __has_include("FlexRigTeensy.h")
  #include "FlexRigTeensy.h"   // radio API (fRig, slice[], setCwSpeed, setBand, etc)
#endif

#if __has_include("Display_Colors.h")
  #include "Display_Colors.h"  // COLOR_* constants
#endif

#if __has_include("Display_Routines.h")
  #include "Display_Routines.h" // Disp* helpers, drawing utils
#endif

// UI modules (names may vary by branch; harmless if missing)
#if __has_include("ui_boot.h")
  #include "ui_boot.h"
#endif

#if __has_include("ui_widgets.h")
  #include "ui_widgets.h"
#endif

// Menus / constants (indexes, arrays like MenuItem, BandMenu, etc)
#if __has_include("MenuDefs.h")
  #include "MenuDefs.h"
#endif

// Utility helpers you’ve referenced from handlers (naming may vary per branch)
#if __has_include("tm_utils.h")
  #include "tm_utils.h"
#endif

#if __has_include("tm_u_helpers.h")
  #include "tm_u_helpers.h"
#endif

// --------------------------- Common device libraries --------------------------

// Encoders
#if __has_include(<Encoder.h>)
  #include <Encoder.h>
#endif

// TFT display variants (pick up whichever your build actually uses)
#if __has_include(<Adafruit_GFX.h>)
  #include <Adafruit_GFX.h>
#endif

#if __has_include(<ILI9341_t3.h>)
  #include <ILI9341_t3.h>
#endif

#if __has_include(<ILI9488_t3.h>)
  #include <ILI9488_t3.h>
#endif

#if __has_include(<HX8357_t3.h>)
  #include <HX8357_t3.h>
#endif

// Touch (if present in your variant)
#if __has_include(<XPT2046_Touchscreen.h>)
  #include <XPT2046_Touchscreen.h>
#endif

// ------------------------- Optional: project-specific --------------------------
// Add more as your project grows, e.g.:
// #if __has_include("tm_clock_widget.h")
//   #include "tm_clock_widget.h"
// #endif

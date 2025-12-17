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
#include "tm_wk_config.h"

// Unified debug wrappers that prefer host project's debug macros if present,
// without redeclaring any symbols (avoids conflicts with tm_logging.h).

#if TM_WK_DEBUG

  // Prefer project-provided debug()/debugln() if they exist as macros.
  #ifdef debug
    #define WK_DEBUG_PRINT(s)    debug(s)
  #else
    #define WK_DEBUG_PRINT(s)    Serial.print(s)
  #endif

  #ifdef debugln
    #define WK_DEBUG_PRINTLN(s)  debugln(s)
  #else
    #define WK_DEBUG_PRINTLN(s)  Serial.println(s)
  #endif

  // Prefer project-provided debugf(...) if it exists as a macro.
  #ifdef debugf
    #define WK_DEBUGF(...)       debugf(__VA_ARGS__)
  #else
    inline void WK_DEBUGF(const char* fmt, ...) {
      char buf[160];
      va_list ap; va_start(ap, fmt);
      vsnprintf(buf, sizeof(buf), fmt, ap);
      va_end(ap);
      Serial.print(buf);
    }
  #endif

  inline void WK_DEBUG(const __FlashStringHelper* s){ WK_DEBUG_PRINT(s); }
  inline void WK_DEBUG(const char* s){ WK_DEBUG_PRINT(s); }
  inline void WK_DEBUGLN(const __FlashStringHelper* s){ WK_DEBUG_PRINTLN(s); }
  inline void WK_DEBUGLN(const char* s){ WK_DEBUG_PRINTLN(s); }
  #define WK_DBG_BEGIN() do{}while(0)

#else

  inline void WK_DEBUG(const __FlashStringHelper*) {}
  inline void WK_DEBUG(const char*) {}
  inline void WK_DEBUGLN(const __FlashStringHelper*) {}
  inline void WK_DEBUGLN(const char*) {}
  inline void WK_DEBUGF(const char*, ...) {}
  #define WK_DBG_BEGIN() do{}while(0)

#endif

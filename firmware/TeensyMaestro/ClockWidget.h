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
#include "tm_time.h"

// Public API
void ClockWidget_Loop();          // call from main loop (poller)
void ClockWidget_Invalidate();    // call when UI needs an immediate repaint (e.g., slice B state change)

// UI-provided hooks (implemented in Display_Routines.ino)
// Return true if Slice B is not in use (i.e., clock area is "free" to use)
bool UI_SliceB_Free();

// UI drawing hooks for time/date widgets (implemented in Display_Routines.ino)
void UI_Clock_Draw(const char* text);  // Draws time in the clock area, colors/font handled by UI
void UI_Clock_Draw_SS(const char* ss);
void UI_Date_Draw(const char* text);   // Draws date in the date area, colors/font handled by UI
void UI_Clock_Clear();            // clear full panel (time + date)
void UI_Clock_ClearTime();        // clear only time line area
void UI_Clock_ClearDate();        // clear only date line area
void UI_Clock_Draw_HH_MM_Colons(const char* hh, const char* mm);
void ClockWidget_UiReady();

// Config flags provided by the app (no widget-internal copies)
extern bool   CFG_ShowDateTime;
extern String CFG_TimeFormat;
extern String CFG_DateFormat;

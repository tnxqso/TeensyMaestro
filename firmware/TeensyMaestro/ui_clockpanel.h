#pragma once
#include <Arduino.h>
#include "ui_state.h"

// ui_clockpanel.h
// Pure UI for the clock panel (Slice-B time row + 6 info lines).
// This module only renders to TFT and holds no business logic.
// IMPORTANT: This UI is anchored to Slice-B's panel area. It does NOT
// compute positions using "mid-screen * index" offsets anymore.
// The underlying radio/model must update UIState; the UI never reads Flex state.

// ---- Layout descriptor injected from app code (no global externs required) ----
struct UIClockLayout {
  int freqX;       // left anchor of Slice-B panel (frequency/slice area)
  int freqY;       // top anchor  of Slice-B panel
  int freqWidth;   // width of Slice-B panel
  int midScreen;   // kept for backward compatibility; IGNORED by this module
};

// Provide layout to the UI module (must be called before drawing).
void ui_clockpanel_set_layout(const UIClockLayout& layout);

// ---- Time + date row API ----
void UI_Clock_Clear();
void UI_Clock_ClearTime();
void UI_Clock_ClearDate();
void UI_Clock_Draw(const char* text);
void UI_Clock_Draw_HH_MM_Colons(const char* hh, const char* mm);
void UI_Clock_Draw_SS(const char* ss);
void UI_Clock_ClearSecondsBox();
void UI_Clock_Draw_AMPM(const char* ap);
void UI_Date_Draw(const char* text);

// ---- Panel occupancy (Slice-B free?) ----
// Uses UIState only; the UI never reads the radio/model directly.
bool UI_SliceB_Free();

// ---- Info lines (1..6) ----
void UI_Info_DrawTimezone();                            // line #1 (yellow)
void UI_Info_DrawLine4_Temp(const char* text);          // line #4
void UI_Info_DrawLine5_RAM1(const char* label,
                            unsigned freeKB,
                            const char* suffix);        // line #5
void UI_Info_DrawLine6_ConfigColored(const char* text); // line #6

// ---- Short config status plumbing (stored in UI, read by widget) ----
void        UI_Info_SetConfigStatus(const char* text);
const char* UI_Info_GetConfigStatusText();

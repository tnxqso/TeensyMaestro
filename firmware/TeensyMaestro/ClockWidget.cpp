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

// ClockWidget.cpp
// Drives the Slice-B clock panel (time line + info lines) with a UI-ready latch.

#include "ClockWidget.h"
#include "ui_clockpanel.h"
#include "tm_time.h"     // TMTime_nowUTC(), TMTime_strftime(), TMTime_is12h(), TMTime_showSeconds(), TMTime_hasTime()
#include "tm_profile_select.h"   // ProfileSel::isVisible()
#include <Arduino.h>

extern bool MenuActive;   // true while any menu is shown
extern bool Splash;       // true while splash is visible

#ifdef __cplusplus
extern "C" {
  // Teensy 4.x on-chip temperature
  float tempmonGetTemp(void);

  // Linker symbols giving RAM1 (DTCM) region BSS..stack
  extern char _ebss;     // end of BSS (start of locals/stack area)
  extern char _estack;   // top of stack (not used here, but kept for reference)
}
#endif

// ===== Config (extern) =====
extern String CFG_DateFormat;     // validated date format
extern bool   CFG_ImperialUnits;  // false=°C, true=°F

// ===================== Internal state =====================
static bool    s_panelActive          = false;
static bool    s_invalidated          = true;
static bool    s_uiReady              = false;  // <- LATCH: nothing draws until true
static bool    s_infoDirty            = true;

static int32_t s_lastSecondBucket     = -1;
static int32_t s_lastMinuteBucket     = -1;
static int32_t s_lastDateKey          = -1;

static bool    s_tzDrawn              = false;  // line #1 drawn once per activation
static bool    s_cfgDrawn             = false;  // line #6 drawn when text is present
static int32_t s_lastInfoMinuteBucket = -1;     // throttle lines 4..6

// To avoid redraw when text is identical
static char s_lastL4[48] = "";
static char s_lastL5[48] = "";
static char s_lastL6[64] = "";

// Reset all caches/flags; does NOT draw by itself.
static inline void invalidate_all() {
  s_infoDirty = true;
  s_invalidated          = true;
  s_lastSecondBucket     = -1;
  s_lastMinuteBucket     = -1;
  s_lastDateKey          = -1;
  s_lastInfoMinuteBucket = -1;
  s_tzDrawn              = false;
  s_cfgDrawn             = false;
  s_lastL4[0] = s_lastL5[0] = s_lastL6[0] = '\0';
}

// ===================== Utilities =====================
static inline int32_t bucket_second(time_t utc_epoch) {
  return (int32_t)utc_epoch;
}
static inline int32_t bucket_minute(time_t utc_epoch) {
  return (int32_t)(utc_epoch / 60);
}
static int32_t local_day_key(time_t utc_epoch) {
  // YYYYMMDD in local tz using our formatter
  char ymd[16];
  TMTime_strftime("%Y%m%d", ymd, sizeof(ymd), utc_epoch);
  return atoi(ymd);
}

// RAM1 (locals/stack) free and used%
// “free” ≈ distance from BSS end to current stack pointer.
// For a meaningful “used %”, compare free to total RAM1 (512 KB on T4.1).
static void get_ram1_locals_free_and_usedpct(unsigned& freeKB, unsigned& usedPct) {
  // Compute “free for locals” bytes
  char sp_here;
  unsigned long freeB = 0;
  if ((uintptr_t)&sp_here > (uintptr_t)&_ebss) {
    freeB = (unsigned long)((uintptr_t)&sp_here - (uintptr_t)&_ebss);
  }
  // Total RAM1 (DTCM) on Teensy 4.1 is 512 KB.
  const unsigned long RAM1_TOTAL = 512UL * 1024UL;
  if (freeB > RAM1_TOTAL) freeB = 0; // safety

  // Convert to display units
  freeKB  = (unsigned)((freeB + 512UL) / 1024UL);
  usedPct = (unsigned)(((RAM1_TOTAL - freeB) * 100UL) / RAM1_TOTAL);
}

// ===================== Public API =====================
void ClockWidget_Init() {
  // Do NOT draw yet. Wait for UI-ready.
  s_panelActive = false;
  s_uiReady     = false;
  invalidate_all();
}

// Mark the entire clock panel for redraw (used on theme/config changes).
// Normally not needed in runtime – ClockWidget_Loop() handles incremental updates.
void ClockWidget_Invalidate() {
  s_panelActive = false;
  invalidate_all();  // clears all “valid” flags so next loop redraws everything
}

// Called by UI once the main background + both slices are fully drawn.
// After this point, the clock is allowed to render onto Slice B.
void ClockWidget_UiReady() {
  s_infoDirty  = true;
  s_uiReady    = true;
  s_panelActive = false; // force a clean “activation” path
  invalidate_all();
}

FLASHMEM void ClockWidget_Loop() {
  // Hard gate: do NOTHING until UI has finished the main frame once.
  if (!s_uiReady) return;

  // Suppress the clock when overlays are active (menu, splash, band/mode selector)
  if (MenuActive || Splash || ProfileSel::isVisible()) {
    if (s_panelActive) {
      s_panelActive = false;
      invalidate_all();  // clean slate when overlays go away
    }
    return;
  }

  const bool freeB = UI_SliceB_Free();
  if (!freeB) {
    if (s_panelActive) {
      s_panelActive = false;
      // don’t clear here; Slice B is in use by radio UI
      invalidate_all();
    }
    return;
  }

  // Panel just became active? Clear it once.
  if (!s_panelActive) {
    s_panelActive = true;
    UI_Clock_Clear();
    // keep invalidated=true so first pass draws everything
  }

  const bool   haveTime = TMTime_hasTime();
  const time_t utc      = TMTime_nowUTC();

  // --- Timezone label (line #1) — draw once per activation ---
  if (!s_tzDrawn || s_invalidated) {
    UI_Info_DrawTimezone();
    s_tzDrawn = true;
  }

  // --- Seconds lane (every second) ---
  {
    const int32_t sb = bucket_second(utc);
    if (sb != s_lastSecondBucket || s_invalidated) {
      s_lastSecondBucket = sb;

      if (TMTime_showSeconds()) {
        // 24h with seconds: draw SS
        if (haveTime) {
          char ss[4] = "00";
          TMTime_strftime("%S", ss, sizeof(ss), utc);
          UI_Clock_Draw_SS(ss);
        } else {
          UI_Clock_Draw_SS("--"); // placeholder while syncing
        }
      } else {
        // No seconds on the line: in 12h mode show AM/PM in the seconds box
        UI_Clock_ClearSecondsBox();
        if (TMTime_is12h() && haveTime) {
          char ap[3] = "AM";
          TMTime_strftime("%p", ap, sizeof(ap), utc);
          UI_Clock_Draw_AMPM(ap);
        }
      }
    }
  }

  // --- HH:MM (minute ticks) ---
  {
    const int32_t mb = bucket_minute(utc);
    if (mb != s_lastMinuteBucket || s_invalidated) {
      s_lastMinuteBucket = mb;

      if (haveTime) {
        char hh[4], mm[4];
        TMTime_strftime(TMTime_is12h() ? "%I" : "%H", hh, sizeof(hh), utc);
        TMTime_strftime("%M", mm, sizeof(mm), utc);
        UI_Clock_Draw_HH_MM_Colons(hh, mm);
      } else {
        UI_Clock_Draw("--:--:--");
      }
    }
  }

  // --- Date (draw only when day changes) ---
  {
    const int32_t dkey = local_day_key(utc);
    if (dkey != s_lastDateKey || s_invalidated) {
      s_lastDateKey = dkey;

      char datebuf[48];
      const char* fmt = (CFG_DateFormat.length() > 0) ? CFG_DateFormat.c_str()
                                                      : "%a %d %b %Y";
      TMTime_strftime(fmt, datebuf, sizeof(datebuf), utc);
      UI_Date_Draw(datebuf);
    }
  }

  // --- Lines 4–6 (once per minute; only when text actually changes) ---
  {
    const int32_t mb = bucket_minute(utc);
    if (mb != s_lastInfoMinuteBucket || s_invalidated) {
      s_lastInfoMinuteBucket = mb;

      // L4: temperature (string built here; UI colors only the value)
      char ln4[48];
      {
        float tC = tempmonGetTemp();
        if (CFG_ImperialUnits) {
          float tF = tC * 9.0f / 5.0f + 32.0f;
          snprintf(ln4, sizeof(ln4), "Temp: %.1f F", tF);
        } else {
          snprintf(ln4, sizeof(ln4), "Temp: %.1f C", tC);
        }
      }
      if (strcmp(ln4, s_lastL4) != 0) {
        UI_Info_DrawLine4_Temp(ln4);
        strncpy(s_lastL4, ln4, sizeof(s_lastL4));
        s_lastL4[sizeof(s_lastL4)-1] = '\0';
      }

      // L5: RAM1 free (kB). We send structured parts to the UI helper.
      {
        unsigned freeKB, usedPct;
        get_ram1_locals_free_and_usedpct(freeKB, usedPct);

        static unsigned s_lastFreeKB = 0xFFFFFFFFu;
        if ((freeKB != s_lastFreeKB) || s_infoDirty) {
          UI_Info_DrawLine5_RAM1("RAM1", freeKB, " kB free");
          s_lastFreeKB = freeKB;

          // also keep a human string in s_lastL5 so generic compare stays sane
          char ln5[48];
          snprintf(ln5, sizeof(ln5), "RAM1: %u kB free", freeKB);
          strncpy(s_lastL5, ln5, sizeof(s_lastL5));
          s_lastL5[sizeof(s_lastL5)-1] = '\0';
        }
      }

      // L6: Config-status (short)
      {
        const char* s = UI_Info_GetConfigStatusText(); // e.g. "OK" or "ERR line 42"
        if (s && *s && strcmp(s, s_lastL6) != 0) {
          UI_Info_DrawLine6_ConfigColored(s);
          strncpy(s_lastL6, s, sizeof(s_lastL6));
          s_lastL6[sizeof(s_lastL6)-1] = '\0';
          s_cfgDrawn = true;
        }
      }
    }
  }

  s_infoDirty   = false;
  s_invalidated = false;
}

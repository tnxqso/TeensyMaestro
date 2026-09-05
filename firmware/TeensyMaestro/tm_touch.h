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
#include "tm_touch_helpers.h"
#include "tm_qsy_select.h"

#ifndef TM_LONG_MS
#define TM_LONG_MS 700
#endif

// --- Forward decls (must match real defs) ---
extern unsigned long TouchTime;

void ResetScreenSaver(const char* reason = nullptr);
void ResetTFTScreen();

void MenuExit();
void SplashScreen();

extern bool MenuActive;
extern const int MaxMenuItems;
extern int  LastMenuItemIDX;
extern int  CWEncSteps;

extern bool Splash;
extern bool SplashM;

// Encoder write hook (decouple from CWMicEnc type)
void TM_MenuEncoderWrite(int value);

// Flex helpers
void SelectSlice(int Slice);
bool FlexIsHeadless();

// Acoustic confirm (defined in main .ino)
extern const byte STPin;
extern const int  BtnClickTone;
extern const int  BtnClickDur;

// ---------------------------------------------------------------------------
#ifndef DEBUG_TOUCH
  #define DEBUG_TOUCH 0
#endif

#ifndef TM_TOUCH_DRY_RUN
  #define TM_TOUCH_DRY_RUN 0
#endif

static inline bool TM_DryRunEnabled() {
#if TM_TOUCH_DRY_RUN
  return true;
#else
  return false;
#endif
}

#if DEBUG_TOUCH
  static bool TM_DebugLoggedPress = false;
#endif

// --- Geometry -------------------------------------------------------------
static constexpr uint16_t TM_BAR_H = 64;
static constexpr uint16_t TOP_BAR_H = TM_BAR_H;

static constexpr uint16_t COL1_END = (uint16_t)(TM_TOUCH_SCREEN_W * 0.20f);
static constexpr uint16_t COL2_END = (uint16_t)(TM_TOUCH_SCREEN_W * 0.514f);
static constexpr uint16_t COL3_END = (uint16_t)(TM_TOUCH_SCREEN_W * 0.771f);

static constexpr uint16_t BOTTOM_BAR_H = TM_BAR_H;
static constexpr uint16_t BOTTOM_Y_MIN = TM_TOUCH_SCREEN_H - BOTTOM_BAR_H;

static constexpr uint16_t TM_DEAD_ZONE = 20;

static constexpr uint16_t RESET_W  = 64;
static constexpr uint16_t RESET_H  = 64;
static constexpr uint16_t RESET_X0 = TM_TOUCH_SCREEN_W - RESET_W;
static constexpr uint16_t RESET_Y0 = TM_TOUCH_SCREEN_H - RESET_H;

static constexpr uint16_t MENU_ROW_H = 21;

// --- Actions / dedup ------------------------------------------------------
enum class TM_TouchAction : uint8_t {
  None = 0,
  SelectA,
  ToggleTxA,
  SelectB,
  ToggleTxB,
  ResetScreen,
  MenuExitAction,
  SplashShow,
  QsySelOpen,
  EncoderWrite,
  // bottom placeholders
  Bot1,
  Bot2,
  Bot3,
  Bot4,
  // long-press dedup keys
  Top0_Long,
  Top1_Long,
  Top2_Long,
  Top3_Long,
  Bottom0_Long,
  Bottom1_Long,
  Bottom2_Long,
  Bottom3_Long,
  ResetHardLong,

  CenterLongToggle,     // center long press: CW<->SSB toggle (no profiles)
  BottomLeftSplashHold, // bottom-left: hold to show Splash (no long press)

  // unified short labels for DRY RUN
  Top1_Short,
  Top2_Short,
  Top3_Short,
  Top4_Short,
  Bottom1_Short,
  Bottom2_Short,
  Bottom3_Short,
  Bottom4_Short,
  ResetShortLabel,
  // debug/control labels
  PressStartTop,
  PressStartCenter,
  PressStartBottom,
  PressStartReset,
  CenterShortBlocked
};

static TM_TouchAction sLastAction = TM_TouchAction::None;
static int            sLastMenuIdxLogged = -1;

// --- Per-press latches/state ---------------------------------------------
static int8_t sLatchedTopZone = -1;
static int8_t sLatchedBottomZone = -1;

enum class TM_PressKind : uint8_t { None, Top, Bottom, Reset, Center };
static TM_PressKind sPressKind = TM_PressKind::None;
static int8_t       sPressIdx  = -1;     // 0..3 for Top/Bottom, unused for Reset/Center

// Press lifecycle
static uint32_t sPressStartMs   = 0;
static bool     sLongFired      = false; // long fired in this press
static bool     sLongBeeped     = false; // acoustic confirm fired

static inline void TM_PressStart() {
  sPressStartMs = millis();
  sLongFired = false;
  sLongBeeped = false;
}

static inline bool TM_IsLongPress(uint32_t now_ms) {
  return (now_ms - sPressStartMs) >= TM_LONG_MS;
}

static inline void TM_OnLongThresholdReached() {
  if (!sLongBeeped) {
    if (STPin != 0xFF && BtnClickTone > 0 && BtnClickDur > 0) {
      tone((unsigned int)STPin, (unsigned int)BtnClickTone, (unsigned long)BtnClickDur);
    }
    sLongBeeped = true;
  }
}

static inline uint8_t TM_TopZoneIdx(uint16_t x) {
  if (x < COL1_END) return 0;
  if (x < COL2_END) return 1;
  if (x < COL3_END) return 2;
  return 3;
}

static inline uint8_t TM_BottomZoneIdx(uint16_t x) {
  if (x < COL1_END) return 0;
  if (x < COL2_END) return 1;
  if (x < COL3_END) return 2;
  return 3;
}

// Print only in DRY RUN or when DEBUG_TOUCH is enabled
#define TM_PRINT_ALLOWED() (TM_DryRunEnabled() || (DEBUG_TOUCH == 1))

// --- Safe println helpers to avoid nullptr overload ambiguity ------------
static inline void TM_PrintlnMaybe(const __FlashStringHelper* f) {
  if (TM_PRINT_ALLOWED() && f) Serial.println(f);
}
static inline void TM_PrintlnMaybe(const char* s) {
  if (TM_PRINT_ALLOWED() && s) Serial.println(s);
}
static inline void TM_PrintlnMaybe(std::nullptr_t) {
  // no-op
}

// NOTE: handle both const char* and const __FlashStringHelper* and nullptr
#define TM_LOG_ONCE(ACTID, CSTR)                         \
  do {                                                   \
    if ((ACTID) != sLastAction) {                        \
      TM_PrintlnMaybe(CSTR);                             \
      sLastAction = (ACTID);                             \
    }                                                    \
  } while (0)

#define TM_ACT(ACTID, MSG_NORMAL, MSG_DRY, ACTION_BLOCK) \
  do {                                                   \
    if (TM_DryRunEnabled()) {                            \
      TM_LOG_ONCE((ACTID), (MSG_DRY));                   \
    } else {                                             \
      if ((ACTID) != sLastAction) {                      \
        if (DEBUG_TOUCH == 1) TM_PrintlnMaybe(MSG_NORMAL); \
        ACTION_BLOCK                                     \
        sLastAction = (ACTID);                           \
      }                                                  \
    }                                                    \
  } while (0)

static inline void TM_ClearPressLatch() {
  sLatchedTopZone = -1;
  sLatchedBottomZone = -1;
  sPressKind = TM_PressKind::None;
  sPressIdx  = -1;
}

static inline void TM_ResetPressState() {
  sLastAction = TM_TouchAction::None;
  sLastMenuIdxLogged = -1;
  TM_ClearPressLatch();
#if DEBUG_TOUCH == 1
  TM_DebugLoggedPress = false;
#endif
}

/***************************** TouchDispatch ***************************/
FLASHMEM void TouchDispatch()
{
  // === Modal routing for QSY Selector (no special center-long anymore) ===
  if (QsySel::isVisible()) {
    while (touch.touched()) {
      TM_TouchPoint tp = TM_ReadTouchMapped();
      if (!tp.valid) continue;
      QsySel::onTouch(tp.x, tp.y, false);
    }
    TM_ResetPressState();
    return;
  }
  // ==========================================================================

  if (!GotTouch) {
    return;
  }

  ResetScreenSaver("Touch Dispatch");
  TouchTime = millis();

  bool pressBegan = false;
  bool bottomLeftSplashActive = false; // track "hold-to-show" splash state

  while (touch.touched()) {
    TM_TouchPoint tp = TM_ReadTouchMapped();
    if (!tp.valid) continue;

#if DEBUG_TOUCH == 1
    if (!TM_DebugLoggedPress) {
      Serial.print(F("px: "));   Serial.print(tp.x);
      Serial.print(F("  py: ")); Serial.print(tp.y);
      Serial.print(F("  z: "));  Serial.print(tp.z);
      Serial.println();
      TM_DebugLoggedPress = true;
    }
#endif

    // First valid sample: start-of-press bookkeeping & zone latching
    if (!pressBegan) {
      TM_PressStart();
      pressBegan = true;

      TM_ClearPressLatch();

      const bool inReset = (tp.x >= RESET_X0 && tp.y >= RESET_Y0);

      if (inReset) {
        sPressKind = TM_PressKind::Reset;
        TM_ACT(TM_TouchAction::PressStartReset,
               F(""), F("[DRY RUN] PRESS_START kind=Reset"), { });
      } else if (tp.y < TOP_BAR_H) {
        sPressKind = TM_PressKind::Top;
        sPressIdx  = (int8_t)TM_TopZoneIdx(tp.x);
        sLatchedTopZone = sPressIdx;
        TM_ACT(TM_TouchAction::PressStartTop,
               F(""), F("[DRY RUN] PRESS_START kind=Top"), { });
      } else if (tp.y >= BOTTOM_Y_MIN) {
        sPressKind = TM_PressKind::Bottom;
        sPressIdx  = (int8_t)TM_BottomZoneIdx(tp.x);
        sLatchedBottomZone = sPressIdx;
        TM_ACT(TM_TouchAction::PressStartBottom,
               F(""), F("[DRY RUN] PRESS_START kind=Bottom"), { });
      } else {
        sPressKind = TM_PressKind::Center; // center area
        TM_ACT(TM_TouchAction::PressStartCenter,
               F(""), F("[DRY RUN] PRESS_START kind=Center"), { });
      }
    }

    // ----------------------------- Dead zones -----------------------------
    if ((sPressKind != TM_PressKind::Reset) &&
        ((tp.y >= TOP_BAR_H && tp.y < (TOP_BAR_H + TM_DEAD_ZONE)) ||
         (tp.y >= (BOTTOM_Y_MIN - TM_DEAD_ZONE) && tp.y < BOTTOM_Y_MIN))) {
      delay(5);
      continue;
    }

    // --------------------- Bottom-left: HOLD-TO-SHOW Splash ----------------
    // Immediate (no long), show as long as finger stays down, then revert on release.
    if (sPressKind == TM_PressKind::Bottom && sPressIdx == 0) {
      if (!bottomLeftSplashActive) {
        // If menu is up, close it so Splash can render on a clean canvas
        if (MenuActive) {
          SplashM = false;
          MenuExit();
          MenuActive = false;
        }

        if (TM_DryRunEnabled()) {
          TM_LOG_ONCE(TM_TouchAction::BottomLeftSplashHold,
                      F("[DRY RUN] Bottom-left HOLD → Splash (press-held)"));
        } else {
          // Only call SplashScreen() if not already showing
          if (!Splash) {
            TM_ACT(TM_TouchAction::SplashShow,
                   F("SplashScreen()"),
                   F("[DRY RUN] Bottom-left HOLD → Splash (start)"),
                   {
                     SplashScreen();
                     Splash = true;
                   });
          }
        }
        bottomLeftSplashActive = true;
      }

      // Keep feeding Flex while holding
      if (fRig.connected && !TM_DryRunEnabled()) {
        fRig.fireEvents();
        fRig.process();
      }
      delay(10);
      continue; // stay in the loop until release
    }

    // -------------------- Center LONG → CW/SSB toggle (no profiles) --------
    if (sPressKind == TM_PressKind::Center && !sLongFired && TM_IsLongPress(millis())) {
      TM_OnLongThresholdReached();
      sLongFired = true;

      TM_ACT(TM_TouchAction::CenterLongToggle,
            F("CW/SSB toggle"),
            F("[DRY RUN] Center LONG → CW/SSB toggle"),
            {
              bool ok = fRig.toggleCW_SSB();  // uses band-check, no profiles involved
              if (!ok) {
                // Two-tone error beep (short, no UI spam)
                if (STPin != 0xFF) {
                  tone((unsigned int)STPin, 1200, 90);
                  delay(130);
                  tone((unsigned int)STPin, 700, 140);
                }
              }
            });

      // Drain until finger release
      while (touch.touched()) { delay(5); }
      break;
    }

    // ----------------------------- Menu active ----------------------------
    if (MenuActive) {
      for (int i = 1; i < MaxMenuItems; i++) {
        tft.drawRect(0, (i * MENU_ROW_H) - 3, TM_TOUCH_SCREEN_W, MENU_ROW_H, COLOR_BLACK);
      }
      const int MenuItemIDX = (int)(tp.y / MENU_ROW_H);

      if (MenuItemIDX == 0) {
        TM_ACT(TM_TouchAction::EncoderWrite,
               F("CWMicEnc.write(0)"),
               F("[DRY RUN] Menu item 0 SHORT"),
               { TM_MenuEncoderWrite(0); });
      }

      if (MenuItemIDX > 0 && MenuItemIDX <= LastMenuItemIDX) {
        tft.drawRect(0, (MenuItemIDX * MENU_ROW_H) - 3, TM_TOUCH_SCREEN_W, MENU_ROW_H, COLOR_YELLOW);

        TM_ACT(TM_TouchAction::EncoderWrite,
               F("CWMicEnc.write(idx * CWEncSteps)"),
               F("[DRY RUN] Menu item selected SHORT"),
               { TM_MenuEncoderWrite(MenuItemIDX * CWEncSteps); });

        if (MenuItemIDX != sLastMenuIdxLogged) {
          sLastMenuIdxLogged = MenuItemIDX;
        }
      }

      delay(20);
      continue;
    }

    // ------------------------ Long-press handling (Top/Bottom/Reset) -----
    if (!sLongFired && TM_IsLongPress(millis())) {
      TM_OnLongThresholdReached();
      sLongFired = true;

      if (sPressKind == TM_PressKind::Top) {
        switch (sPressIdx) {
          case 0: TM_LOG_ONCE(TM_TouchAction::Top0_Long,    F("[DRY RUN] Top zone 1 LONG")); break;
          case 1: TM_LOG_ONCE(TM_TouchAction::Top1_Long,    F("[DRY RUN] Top zone 2 LONG")); break;
          case 2: TM_LOG_ONCE(TM_TouchAction::Top2_Long,    F("[DRY RUN] Top zone 3 LONG")); break;
          default: TM_LOG_ONCE(TM_TouchAction::Top3_Long,   F("[DRY RUN] Top zone 4 LONG")); break;
        }
      }
      else if (sPressKind == TM_PressKind::Bottom) {
        // Bottom long placeholders retained (except bottom-left is handled above as hold-to-show)
        switch (sPressIdx) {
          case 0: TM_LOG_ONCE(TM_TouchAction::Bottom0_Long, F("[DRY RUN] Bottom zone 1 LONG (placeholder)")); break;
          case 1: TM_LOG_ONCE(TM_TouchAction::Bottom1_Long, F("[DRY RUN] Bottom zone 2 LONG (placeholder)")); break;
          case 2: TM_LOG_ONCE(TM_TouchAction::Bottom2_Long, F("[DRY RUN] Bottom zone 3 LONG (placeholder)")); break;
          default: TM_LOG_ONCE(TM_TouchAction::Bottom3_Long,F("[DRY RUN] Bottom zone 4 LONG (placeholder)")); break;
        }
      }
      else if (sPressKind == TM_PressKind::Reset) {
        if (TM_DryRunEnabled()) {
          TM_LOG_ONCE(TM_TouchAction::ResetHardLong, F("[DRY RUN] Reset LONG (hard reset)"));
        } else {
#if DEBUG_TOUCH == 1
          Serial.println(F("Hard resetting (long-press)"));
#endif
          __disable_irq();
          SCB_AIRCR = 0x05FA0004; // SYSRESETREQ
          while (true) { /* wait for reset */ }
        }
      }
    }

    // Keep Flex pumping
    if (fRig.connected && !TM_DryRunEnabled()) {
      fRig.fireEvents();
      fRig.process();
    }
  } // while touched()

  // ------------------------- Release: fire SHORT if long not fired -------
  if (pressBegan && !sLongFired) {
    switch (sPressKind) {
      case TM_PressKind::Top:
        if (TM_DryRunEnabled()) {
          switch (sPressIdx) {
            case 0: TM_LOG_ONCE(TM_TouchAction::Top1_Short, F("[DRY RUN] Top zone 1 SHORT")); break;
            case 1: TM_LOG_ONCE(TM_TouchAction::Top2_Short, F("[DRY RUN] Top zone 2 SHORT")); break;
            case 2: TM_LOG_ONCE(TM_TouchAction::Top3_Short, F("[DRY RUN] Top zone 3 SHORT")); break;
            default: TM_LOG_ONCE(TM_TouchAction::Top4_Short, F("[DRY RUN] Top zone 4 SHORT")); break;
          }
        } else {
          switch (sPressIdx) {
            case 0: // Select A
              TM_ACT(TM_TouchAction::SelectA, F("Select slice A"), nullptr, { SelectSlice(0); });
              break;
            case 1: // Toggle TX A
              TM_ACT(TM_TouchAction::ToggleTxA, F("Select TX A"), nullptr, {
                if (fRig.slice[A].in_use == 1) {
                  const int newTx = (fRig.slice[A].tx != 1) ? 1 : 0;
                  fRig.setTx(0, newTx);
                }
              });
              break;
            case 2: // Split
              if (fRig.slice[A].in_use == 1 &&
                  fRig.slice[B].in_use != 1 &&
                  TMU_IsValidPanHandle((uint32_t)fRig.slice[A].pan)) {
                TM_ACT(TM_TouchAction::SelectB, F("split()"), nullptr, { (void)fRig.split(); });
              }
              break;
            default: // Toggle TX B
              TM_ACT(TM_TouchAction::ToggleTxB, F("Select TX B"), nullptr, {
                if (fRig.slice[B].in_use == 1) {
                  const int newTx = (fRig.slice[B].tx != 1) ? 1 : 0;
                  fRig.setTx(1, newTx);
                }
              });
              break;
          }
        }
        break;

      case TM_PressKind::Center: {
        const bool allowCenterShort = (fRig.connected) && (!FlexIsHeadless());
        if (allowCenterShort) {
          TM_ACT(TM_TouchAction::QsySelOpen,
                F("QsySel::open()"),
                F("[DRY RUN] Center area SHORT (allowed)"),
                { QsySel::open(); });
        } else {
          TM_ACT(TM_TouchAction::CenterShortBlocked,
                F(""),
                F("[DRY RUN] Center area SHORT (blocked by connected/headless gate)"),
                { /* no-op */ });
        }
        break;
      }

      case TM_PressKind::Bottom:
        // Bottom-left short should NOT do anything special; Splash is tied to HOLD only.
        // So on release, if we were showing Splash via bottom-left, turn it off cleanly.
        if (sPressIdx == 0) {
          if (TM_DryRunEnabled()) {
            TM_LOG_ONCE(TM_TouchAction::BottomLeftSplashHold,
                        F("[DRY RUN] Bottom-left RELEASE → Splash off"));
          } else {
            if (Splash) {
              Splash = false;
              MenuExit();   // return to normal UI
            }
          }
          // Do not fire any placeholder action for bottom-left
          break;
        }

        if (TM_DryRunEnabled()) {
          switch (sPressIdx) {
            case 1: TM_LOG_ONCE(TM_TouchAction::Bottom2_Short, F("[DRY RUN] Bottom zone 2 SHORT (placeholder)")); break;
            case 2: TM_LOG_ONCE(TM_TouchAction::Bottom3_Short, F("[DRY RUN] Bottom zone 3 SHORT (placeholder)")); break;
            default: TM_LOG_ONCE(TM_TouchAction::Bottom4_Short, F("[DRY RUN] Bottom zone 4 SHORT (placeholder)")); break;
          }
        } else {
          switch (sPressIdx) {
            case 1: TM_ACT(TM_TouchAction::Bot2, F("Bottom zone 2"), nullptr, { /* no-op */ }); break;
            case 2: TM_ACT(TM_TouchAction::Bot3, F("Bottom zone 3"), nullptr, { /* no-op */ }); break;
            default: TM_ACT(TM_TouchAction::Bot4, F("Bottom zone 4"), nullptr, { /* no-op */ }); break;
          }
        }
        break;

      case TM_PressKind::Reset:
        if (TM_DryRunEnabled()) {
          TM_LOG_ONCE(TM_TouchAction::ResetShortLabel, F("[DRY RUN] Reset SHORT (screen refresh)"));
        } else {
#if DEBUG_TOUCH == 1
          Serial.println(F("Resetting screen"));
#endif
          SplashM = false;
          ResetTFTScreen();
          delay(100);
#if DEBUG_TOUCH == 1
          Serial.println(F("Screen reset complete"));
#endif
        }
        break;

      default:
        break;
    }
  }

  // No special “center long” splash revert anymore.

  // Clear per-press state
  TM_ResetPressState();
}

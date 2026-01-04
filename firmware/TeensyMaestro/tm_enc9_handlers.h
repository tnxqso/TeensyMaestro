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

/********************* tm_enc9_handlers.h (header-only, step 1) ******************
 * What this is:
 *  - A header-only set of small handlers plus ReadCWMicEnc(), extracted from the
 *    monolithic implementation to reduce cognitive load while preserving behavior.
 *  - No new features; semantics are 1:1 with the original code path.
 *
 * How to use:
 *  - Include this header in your main .ino AFTER all referenced globals are declared
 *    (e.g., CWMicEnc, CWEncSteps, fRig, tft, color constants, menus) and BEFORE the
 *    first call site (typically before loop()).
 *
 * Notes:
 *  - Comments are in English per project convention.
 *  - Header-only keeps integration simple in Arduino builds. A .cpp/.h split can be
 *    done later if desired for faster rebuilds and cleaner symbol management.
 *******************************************************************************/

#include "tm_project.hpp"
#include "tm_enc_helpers.hpp"
#include <EEPROM.h>
#include "tm_system_utils.h"

// [SERNUM-HELPERS] --- EEPROM commit helpers for contest serial number
// NOTE: EEPROM.get() is called once in Setup via GetEEPROM().
// We only write when user commits in menu, or when a macro increments the number.

// Address is assumed to be defined elsewhere (as used by GetEEPROM()).
extern const int EEAddr;

// Global serial number variables expected to exist:
//   extern volatile int SerNum;      // current working value
//   extern volatile int SerNumSave;  // UI working copy for menu editing

// Save immediately to EEPROM (used on explicit commit).
static inline void TM_SerNum_SaveEEPROM()
{
  EEPROM.put(EEAddr, SerNum);
  // Optional: tiny guard to avoid immediately re-reading stale value elsewhere
  // (Teensy EEPROM emulation does not need commit())
  delay(2);
}

// Throttled saver to avoid excessive flash wear when macros bump SerNum rapidly.
static inline void TM_SerNum_SaveEEPROM_Throttled(uint16_t min_interval_ms = 750)
{
  static uint32_t s_lastWriteMs = 0;
  const uint32_t now = millis();
  if (now - s_lastWriteMs >= min_interval_ms) {
    EEPROM.put(EEAddr, SerNum);
    s_lastWriteMs = now;
  }
}

// ---------------- Case handlers (Encoder_9 in normal mode) --------------------

static void HandleEnc9_CWSpeed() {
  const int raw    = enc_read_quantized(CWMicEnc, CWEncSteps);
  const int newWpm = TMU_ClampWpm(raw);
  
  // Exit immediately if the value hasn't changed from the last processed state
  if (newWpm == CWValSave) return;

  ResetScreenSaver("Enc9: CW/Mic/RF");

  // CRITICAL FIX: Manually update CWValSave here.
  // We cannot rely on Keyer_Apply_Wpm() to do it, because if it detects 
  // an "echo" from the radio, it skips updating CWValSave.
  // If we don't update it here, 'newWpm == CWValSave' above remains false,
  // causing an infinite loop of repeated commands.
  CWValSave = newWpm;

  enc_write_quantized(CWMicEnc, newWpm, CWEncSteps);
  Keyer_Apply_Wpm(newWpm, /*preserveBaseline=*/false);

  if (fRig.connected) {
    const bool headless = FlexIsHeadless();
    String mode;
    const bool haveMode = TMU_TxModeKnown(mode);
    const bool isCw     = TMU_TxIsCw(); // Uncommented: Vital to prevent SSB storms
    
    // LOGIC: Only sync to radio if:
    // 1. Not Headless (PC is connected, per your design requirement)
    // 2. We know the mode.
    // 3. The mode is actually CW (Prevents sending CW commands in SSB, which caused the crash)
    if (!headless && haveMode && isCw) {
      
      // TRAFFIC GUARD: Only send command if the radio value actually differs.
      // This prevents feedback loops where the radio confirms a value, 
      // and we immediately send it back, creating a network storm.
      if (fRig.transmit.speed != newWpm) {
        fRig.setCwSpeed(newWpm);
      }
    }
  }
}

static void HandleEnc9_MicGain() {
  int v = enc_read_quantized(CWMicEnc, CWEncSteps);
  if (v == MicGainSave) return;

  v = clamp_and_sync_encoder(CWMicEnc, v, 0, 100, CWEncSteps);
  ResetScreenSaver("Enc9: MicGain");
  if (fRig.connected) {
    fRig.setMicLevel(v);
    debugln("mic gain " + String(v));
  }

  MicGain = MicGainSave = v;
  DispMicGain();
}

static void HandleEnc9_RFPower() {
  int v = enc_read_quantized(CWMicEnc, CWEncSteps);
  if (v == RFPowerSave) return;

  v = clamp_and_sync_encoder(CWMicEnc, v, 0, 100, CWEncSteps);
  ResetScreenSaver("Enc9: RFPower");
  if (fRig.connected) {
    fRig.setRfPower(v);
    debugln("RF Power " + String(v));
  }

  RFPower = RFPowerSave = v;
  DispRF();
}

static void HandleEnc9_TunePower() {
  int v = enc_read_quantized(CWMicEnc, CWEncSteps);
  if (v == TunePowerSave) return;

  v = clamp_and_sync_encoder(CWMicEnc, v, 0, 100, CWEncSteps);
  ResetScreenSaver("Enc9: TunePower");
  if (fRig.connected) {
    fRig.setTunePower(v);
    debugln("Tune Power " + String(v));
  }

  TunePower = TunePowerSave = v;
  DispRF();
}

static void HandleEnc9_WNBLevel() {
  int v = enc_read_quantized(CWMicEnc, CWEncSteps);
  if (v == WNBLevelSave) return;

  v = clamp_and_sync_encoder(CWMicEnc, v, 0, 100, CWEncSteps);
  ResetScreenSaver("Enc9: WNBLevel");
  if (fRig.connected) {
    if (fRig.slice[A].in_use)      fRig.setWnbLevel(A, v);
    else if (fRig.slice[B].in_use) fRig.setWnbLevel(B, v);
  }

  debugln("WNB Level " + String(v));
  WNBLevel = WNBLevelSave = v;
  DispWNB();
}

static void HandleEnc9_MonLevel() {
  int v = enc_read_quantized(CWMicEnc, CWEncSteps);
  if (v == MonLevelSave) return;

  v = clamp_and_sync_encoder(CWMicEnc, v, 0, 100, CWEncSteps);
  ResetScreenSaver("Enc9: MonLevel");
  if (fRig.connected) {
    fRig.setSbMonitorGain(v);
    debugln("Tune Power " + String(v));
  }

  MonLevel = MonLevelSave = v;
  DispMonLevel();
}

static void HandleEnc9_VOXLevel() {
  int v = enc_read_quantized(CWMicEnc, CWEncSteps);
  if (v == VOXLevelSave) return;

  v = clamp_and_sync_encoder(CWMicEnc, v, 0, 100, CWEncSteps);
  ResetScreenSaver("Enc9: VOXLevel");
  if (fRig.connected) {
    fRig.setVoxLevel(v);
    debugln("VOX Level " + String(v));
  }

  VOXLevel = VOXLevelSave = v;
  DispVOXLevel();
}

static void HandleEnc9_VOXDelay() {
  int v = enc_read_quantized(CWMicEnc, CWEncSteps);
  if (v == VOXDelaySave) return;

  v = clamp_and_sync_encoder(CWMicEnc, v, 0, 100, CWEncSteps);
  ResetScreenSaver("Enc9: VOXDelay");
  if (fRig.connected) {
    fRig.setVoxDelay(v);
    debugln("VOX Delay " + String(v));
  }

  VOXDelay = VOXDelaySave = v;
  DispVOXDelay();
}

static void HandleEnc9_Band() {
  int v = enc_read_quantized(CWMicEnc, CWEncSteps);

  if (v < 0) {
    v = 0;
    enc_write_quantized(CWMicEnc, 0, CWEncSteps);
  } else if (v > 12) {
    v = 12;
    enc_write_quantized(CWMicEnc, 12, CWEncSteps);
  }
  if (v == BandSave) return;

  ResetScreenSaver("Enc9: Band");
  
  ActivePan = fRig.slice[SliceActiveVal].pan;
  debug("SliceActiveVal: "); debugln(SliceActiveVal);
  debug("ActivePan: ");      debugln(ActivePan);
  debug("Band: ");           debugln(BandMenu[v]);
  debug("BandSave: ");       debugln(BandMenu[BandSave]);

  if (ActivePan > 0) {
    // Original offset of 40 MHz is preserved - What?
    int idx = TMU_HandleToPanIndexSafe((uint32_t)ActivePan, TMU_ArrayLen(fRig.panadapter));
    if (idx >= 0) fRig.setBand(idx, BandMenu[v]);
  }

  Band = BandSave = v;
  // Preserve original UI call (even if naming looks odd)
  DispCWSpeed();
}

// ------------------------- Menu handlers (MenuActive) -------------------------

static bool HandleMenu_STFreq() {
  if (!STFreqMenuActive) return false;

  int val = enc_read_quantized(CWMicEnc, CWEncSteps) * 10;
  if (val == static_cast<int>(STFreqSave)) return true;

  if (val < 100) {
    val = 100;
    CWMicEnc.write((val * CWEncSteps) / 10);
  } else if (val > 6000) {
    val = 6000;
    CWMicEnc.write((val * CWEncSteps) / 10);
  }

  STFreq = STFreqSave = val;
  MenuItem[CWMenu][4] = "CW Sidetone Freq: " + String(STFreq);
  DispSTFreq();
  return true;
}

static bool HandleMenu_Accel() {
  if (!AccelMenuActive) return false;

  int val = enc_read_quantized(CWMicEnc, CWEncSteps) * 100;
  if (val == AccelFactorSave) return true;

  if (val < 100) {
    val = 100;
    CWMicEnc.write((val * CWEncSteps) / 100);
  } else if (val > 20000) {
    val = 20000;
    CWMicEnc.write((val * CWEncSteps) / 100);
  }

  AccelFactor = AccelFactorSave = val;
  MenuItem[MiscMenu][13] = "VFO Acceleration Factor: " + String(AccelFactor);
  DispAccelFactor();
  return true;
}

// [SERNUM-MENU-EDIT] -- called while the "Set contest serial number" menu is active
static bool HandleMenu_SerNum() {
  if (!SerNumMenuActive) return false;

  int val = enc_read_quantized(CWMicEnc, CWEncSteps);
  if (val == SerNumSave) return true;

  if (val < 1) val = 1;

  // Update the UI working copy and the live value so DispSerNum shows it
  SerNum = SerNumSave = val;
  MenuItem[CWMenu][5] = "Set contest serial number: " + String(SerNum);

  // Redraw the live preview in menu/home depending on your current UI logic
  DispSerNum();
  return true;
}

static bool HandleMenu_RFPower() {
  if (!RFPowerMenuActive) return false;

  int v = enc_read_quantized(CWMicEnc, CWEncSteps);
  if (v == RFPowerSave) return true;

  if (v < 0) {
    v = 0;
    enc_write_quantized(CWMicEnc, 0, CWEncSteps);
  } else if (v > 100) {
    v = 100;
    enc_write_quantized(CWMicEnc, 100, CWEncSteps);
  }

  RFPower = RFPowerSave = v;
  MenuItem[TransmitMenu][0] = "RF Power: " + String(RFPower);
  DispRF();
  return true;
}

static bool HandleMenu_MicGain() {
  if (!MicGainMenuActive) return false;

  int v = enc_read_quantized(CWMicEnc, CWEncSteps);
  if (v == MicGainSave) return true;

  if (v < 0) {
    v = 0;
    enc_write_quantized(CWMicEnc, 0, CWEncSteps);
  } else if (v > 100) {
    v = 100;
    enc_write_quantized(CWMicEnc, 100, CWEncSteps);
  }

  MicGain = MicGainSave = v;
  MenuItem[TransmitMenu][1] = "Mic Gain: " + String(MicGain);
  DispMicGain();
  return true;
}

static bool HandleMenu_Navigation() {
  const int current = enc_read_quantized(CWMicEnc, CWEncSteps);
  if (MenuItemIDX == current) return false;

  MenuItemIDX = current;

  if (MenuItemIDX < 0) {
    MenuItemIDX = LastMenuItemIDX;
    enc_write_quantized(CWMicEnc, MenuItemIDX, CWEncSteps);
  } else if (MenuItemIDX > LastMenuItemIDX) {
    MenuItemIDX = 0;
    enc_write_quantized(CWMicEnc, MenuItemIDX, CWEncSteps);
  }

  // Clear all rectangles first
  for (int i = 1; i <= LastMenuItemIDX; i++) {
    tft.drawRect(0, (i * 21) - 3, 480, 21, COLOR_BLACK);
  }

  // Highlight the selected one (except 0)
  if (MenuItemIDX != 0) {
    tft.drawRect(0, (MenuItemIDX * 21) - 3, 480, 21, COLOR_YELLOW);
    debugln(MenuItemIDX);
  }
  return true;
}

// ------------------------------ Public API ------------------------------------

/***************************** ReadCWMicEnc *************************************
 * Structured entry point that delegates to small, testable handlers.
 * Behavior is 1:1 with the original implementation.
 *******************************************************************************/
static inline void ReadCWMicEnc()
{
  if (!MenuActive)
  {
    // Only act on quantized steps
    if ((CWMicEnc.read() % CWEncSteps) != 0) {
      return;
    }

    switch (Encoder_9)
    {
      case Enc9_CWSpeed:    HandleEnc9_CWSpeed();   break;
      case Enc9_MicGain:    HandleEnc9_MicGain();   break;
      case Enc9_RFPower:    HandleEnc9_RFPower();   break;
      case Enc9_TunePower:  HandleEnc9_TunePower(); break;
      case Enc9_WNBLevel:   HandleEnc9_WNBLevel();  break;
      case Enc9_MonLevel:   HandleEnc9_MonLevel();  break;
      case Enc9_VOXLevel:   HandleEnc9_VOXLevel();  break;
      case Enc9_VOXDelay:   HandleEnc9_VOXDelay();  break;
      case Enc9_Band:       HandleEnc9_Band();      break;
      default: /* no-op */  break;
    }
    return;
  }

  // MenuActive path: return as soon as a matching handler performs an update.
  if (HandleMenu_STFreq())     return;
  if (HandleMenu_Accel())      return;
  if (HandleMenu_SerNum())     return;
  if (HandleMenu_RFPower())    return;
  if (HandleMenu_MicGain())    return;
  if (HandleMenu_Navigation()) return;

  // If nothing matched, fall through (no-op) to keep existing semantics.
}

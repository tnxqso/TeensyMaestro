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

#include <Arduino.h>
#include "tm_system_utils.h"
#include "tm_config.h"
#include <FlexRigTeensy.h>   // brings FlexRig definition
#include <MTP_Teensy.h>
#include "tm_wk_bridge.h"

#if DEBUG_WPM
// Track one-time and change logging
static bool s_prevHaveMode = false;
#endif

// --- Externs provided by the sketch ---
extern String  MyCall;
extern uint8_t mac[6];
extern String  addr;
extern bool    InSetup;

extern bool    gFixedEndpointUsed;
extern char    gFixedEndpointLabel[64];

extern int ClientMenuItem;
extern int TXSlice;
extern int  CWVal;  // needed for unchanged check
extern void Keyer_Apply_Wpm(int newWpm, bool preserveBaseline);
extern volatile bool g_KeyerTimingActive;

// Provided in main .ino (DMAMEM FlexRig fRig;)
extern FlexRig fRig;

// Headless state helper (declared elsewhere in the sketch)
bool FlexIsHeadless();

// --- WPM adoption gate state ---
static bool s_wpmAdoptedFromRadio = false;

bool TMU_HasAdoptedRadioWpm() {
  return s_wpmAdoptedFromRadio;
}

void TMU_MarkAdoptedRadioWpm() {
  s_wpmAdoptedFromRadio = true;
}

// -----------------------------------------------------------------------------
// Boot info snapshot (unchanged)
// -----------------------------------------------------------------------------
FLASHMEM BootInfo BuildBootInfo() {
  BootInfo bi{};

  // --- Call / version ---
  bi.myCall   = MyCall;
  bi.version  = String(F(TM_VERSION));
#if DEBUG == 1
  bi.isDebugBuild = true;
#else
  bi.isDebugBuild = false;
#endif

  // --- Teensy MAC / IP ---
  for (int i = 0; i < 6; ++i) bi.mac[i] = mac[i];
  bi.ipStr = addr;

  // --- Flex rig fields ---
  bi.inSetup      = InSetup;
  bi.rigConnected = fRig.connected;

  bi.fixedEndpointUsed  = gFixedEndpointUsed;
  bi.fixedEndpointLabel = String(gFixedEndpointLabel);

  bi.rigIp[0] = fRig.ipAddress[0];
  bi.rigIp[1] = fRig.ipAddress[1];
  bi.rigIp[2] = fRig.ipAddress[2];
  bi.rigIp[3] = fRig.ipAddress[3];

  bi.rigNick    = fRig.nickName;
  bi.rigSerial  = fRig.serial;
  bi.rigModel   = fRig.modelName;
  bi.rigVersion = fRig.softVersion;

  // Client (only shown when not in setup)
  bi.clientStation = fRig.Client_Station[ClientMenuItem];
  bi.clientProgram = fRig.Client_Program[ClientMenuItem];

  return bi;
}

#include <SD.h>

// -----------------------------------------------------------------------------
// SD card initialization glue
// -----------------------------------------------------------------------------

static bool g_sd_initialized = false;

FLASHMEM bool TM_SD_Ensure()
{
  if (g_sd_initialized) {
    return true;
  }

  // Use built-in SD on Teensy 4.1
  if (!SD.begin(BUILTIN_SDCARD)) {
    return false;
  }

  g_sd_initialized = true;
  return true;
}

// -----------------------------------------------------------------------------
// CW / WPM helpers (implementations)
// -----------------------------------------------------------------------------

bool TMU_TxModeKnown(String& modeOut) {
  if (TXSlice < 0) { modeOut = ""; return false; }
  modeOut = fRig.slice[TXSlice].mode;
  return modeOut.length() > 0;
}

bool TMU_TxIsCw() {
  if (TXSlice < 0) return false;
  const String m = fRig.slice[TXSlice].mode;
  return m == "CW";
}

int TMU_ClampWpm(int wpm) {
  if (wpm < 5)  return 5;
  if (wpm > 60) return 60;   // keep consistent with encoder/UI range
  return wpm;
}

void TMU_LogCwSyncSnapshot(const char* tag) {
#if DEBUG_WPM
  String mode;
  const bool haveMode = TMU_TxModeKnown(mode);
  const bool headless = FlexIsHeadless();
  const int  cwxWpm   = fRig.cwx.wpm;
  const int  txWpm    = fRig.transmit.speed;

  // Log exactly once when TX mode becomes known after previously unknown
  if (!s_prevHaveMode && haveMode) {
    DLOG_WPM("[WPM SNAP] %s headless=%s txSlice=%d haveMode=%s mode=%s cwx.wpm=%d tx.speed=%d\n",
             (tag ? tag : "snap"),
             headless ? "YES" : "NO",
             TXSlice,
             haveMode ? "YES" : "NO",
             mode.length() ? mode.c_str() : "(empty)",
             cwxWpm,
             txWpm);
  }
  s_prevHaveMode = haveMode;
#else
  (void)tag;
#endif
}

// Single source of truth for radio WPM
int TMU_GetReportedCwWpm() {
  // If not connected, we cannot fetch from radio → return current local CWVal.
  if (!fRig.connected) {
  #if DEBUG_WPM && DEBUG_WPM_GETTER_VERBOSE
    DLOG_WPM("[WPM] rig not connected → returning CWVal=%d\n", CWVal);
  #endif
    return TMU_ClampWpm(CWVal);
  }

  if (fRig.cwx.wpm > 0) {
    const int w = TMU_ClampWpm(fRig.cwx.wpm);
  #if DEBUG_WPM && DEBUG_WPM_GETTER_VERBOSE
    DLOG_WPM("[WPM] CWX.wpm=%d → %d\n", fRig.cwx.wpm, w);
  #endif
    return w;
  }

  String mode;
  if (TMU_TxModeKnown(mode) && TMU_TxIsCw() && fRig.transmit.speed > 0) {
    const int w = TMU_ClampWpm(fRig.transmit.speed);
  #if DEBUG_WPM && DEBUG_WPM_GETTER_VERBOSE
    DLOG_WPM("[WPM] Fallback tx.speed=%d → %d (mode=%s)\n",
             fRig.transmit.speed, w, mode.c_str());
  #endif
    return w;
  }

#if DEBUG_WPM && DEBUG_WPM_GETTER_VERBOSE
  DLOG_WPM("[WPM] no valid radio value → returning CWVal=%d\n", CWVal);
#endif
  return TMU_ClampWpm(CWVal);
}

bool TMU_AdoptCwWpmIfValid(bool onlyWhenCwMode,
                           bool preserveBaseline,
                           int* outAppliedWpm,
                           const char* tag)
{
  if (outAppliedWpm) *outAppliedWpm = -1;

  TMU_LogCwSyncSnapshot(tag ? tag : "AdoptWpm");

  if (!fRig.connected) {
    DLOG_WPM("[WPM] skip adopt: rig not connected\n");
    return false;
  }

  // Respect onlyWhenCwMode if requested.
  if (onlyWhenCwMode && !TMU_TxIsCw()) {
    String mode; TMU_TxModeKnown(mode);
    DLOG_WPM("[WPM] skip adopt: onlyWhenCwMode && mode=%s\n",
             mode.length() ? mode.c_str() : "(unknown)");
    return false;
  }

  const int chosen = TMU_GetReportedCwWpm();
  if (chosen == CWVal) {
  #if !DEBUG_WPM_CHANGES_ONLY
    DLOG_WPM("[WPM] adopt: unchanged (%d) → no-op\n", chosen);
  #endif
    return false;
  }

  Keyer_Apply_Wpm(chosen, preserveBaseline);
  if (outAppliedWpm) *outAppliedWpm = chosen;
  DLOG_WPM("[WPM] adopt: applied=%d preserveBaseline=%d\n",
           chosen, preserveBaseline ? 1 : 0);
  return true;
}

bool TMU_SyncCwWpm(bool preserveBaseline,
                   int* outAppliedWpm,
                   const char* tag)
{
  if (outAppliedWpm) *outAppliedWpm = -1;

  if (!fRig.connected) {
    DLOG_WPM("[WPM] sync skip: rig not connected\n");
    return false;
  }

  // Optional snapshot for debugging (one-time when mode becomes known)
  TMU_LogCwSyncSnapshot(tag ? tag : "sync");

  // Single source of truth for fetching radio WPM.
  const int reported = TMU_GetReportedCwWpm();

  // Debounce: nothing to do if no change.
  if (reported == CWVal) {
  #if !DEBUG_WPM_CHANGES_ONLY
    DLOG_WPM("[WPM] sync unchanged (%d) → no-op\n", reported);
  #endif
    return false;
  }

  // Apply to keyer (updates CWVal/WPM/ElementLen/enc+display)
  Keyer_Apply_Wpm(reported, preserveBaseline);
  if (outAppliedWpm) *outAppliedWpm = reported;

  DLOG_WPM("[WPM] sync applied=%d preserveBaseline=%d\n",
           reported, preserveBaseline ? 1 : 0);
  return true;
}

// Provided by the main UI unit:
extern void RefreshScreen();

// Provided by the UI to tell if a refresh is in progress:
extern volatile bool sInRefreshScreen;

static volatile bool     s_uiRefreshPending = false;
static volatile uint32_t s_uiRefreshDueMs   = 0;

void TM_Service(const TM_ServiceOptions& opt)
{
  // 1) Radio I/O: ingest TCP and dispatch parsed events
  if (opt.pumpRig && fRig.connected) {
    fRig.process();     // read incoming TCP data
    fRig.fireEvents();  // dispatch updates to all modules
  }

  // 2) Optional background subsystems
  if (!opt.timingCritical) {
    if (opt.pumpWK)   TM_WK_Bridge::poll();
     if (opt.pumpMtpd) MTP.loop();
  }

  // 3) Yield to the scheduler only when it is safe to do so
  if (opt.allowSleep && !opt.timingCritical && !g_KeyerTimingActive) {
    delay(1);
  }
  
}

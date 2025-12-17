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
#include "ui_boot.h"

// -----------------------------------------------------------------------------
// Compile-time debug control for WPM logging
// -----------------------------------------------------------------------------
#ifndef DEBUG_WPM
#define DEBUG_WPM 0
#endif

#if DEBUG_WPM
  #define DLOG_WPM(...)  do { Serial.printf(__VA_ARGS__); } while (0)
#else
  #define DLOG_WPM(...)  do { } while (0)
#endif

// When set to 1, we only log changes (suppress "unchanged" spam)
#ifndef DEBUG_WPM_CHANGES_ONLY
#define DEBUG_WPM_CHANGES_ONLY 1
#endif

// Flex pan handles are 0x40000000 + idx (idx >= 0). We keep helpers here to avoid a new header.
static constexpr uint32_t TMU_PAN_BASE = 0x40000000u;

// Validate a true Flex handle; disallow legacy 0/1 and sentinel 0xFFFFFFFF.
static inline bool TMU_IsValidPanHandle(uint32_t h) {
  return (h >= TMU_PAN_BASE) && (h != 0xFFFFFFFFu);
}

// Safe mapping: handle -> index (>=0) or -1 if invalid/out-of-range.
// panCount = length of your fRig.panadapter array.
static inline int TMU_HandleToPanIndexSafe(uint32_t h, size_t panCount) {
  if (!TMU_IsValidPanHandle(h)) return -1;
  const uint32_t idx = h - TMU_PAN_BASE;
  return (idx < panCount) ? static_cast<int>(idx) : -1;
}

// Tiny constexpr helper to get array length without heavy templates.
template <typename T, size_t N>
constexpr size_t TMU_ArrayLen(const T (&)[N]) { return N; }

// Optional: map handle -> debug index for prints (never use for logic).
static inline uint32_t TMU_PanToDebugIndex(uint32_t h) {
  if (!TMU_IsValidPanHandle(h)) return 0xFFFFFFFFu;
  return (h - TMU_PAN_BASE);
}

// Optional: compact debug logger for pan handles.
static inline void TMU_LogPan(const char* label, uint32_t pan) {
  Serial.print(label ? label : "pan");
  Serial.print("=");
  // Hex print without heap: mimic "0xhhhhhhhh"
  Serial.print("0x");
  for (int i = 7; i >= 0; --i) {
    const uint8_t nib = (pan >> (i * 4)) & 0xF;
    Serial.print((char)(nib < 10 ? ('0' + nib) : ('A' + (nib - 10))));
  }
  Serial.print(" (idx=");
  uint32_t idx = TMU_PanToDebugIndex(pan);
  if (idx == 0xFFFFFFFFu) Serial.print("INVALID");
  else Serial.print(idx);
  Serial.println(")");
}

bool TM_SD_Ensure();

// Build a consistent snapshot of current system state.
// This is the single source of truth for the info screen.
BootInfo BuildBootInfo();


// -----------------------------------------------------------------------------
// CW / WPM helpers
// -----------------------------------------------------------------------------
//
// Rationale:
//  - Flex publishes CW speed in two places. 'cwx.wpm' tends to reflect the
//    user's intended CW speed early in the session, while 'transmit.speed'
//    may be stale (often 30) until the radio has actually keyed CW.
//  - To avoid duplicating this logic across the sketch, these helpers centralize
//    how we read and validate the CW speed and how we reason about TX mode.
//

// Returns true if a TX slice exists and has a non-empty mode string.
// If true, writes the current TX mode into 'modeOut'.
bool TMU_TxModeKnown(String& modeOut);

// Convenience: true iff current TX slice exists and its mode is "CW".
bool TMU_TxIsCw();

// Clamp a WPM value to a sane range (inclusive).
int TMU_ClampWpm(int wpm);

// Single source of truth for fetching CW WPM from the radio:
//  - Requires fRig.connected to consider radio values.
//  - Prefers cwx.wpm when available (>0), clamped via TMU_ClampWpm.
//  - Falls back to transmit.speed only if TX mode is known AND CW.
//  - If no valid radio value is available, returns the current global CWVal.
//
// Always returns a positive WPM.
int TMU_GetReportedCwWpm();

// Debug helper: log a compact snapshot of CW/WPM-related state exactly once
// when TX mode becomes known after being unknown.
// Emission controlled by DEBUG_WPM.
void TMU_LogCwSyncSnapshot(const char* tag);

// Adopt CW WPM from the radio into the local keyer state.
// - onlyWhenCwMode: if true, only adopt when TX slice mode is CW.
// - preserveBaseline: passed through to Keyer_Apply_Wpm (see its doc).
// - outAppliedWpm: optional; receives the applied/clamped WPM or -1 if skipped.
// Returns true if a new WPM was applied.
bool TMU_AdoptCwWpmIfValid(bool onlyWhenCwMode,
                           bool preserveBaseline,
                           int* outAppliedWpm = nullptr,
                           const char* tag = nullptr);

// Mode-aware sync policy (loop/event usage):
//  - Uses TMU_GetReportedCwWpm() as the sole radio source.
//  - Debounces against current CWVal.
//  - Skips if rig not connected or TX mode unknown/empty (handled in getter).
//
// Returns true if a new WPM was applied. Writes the applied WPM to outAppliedWpm if provided.
bool TMU_SyncCwWpm(bool preserveBaseline,
                   int* outAppliedWpm = nullptr,
                   const char* tag = nullptr);

// Unified cooperative service pump for TeensyMaestro subsystems.
// Used to keep network I/O, event dispatching, and background tasks flowing
// during wait loops or timing-sensitive operations.

struct TM_ServiceOptions {
  bool timingCritical = false; // true = absolutely no sleeps or delays
  bool pumpRig        = true;  // run fRig.process() + fRig.fireEvents()
  bool pumpWK         = true;  // run TM_WK_Bridge::poll()
  bool pumpMtpd       = true;  // run MTP.loop() to service USB MTP
  bool allowSleep     = true;  // permit delay(1) if not timingCritical
};

// Main entry point
void TM_Service(const TM_ServiceOptions& opt = TM_ServiceOptions());

// Convenience wrappers for common modes
inline void TM_PumpFast() {
  TM_Service({/*timingCritical*/true,
              /*pumpRig*/true,
              /*pumpWK*/false,
              /*pumpMtpd*/false,
              /*allowSleep*/false});
}

inline void TM_PumpCoop() {
  TM_Service({/*timingCritical*/false,
              /*pumpRig*/true,
              /*pumpWK*/true,
              /*pumpMtpd*/true,
              /*allowSleep*/true});
}

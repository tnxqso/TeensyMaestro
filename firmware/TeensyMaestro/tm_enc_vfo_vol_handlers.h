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

#include "tm_project.hpp"
#include "tm_enc_helpers.hpp"
#include "tm_rig_alias.h"

// ===== Externals provided elsewhere in the project ============================

// Slices
#ifndef A
#define A 0
#endif
#ifndef B
#define B 1
#endif

// Always enable verbose VFO debug unless you comment this out
//#define TM_VFO_ACCEL_DEBUG_VERBOSE

// Encoders (VFO) — concrete type lives elsewhere (e.g., Encoder, TMEncoder, etc.)
extern decltype(VolAEnc) VFOAEnc;   // reuse same underlying type as volume encoders
extern decltype(VolBEnc) VFOBEnc;

// Volume encoders / helpers are declared in tm_enc_helpers.hpp
extern decltype(VolAEnc) VolAEnc;
extern decltype(VolBEnc) VolBEnc;
extern int VolAEncSteps, VolBEncSteps;
extern int VolVal[2], VolValSave[2];

// VFO state from main application
extern volatile int      VFOAccelCount[2];
extern volatile int      AccelStep[2];
extern volatile int      VFOTuningRate[2];
extern volatile int      VFOTuningRateSave[2];

extern int               VFOStep[2];
extern const int         StepSize[];           // step size (Hz) by VFOStep index
extern long              VFOVal[2];
extern int               VFODir[2], VFODirSave[2];

extern int               SetFreq[2];           // .ino: int SetFreq[2];
extern double            CurFreq[2];           // .ino: double CurFreq[2] = {0.0, 0.0};

extern bool              VFOTrack;
extern bool              SnapToStep;
extern bool              VFOaccel;
extern bool              MenuActive;
extern bool              AccelTimingActive[2]; // kept for compatibility

extern void              DispFrq(int slice);

extern bool CheckInBand(int slice, bool sync_ui); // no default here!
extern void CheckInBand(int slice);               // legacy wrapper prototype

extern void              ResetScreenSaver(const char* reason);
// Acceleration shaping parameters (runtime-tunable via MMConfig.ini)
extern int               AccelFactor;          // quadratic divisor; smaller => more accel
extern float             VFOAccel_OnFactor;
extern float             VFOAccel_OffFactor;
extern int               VFOAccel_MinDeadband;
extern int               VFOAccel_CountEmaDen;
extern int               VFOAccel_AccelEmaDen;
extern int               VFOAccel_MaxUpMult;
extern int               VFOAccel_MaxDownMult;
extern int               VFOAccel_MaxAbsHz;

// Small confirmation window (not user-configurable)
static constexpr uint8_t TM_ON_CONFIRM_TICKS = 2;

// ===== VFO helpers ============================================================

// Access raw VFO encoder by slice (no quantization)
static inline long vfo_read(int slice) {
  return (slice == A) ? VFOAEnc.read() : VFOBEnc.read();
}
static inline void vfo_write_zero(int slice) {
  if (slice == A) VFOAEnc.write(0);
  else            VFOBEnc.write(0);
}

// Reset acceleration/timing state for a slice
static inline void vfo_reset_accel_state(int slice) {
  VFOAccelCount[slice]     = 0;
  AccelStep[slice]         = 0;
  VFOTuningRate[slice]     = VFOTuningRateSave[slice];
  AccelTimingActive[slice] = false;
}

// ===== VFO core handlers ======================================================

// Locked path: zero encoder, reset state
static void HandleVFO_Locked(const int slice) {
  vfo_write_zero(slice);
  vfo_reset_accel_state(slice);
}

// Direction change: stop acceleration and reset
static void HandleVFO_OnDirectionChange(const int slice) {
  VFOAccelCount[slice]     = 0;
  VFOVal[slice]            = 0;
  AccelStep[slice]         = 0;
  VFOTuningRate[slice]     = VFOTuningRateSave[slice];
  AccelTimingActive[slice] = false;
  vfo_write_zero(slice); // resync baseline so next delta is fresh
}

// Perform one VFO move when threshold exceeded
static void HandleVFO_MoveOnce(const int slice) {
  ResetScreenSaver("EncVFO: move once");

  const int baseStep = StepSize[VFOStep[slice]];
  const int accStep  = AccelStep[slice];

  if (VFOVal[slice] > 0) {
    // Up
    SetFreq[slice] = fRig.slice[slice].RF_frequency + baseStep + accStep;
    if (VFOTrack && fRig.slice[1 - slice].in_use == 1) {
      SetFreq[1 - slice] = fRig.slice[1 - slice].RF_frequency + baseStep + accStep;
    }
  } else {
    // Down
    SetFreq[slice] = fRig.slice[slice].RF_frequency - baseStep - accStep;
    if (VFOTrack && fRig.slice[1 - slice].in_use == 1) {
      SetFreq[1 - slice] = fRig.slice[1 - slice].RF_frequency - baseStep - accStep;
    }
  }

  // Optional snap-to-step
  if (SnapToStep) {
    const int step = baseStep;
    SetFreq[slice] = (SetFreq[slice] / step) * step;
    if (VFOTrack && fRig.slice[1 - slice].in_use == 1) {
      SetFreq[1 - slice] = (SetFreq[1 - slice] / step) * step;
    }
  }

#ifdef TM_VFO_ACCEL_DEBUG_VERBOSE
  const long preHz   = fRig.slice[slice].RF_frequency;
  const long postHz  = SetFreq[slice];
  const long deltaHz = postHz - preHz;

  // print only when something moves or accel is applied
  if (deltaHz != 0 || accStep != 0) {
    const char dirChar = (deltaHz >= 0) ? '+' : '-';
    Serial.print("APPLY: slice=");
    Serial.print(slice);
    Serial.print(" dir=");
    Serial.print(dirChar);
    Serial.print(" base=");
    Serial.print(baseStep);
    Serial.print(" acc=");
    Serial.print(accStep);
    Serial.print(" total=");
    Serial.print(baseStep + accStep);
    Serial.print("  delta=");
    Serial.print(deltaHz);
    Serial.print(" Hz  freq=");
    Serial.print(preHz);
    Serial.print("->");
    Serial.println(postHz);
  }
#endif

  // Apply to radio and UI
  fRig.setFreq(slice, SetFreq[slice]);
  DispFrq(slice);
  CheckInBand(slice);

  if (VFOTrack && fRig.slice[1 - slice].in_use == 1) {
    fRig.setFreq(1 - slice, SetFreq[1 - slice]);
    DispFrq(1 - slice);
    CheckInBand(1 - slice);
  }

  // Update current frequencies
  CurFreq[A] = fRig.slice[A].RF_frequency;
  CurFreq[B] = fRig.slice[B].RF_frequency;

  // Clear delta and small settle
  vfo_write_zero(slice);
  delayMicroseconds(250);
  VFOVal[slice] = 0;
}

// Idle path when no movement: clear accel and restore baseline rate
static void HandleVFO_Idle(const int slice) {
  vfo_reset_accel_state(slice);
}

// ===== VFO entry (classic threshold-based loop) ===============================

/*
 * Flow:
 *  - If slice is locked, zero & reset.
 *  - Read raw encoder delta; derive direction; on direction change, reset.
 *  - Accumulate absolute movement into VFOAccelCount[slice] (used by accel task).
 *  - If |delta| > VFOTuningRate[slice], perform one move step.
 *  - Else idle-reset accel state.
 */
static inline void ReadVFOEnc(int Slice)
{
  // Lock gate
  if (fRig.slice[Slice].lock == 1) {
    HandleVFO_Locked(Slice);
    return;
  }

  // Read raw delta & track direction
  VFOVal[Slice] = vfo_read(Slice);
  VFODir[Slice] = (VFOVal[Slice] > 0) ? 1 : -1;
  if (VFODir[Slice] != VFODirSave[Slice]) {
    VFODirSave[Slice] = VFODir[Slice];
    HandleVFO_OnDirectionChange(Slice);
  }

  // Accumulate absolute movement for acceleration logic
  VFOAccelCount[Slice] += abs(VFOVal[Slice]);

  // Thresholded move or idle
  if (VFOVal[Slice] != 0) {
    if (abs(VFOVal[Slice]) > VFOTuningRate[Slice]) {
      HandleVFO_MoveOnce(Slice);
    }
  } else {
    HandleVFO_Idle(Slice);
  }
}

/******************************** Volume handlers ********************************/

static inline void HandleVol_A() {
  if (!enc_is_on_step(VolAEnc, VolAEncSteps)) return;

  VolVal[A] = enc_read_quantized(VolAEnc, VolAEncSteps);
  if (VolVal[A] == VolValSave[A]) return;

  // Clamp 0..100 and sync encoder when clamped
  VolVal[A] = clamp_and_sync_encoder(VolAEnc, VolVal[A], 0, 100, VolAEncSteps);

  ResetScreenSaver("EncVol A");

  fRig.setAudioGain(A, VolVal[A]);
  VolValSave[A] = VolVal[A];
  DispVol(A);
}

static inline void HandleVol_B() {
  if (!enc_is_on_step(VolBEnc, VolBEncSteps)) return;

  VolVal[B] = enc_read_quantized(VolBEnc, VolBEncSteps);
  if (VolVal[B] == VolValSave[B]) return;

  VolVal[B] = clamp_and_sync_encoder(VolBEnc, VolVal[B], 0, 100, VolBEncSteps);

  ResetScreenSaver("EncVol B");

  fRig.setAudioGain(B, VolVal[B]);
  VolValSave[B] = VolVal[B];

  DispVol(B);
}

// Public entry points for volume
static inline void ReadVolAEnc() { HandleVol_A(); }
static inline void ReadVolBEnc() { HandleVol_B(); }

// ===== VFO Acceleration Processor ============================================

/*
  Acceleration with hysteresis + short confirmation window + smoothed EMA.

  - Runs ~100 Hz (every ~10 ms).
  - Requires TM_ON_CONFIRM_TICKS consecutive ticks above On-threshold to enable.
  - Disables below Off-threshold (hysteresis).
  - EMA on input counts and on acceleration term.
  - Gentle decay when target goes to zero.
  - Slew limits bound how fast accel can rise/fall per tick (scaled by Step).
  - Holds VFOTuningRate[] at VFOTuningRateSave[] (prevents jittery UI).

  Uses global runtime-tunable parameters loaded from MMConfig.ini:
    VFOAccel_OnFactor, VFOAccel_OffFactor,
    VFOAccel_MinDeadband, VFOAccel_CountEmaDen, VFOAccel_AccelEmaDen,
    VFOAccel_MaxUpMult, VFOAccel_MaxDownMult, VFOAccel_MaxAbsHz,
    AccelFactor (quadratic divisor; smaller => more acceleration).
*/
static inline void TM_VFOAccel_Process()
{
  // ~100 Hz pacing
  static uint32_t s_last_ms = 0;
  const uint32_t now = millis();
  if (now - s_last_ms < 10) return;
  s_last_ms = now;

  // If disabled or menu is active: clear counters, hold saved rate, zero accel
  if (!VFOaccel || MenuActive) {
    noInterrupts();
    VFOAccelCount[0] = 0;
    VFOAccelCount[1] = 0;
    interrupts();

    VFOTuningRate[0] = VFOTuningRateSave[0];
    VFOTuningRate[1] = VFOTuningRateSave[1];
    AccelStep[0]     = 0;
    AccelStep[1]     = 0;
    return;
  }

  // Snapshot raw counts since last tick (produced elsewhere)
  uint32_t cnt_raw[2];
  noInterrupts();
  cnt_raw[0] = (uint32_t)VFOAccelCount[0];
  cnt_raw[1] = (uint32_t)VFOAccelCount[1];
  VFOAccelCount[0] = 0;
  VFOAccelCount[1] = 0;
  interrupts();

  // Per-slice persistent state
  static uint32_t s_cntEma[2]  = {0, 0};  // filtered count
  static int      s_accEma[2]  = {0, 0};  // filtered accel term (Hz)
  static bool     s_accOn[2]   = {false, false};
  static uint8_t  s_onTicks[2] = {0, 0};

  auto process_slice = [&](int idx) {
    // Keep visible tuning rate locked to saved value (prevents UI jitter)
    const int saveRate = VFOTuningRateSave[idx];
    VFOTuningRate[idx] = saveRate;

    // Thresholds derived from saved rate
    const float th_on  = VFOAccel_OnFactor  * (float)saveRate;
    const float th_off = VFOAccel_OffFactor * (float)saveRate;

    // --- Count EMA ---
    const int denCnt = (VFOAccel_CountEmaDen < 2) ? 2 : VFOAccel_CountEmaDen;
    uint32_t cntEma = (s_cntEma[idx] == 0)
                      ? cnt_raw[idx]
                      : ( (s_cntEma[idx] * (uint32_t)(denCnt - 1) + cnt_raw[idx]) / (uint32_t)denCnt );
    s_cntEma[idx] = cntEma;

    // Deadband to ignore tiny tails after stopping
    const uint32_t minDB = (uint32_t)((VFOAccel_MinDeadband < 0) ? 0 : VFOAccel_MinDeadband);
    const uint32_t deadbFromRate = (saveRate >= 24) ? (uint32_t)(saveRate / 3) : minDB;
    const uint32_t deadb = (deadbFromRate > minDB) ? deadbFromRate : minDB;
    if (cntEma <= deadb) cntEma = 0;

    // --- Hysteresis + confirmation window ---
    if (!s_accOn[idx]) {
      if ((float)cntEma >= th_on) {
        if (++s_onTicks[idx] >= TM_ON_CONFIRM_TICKS) {
          s_accOn[idx]  = true;
          s_onTicks[idx]= 0;
        }
      } else {
        s_onTicks[idx] = 0;
      }
    } else {
      if ((float)cntEma <= th_off) {
        s_accOn[idx]   = false;
        s_onTicks[idx] = 0;
      }
    }

    // --- Target acceleration (Hz), quadratic in cntEma ---
    int targetAcc = 0;
    if (s_accOn[idx] && cntEma > 0) {
      const int stepHz = StepSize[VFOStep[idx]];
      int effFactor    = AccelFactor;           // smaller => more acceleration

      // Taper very small steps to avoid jumpiness on fine resolution
      if (stepHz < 50 && effFactor > 0) effFactor /= 3;

      // Guardrail; avoids runaway on tiny divisors
      if (effFactor < 300) effFactor = 300;

      const uint64_t sq = (uint64_t)cntEma * (uint64_t)cntEma;
      uint32_t mul      = (uint32_t)(sq / (uint64_t)effFactor);
      // Do not force mul>=1 (prevents sticky onset).

      // Clamp multiplier and absolute accel
      const uint32_t MAX_MUL = 500u;
      if (mul > MAX_MUL) mul = MAX_MUL;

      const int absCap = (VFOAccel_MaxAbsHz > 0) ? VFOAccel_MaxAbsHz : 30000;
      targetAcc = stepHz * (int)mul;
      if (targetAcc > absCap) targetAcc = absCap;
    }

    // --- Smooth approach to target + gentle decay + slew-limit (integer EMA) ---
    const int denAcc   = (VFOAccel_AccelEmaDen < 3) ? 3 : VFOAccel_AccelEmaDen;
    const int baseStep = StepSize[VFOStep[idx]];

    // Slew limits scale with the current step size (keeps your semantics)
    const int MAX_UP   = baseStep * ((VFOAccel_MaxUpMult   > 0) ? VFOAccel_MaxUpMult   : 60);
    const int MAX_DOWN = baseStep * ((VFOAccel_MaxDownMult > 0) ? VFOAccel_MaxDownMult : 20);

    // Start from previous filtered acceleration (Hz)
    const int prevAcc = s_accEma[idx];
    int accEma        = prevAcc;

    if (targetAcc != 0) {
      // EMA toward target with rounding to nearest (reduces bias to 0)
      const int delta = targetAcc - accEma;
      // Slightly "snappier" EMA: bias rounding a tad to react sooner
      accEma += (delta >= 0 ? (delta + denAcc*3/5) : (delta - denAcc*3/5)) / denAcc;

      // Stronger minimum floor so slow turning still moves visibly
      if (accEma == 0) {
        const int min_floor_hz = baseStep;  // floor was baseStep/2
        accEma = (targetAcc > 0) ?  min_floor_hz : -min_floor_hz;
      }
    } else {
      // Faster decay when knob stops
      int dropPct  = 15;                    // was ~10%
      int drop     = (accEma >= 0 ?  accEma : -accEma) * dropPct / 100;
      int minDrop  = baseStep / 2;          // was baseStep/3
      if (drop < minDrop) drop = minDrop;
      accEma += (accEma >= 0) ? -drop : +drop;   // symmetric toward zero
      if ((prevAcc >= 0 && accEma < 0) || (prevAcc <= 0 && accEma > 0)) accEma = 0;
    }

    // Absolute safety cap
    const int absCap = (VFOAccel_MaxAbsHz > 0) ? VFOAccel_MaxAbsHz : 30000;
    if (accEma >  absCap) accEma =  absCap;
    if (accEma < -absCap) accEma = -absCap;

    // Relative caps per direction (multiples of base step)
    const int up_cap = baseStep * VFOAccel_MaxUpMult;
    const int dn_cap = baseStep * VFOAccel_MaxDownMult;
    if (accEma >  up_cap) accEma =  up_cap;
    if (accEma < -dn_cap) accEma = -dn_cap;

    // Slew-limit compared to previous filtered value
    {
      int delta = accEma - prevAcc;
      if (delta >  MAX_UP)   delta =  MAX_UP;
      if (delta < -MAX_DOWN) delta = -MAX_DOWN;
      accEma = prevAcc + delta;
    }

    // Commit
    s_accEma[idx]  = accEma;
    AccelStep[idx] = accEma;
  };

  process_slice(0);
  process_slice(1);

#ifdef TM_VFO_ACCEL_DEBUG_VERBOSE
  static uint32_t dbg_t = 0;
  if (now - dbg_t >= 250) {
    dbg_t = now;

    const long aHz = fRig.slice[0].RF_frequency;
    const long bHz = fRig.slice[1].RF_frequency;

    Serial.printf(
      "ACCEL: f=%d | A:cnt=%lu ema=%lu rate=%d acc=%d freq=%ld | B:cnt=%lu ema=%lu rate=%d acc=%d freq=%ld\n",
      AccelFactor,
      (unsigned long)cnt_raw[0], (unsigned long)s_cntEma[0], VFOTuningRate[0], AccelStep[0], aHz,
      (unsigned long)cnt_raw[1], (unsigned long)s_cntEma[1], VFOTuningRate[1], AccelStep[1], bHz
    );
  }
#endif
}

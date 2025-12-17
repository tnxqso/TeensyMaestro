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

/******************************* AGC (slice A) **********************************/

// AGC main (no RIT/XIT active): squelch in FM-family, AGC threshold otherwise.
static void HandleAGC_A_Main() {
  if (!enc_is_on_step(AGCAEnc, AGCAEncSteps)) return;

  AGCVal[A] = enc_read_quantized(AGCAEnc, AGCAEncSteps);
  if (AGCVal[A] == AGCValSave[A]) return;

  // Clamp 0..100 and sync encoder if clamped
  AGCVal[A] = clamp_and_sync_encoder(AGCAEnc, AGCVal[A], 0, 100, AGCAEncSteps);

  ResetScreenSaver("EncAGC A main");

  // FM family -> squelch; otherwise -> AGC threshold
  const String& mode = fRig.slice[A].mode;
  if (mode == "FM" || mode == "NFM" || mode == "DFM") {
    fRig.setSquelchLevel(A, AGCVal[A]);
  } else {
    fRig.setAgcThreshold(A, AGCVal[A]);
  }

  AGCValSave[A] = AGCVal[A];
}

// RIT path (A)
static void HandleAGC_A_RIT() {
  // NOTE: original code does NOT require on-step check in RIT/XIT branches
  RITVal[A] = enc_read_quantized(AGCAEnc, AGCAEncSteps) * 10;
  if (RITVal[A] == RITValSave[A]) return;

  ResetScreenSaver("EncAGC A RIT");  

  SelectedTimer = millis();
  debug("RIT A: ");
  debugln(RITVal[A]);

  fRig.setRitFreq(A, RITVal[A]);
  RITValSave[A] = RITVal[A];
}

// XIT path (A)
static void HandleAGC_A_XIT() {
  XITVal[A] = enc_read_quantized(AGCAEnc, AGCAEncSteps) * 10;
  if (XITVal[A] == XITValSave[A]) return;

  ResetScreenSaver("EncAGC A XIT");
  
  SelectedTimer = millis();
  debug("XIT A: ");
  debugln(XITVal[A]);

  fRig.setXitFreq(A, XITVal[A]);
  XITValSave[A] = XITVal[A];
}

/******************************* AGC (slice B) **********************************/

static void HandleAGC_B_Main() {
  if (!enc_is_on_step(AGCBEnc, AGCBEncSteps)) return;

  AGCVal[B] = enc_read_quantized(AGCBEnc, AGCBEncSteps);
  if (AGCVal[B] == AGCValSave[B]) return;

  AGCVal[B] = clamp_and_sync_encoder(AGCBEnc, AGCVal[B], 0, 100, AGCBEncSteps);

  ResetScreenSaver("EncAGC B main");
  
  const String& mode = fRig.slice[B].mode;
  if (mode == "FM" || mode == "NFM" || mode == "DFM") {
    fRig.setSquelchLevel(B, AGCVal[B]);
  } else {
    fRig.setAgcThreshold(B, AGCVal[B]);
  }

  AGCValSave[B] = AGCVal[B];
}

static void HandleAGC_B_RIT() {
  RITVal[B] = enc_read_quantized(AGCBEnc, AGCBEncSteps) * 10;
  if (RITVal[B] == RITValSave[B]) return;

  ResetScreenSaver("EncAGC B RIT");

  SelectedTimer = millis();
  debug("RIT B: ");
  debugln(RITVal[B]);

  fRig.setRitFreq(B, RITVal[B]);
  RITValSave[B] = RITVal[B];
}

static void HandleAGC_B_XIT() {
  XITVal[B] = enc_read_quantized(AGCBEnc, AGCBEncSteps) * 10;
  if (XITVal[B] == XITValSave[B]) return;

  ResetScreenSaver("EncAGC B XIT");

  SelectedTimer = millis();
  debug("XIT B: ");
  debugln(XITVal[B]);

  fRig.setXitFreq(B, XITVal[B]);
  XITValSave[B] = XITVal[B];
}

/**************************** Custom bandwidth (High A) *************************/

static void HandleHigh_A_Main() {
  if (!enc_is_on_step(HighAEnc, HighAEncSteps)) return;

  HighVal[A] = enc_read_quantized(HighAEnc, HighAEncSteps);
  if (HighVal[A] == HighValSave[A]) return;

  HighValSave[A] = HighVal[A];

  ResetScreenSaver("EncHigh A main");

  // Non-CW: set high cutoff; CW: set width
  if (fRig.slice[A].mode != "CW") {
    fRig.setRxFiltHigh(A, HighVal[A] * 10);
  } else {
    fRig.setRxFiltWidth(A, HighVal[A] * 10);
  }
}

static void HandleHigh_A_NR() {
  NRVal[A] = enc_read_quantized(HighAEnc, HighAEncSteps);

  // Clamp 0..100 and sync encoder if clamped
  NRVal[A] = clamp_and_sync_encoder(HighAEnc, NRVal[A], 0, 100, HighAEncSteps);

  if (NRVal[A] == NRValSave[A]) return;

  SelectedTimer = millis();

  ResetScreenSaver("EncHigh A NR");

  fRig.setNrLevel(A, NRVal[A]);
  NRValSave[A] = NRVal[A];
}

/**************************** Custom bandwidth (High B) *************************/

static void HandleHigh_B_Main() {
  if (!enc_is_on_step(HighBEnc, HighBEncSteps)) return;

  HighVal[B] = enc_read_quantized(HighBEnc, HighBEncSteps);
  if (HighVal[B] == HighValSave[B]) return;

  HighValSave[B] = HighVal[B];

  ResetScreenSaver("EncHigh B main");

  if (fRig.slice[B].mode != "CW") {
    fRig.setRxFiltHigh(B, HighVal[B] * 10);
  } else {
    fRig.setRxFiltWidth(B, HighVal[B] * 10);
  }
}

static void HandleHigh_B_NR() {
  NRVal[B] = enc_read_quantized(HighBEnc, HighBEncSteps);
  NRVal[B] = clamp_and_sync_encoder(HighBEnc, NRVal[B], 0, 100, HighBEncSteps);

  if (NRVal[B] == NRValSave[B]) return;

  ResetScreenSaver("EncHigh B NR");

  SelectedTimer = millis();
  fRig.setNrLevel(B, NRVal[B]);
  NRValSave[B] = NRVal[B];
}

/***************************** Custom bandwidth (Low A) *************************/

static void HandleLow_A_Main() {
  if (!enc_is_on_step(LowAEnc, LowAEncSteps)) return;

  LowVal[A] = enc_read_quantized(LowAEnc, LowAEncSteps);
  if (LowVal[A] == LowValSave[A]) return;

  LowValSave[A] = LowVal[A];

  ResetScreenSaver("EncLow A main");

  // Non-CW: set low cutoff; CW: set shift
  if (fRig.slice[A].mode != "CW") {
    fRig.setRxFiltLow(A, LowVal[A] * 10);
  } else {
    fRig.setRxFiltShift(A, LowVal[A] * 10);
  }
}

static void HandleLow_A_NB() {
  NBVal[A] = enc_read_quantized(LowAEnc, LowAEncSteps);
  NBVal[A] = clamp_and_sync_encoder(LowAEnc, NBVal[A], 0, 100, LowAEncSteps);

  if (NBVal[A] == NBValSave[A]) return;

  ResetScreenSaver("EncLow A NB");

  SelectedTimer = millis();
  fRig.setNbLevel(A, NBVal[A]);
  NBValSave[A] = NBVal[A];
}

/***************************** Custom bandwidth (Low B) *************************/

static void HandleLow_B_Main() {
  if (!enc_is_on_step(LowBEnc, LowBEncSteps)) return;

  LowVal[B] = enc_read_quantized(LowBEnc, LowBEncSteps);
  if (LowVal[B] == LowValSave[B]) return;

  LowValSave[B] = LowVal[B];

  ResetScreenSaver("EncLow B main");

  if (fRig.slice[B].mode != "CW") {
    fRig.setRxFiltLow(B, LowVal[B] * 10);
  } else {
    fRig.setRxFiltShift(B, LowVal[B] * 10);
  }
}

static void HandleLow_B_NB() {
  NBVal[B] = enc_read_quantized(LowBEnc, LowBEncSteps);
  NBVal[B] = clamp_and_sync_encoder(LowBEnc, NBVal[B], 0, 100, LowBEncSteps);

  if (NBVal[B] == NBValSave[B]) return;

  ResetScreenSaver("EncLow B NB");

  SelectedTimer = millis();
  fRig.setNbLevel(B, NBVal[B]);
  NBValSave[B] = NBVal[B];
}

/******************************* Public entry points ****************************/
/*  Behavior is 1:1 with the original implementations. Each entry point routes
 *  to its matching handler based on the original conditions.
 */

// ReadAgcAEnc()
static inline void ReadAgcAEnc()
{
  if (!SetRIT[A] && !SetXIT[A]) {
    HandleAGC_A_Main();
  } else {
    if (SetRIT[A])      HandleAGC_A_RIT();
    else if (SetXIT[A]) HandleAGC_A_XIT();
  }
}

// ReadAgcBEnc()
static inline void ReadAgcBEnc()
{
  if (!SetRIT[B] && !SetXIT[B]) {
    HandleAGC_B_Main();
  } else {
    if (SetRIT[B])      HandleAGC_B_RIT();
    else if (SetXIT[B]) HandleAGC_B_XIT();
  }
}

// ReadHighAEnc()
static inline void ReadHighAEnc()
{
  if (!SetNR[A]) {
    HandleHigh_A_Main();
  } else {
    HandleHigh_A_NR();
  }
}

// ReadHighBEnc()
static inline void ReadHighBEnc()
{
  if (!SetNR[B]) {
    HandleHigh_B_Main();
  } else {
    HandleHigh_B_NR();
  }
}

// ReadLowAEnc()
static inline void ReadLowAEnc()
{
  if (!SetNB[A]) {
    HandleLow_A_Main();
  } else {
    HandleLow_A_NB();
  }
}

// ReadLowBEnc()
static inline void ReadLowBEnc()
{
  if (!SetNB[B]) {
    HandleLow_B_Main();
  } else {
    HandleLow_B_NB();
  }
}

/*
  TM_Keyer_Wrapper.cpp

  TeensyMaestro — Community Edition (CE)
  SPDX-License-Identifier: CC-BY-NC-SA-3.0
  SPDX-FileCopyrightText: 2025 TNX QSO

  A community-maintained edition with open-source utilities
  for ham radio enthusiasts, focusing on FlexRadio® and Wavelog integrations.
*/

#include <Arduino.h>
#include "TM_Keyer_Engine.h"
#include "TM_Macro_Engine.h" 
#include "tm_system_utils.h" 
#include "Encoder.h"         
#include <FlexRigTeensy.h>   

// --- Globals from TeensyMaestro.ino ---
extern TM_Keyer_Engine g_keyerEngine;

// Use pointer to access the FlexRig instance
extern FlexRig* g_fRig; 

extern volatile int CWVal;
extern volatile int CWValSave;
extern volatile int WPM;
extern volatile long ElementLen;
extern Encoder CWMicEnc;
extern int Encoder_9;
extern int CWEncSteps;

// Non-const globals work fine with extern
extern byte DotPin;
extern byte DashPin;

// Const globals from .ino are invisible to linker, so we define local copies here
// matching TeensyMaestroConstants
static const byte LOCAL_KeyOutPin      = 33;
static const byte LOCAL_StraightKeyPin = 32;
static const byte LOCAL_STPin          = 34;

// Globals used by Macros
extern String MyCall;
extern int SerNum;

// Global instance of Macro Engine
TM_Macro_Engine g_macroEngine(g_keyerEngine);

// Forward declarations
void DispCWSpeed();

// --- Missing Linker Fixes ---

// Define missing flag required by tm_system_utils.cpp
volatile bool g_KeyerTimingActive = false;

// Local constants for Encoders
static const int Enc9_CWSpeed = 0;
static const int Enc9_RFPower = 2;
static const int Enc9_Band    = 8;

// --- Implementation of Keyer API ---

void KeyerSetup() {
  // Configure Pins using local definitions
  pinMode(DotPin, INPUT_PULLUP);
  pinMode(DashPin, INPUT_PULLUP);
  pinMode(LOCAL_StraightKeyPin, INPUT_PULLUP);
  
  pinMode(LOCAL_KeyOutPin, OUTPUT);
  pinMode(LOCAL_STPin, OUTPUT);

  digitalWriteFast(LOCAL_KeyOutPin, LOW); 

  // Reset variables
  g_KeyerTimingActive = false;
  
  // Sync engine settings
  if (WPM < 5) WPM = 20;
  g_keyerEngine.setWpm((uint8_t)WPM);
}

void Keyer_Beep(uint16_t freq, uint16_t ms) {
    tone(LOCAL_STPin, freq, ms);
}

void Keyer_Apply_Wpm(int newWpm, bool preserveBaseline)
{
  newWpm = TMU_ClampWpm(newWpm); 

  CWVal = newWpm;
  if (!preserveBaseline) {
    CWValSave = CWVal;
  }
  WPM = CWVal;
  
  // Legacy UI variable update
  ElementLen = (1200000L / (WPM > 0 ? WPM : 1));

  // Notify Engine
  g_keyerEngine.setWpm((uint8_t)CWVal);

  // Update UI/Encoder
  if (Encoder_9 == Enc9_CWSpeed) {
    CWMicEnc.write(CWVal * CWEncSteps);
  }
  if (Encoder_9 == Enc9_CWSpeed || Encoder_9 == Enc9_RFPower || Encoder_9 == Enc9_Band) {
    DispCWSpeed();
  }
}

void Keyer_Recalc_Timing() {
    ElementLen = (1200000L / (WPM > 0 ? WPM : 1));
}

void Keyer_AbortNow(void) {
    g_keyerEngine.abortNow();
}

// Dummy/Stub function for old loop (linker satisfaction)
void KeyerLoop() {
}

// --- MACRO IMPLEMENTATION ---

void SendMsg(String M) {
    // 1. Sync globals to Macro Engine
    g_macroEngine.setCallsign(MyCall);
    g_macroEngine.setSerialNumber(SerNum);

    // 2. Play Macro
    g_macroEngine.play(M);

    // 3. Sync Serial back
    SerNum = g_macroEngine.getSerialNumber();
}

void SendMsgRaw(const char* data, size_t len) {
    // Direct passthrough to engine
    for(size_t i=0; i<len; i++) {
        g_keyerEngine.enqueueChar(data[i]);
    }
}

// Send FlexRadio Macro (CAT command)
void SendFlexMsg(int M)
{
  if (!g_fRig || !g_fRig->connected) return;

  // Find active TX slice in CW mode
  for (int slice = 0; slice < g_fRig->nMaxSlice; ++slice)
  {
    if (g_fRig->slice[slice].in_use == 1 &&
        g_fRig->slice[slice].tx == 1 &&
        g_fRig->slice[slice].mode == "CW")
    {
      // Build the macro command: cwx macro send <index>
      char buf[32];
      snprintf(buf, sizeof(buf), "cwx macro send %d", M);
      g_fRig->send(buf);   
      break;
    }
  }
}

// --- Status Helpers ---

uint16_t Keyer_TxQ_Used(void) {
    return g_keyerEngine.getQueueSize();
}

uint16_t Keyer_TxQ_CapacityBytes(void) {
    return 256;
}

extern "C" {
    uint16_t Keyer_TxQ_Used_C(void) {
        return Keyer_TxQ_Used();
    }
}
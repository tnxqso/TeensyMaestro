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
#include "tm_wk_proto.h"     // <--- ADDED: Required for protocol sync

// --- Globals from TeensyMaestro.ino ---
extern TM_Keyer_Engine g_keyerEngine;
extern FlexRig* g_fRig; 

extern volatile int CWVal;
extern volatile int CWValSave;
extern volatile int WPM;
extern volatile long ElementLen;
extern Encoder CWMicEnc;
extern int Encoder_9;
extern int CWEncSteps;

extern byte DotPin;
extern byte DashPin;

static const byte LOCAL_KeyOutPin      = 33;
static const byte LOCAL_StraightKeyPin = 32;
static const byte LOCAL_STPin          = 34;

extern String MyCall;
extern int SerNum;

TM_Macro_Engine g_macroEngine(g_keyerEngine);

void DispCWSpeed();

volatile bool g_KeyerTimingActive = false;

static const int Enc9_CWSpeed = 0;
static const int Enc9_RFPower = 2;
static const int Enc9_Band    = 8;

// --- Implementation of Keyer API ---

void KeyerSetup() {
  pinMode(DotPin, INPUT_PULLUP);
  pinMode(DashPin, INPUT_PULLUP);
  pinMode(LOCAL_StraightKeyPin, INPUT_PULLUP);
  
  pinMode(LOCAL_KeyOutPin, OUTPUT);
  pinMode(LOCAL_STPin, OUTPUT);

  digitalWriteFast(LOCAL_KeyOutPin, LOW); 

  g_KeyerTimingActive = false;
  
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
  
  // Logic: If preserveBaseline is FALSE, it means the user turned the knob.
  // We must tell the WinKeyer Protocol that this is the new "Baseline" (Knob speed).
  if (!preserveBaseline) {
    CWValSave = CWVal;
    
    if (TM_WK_Protocol::active()) {
        TM_WK_Protocol::active()->setLocalBaseline((uint8_t)CWVal);
    }
  }
  WPM = CWVal;
  
  ElementLen = (1200000L / (WPM > 0 ? WPM : 1));

  g_keyerEngine.setWpm((uint8_t)CWVal);

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

void KeyerLoop() {
}

// --- MACRO IMPLEMENTATION ---

void SendMsg(String M) {
    g_macroEngine.setCallsign(MyCall);
    g_macroEngine.setSerialNumber(SerNum);
    g_macroEngine.play(M);
    SerNum = g_macroEngine.getSerialNumber();
}

void SendMsgRaw(const char* data, size_t len) {
    for(size_t i=0; i<len; i++) {
        g_keyerEngine.enqueueChar(data[i]);
    }
}

void SendFlexMsg(int M)
{
  if (!g_fRig || !g_fRig->connected) return;

  for (int slice = 0; slice < g_fRig->nMaxSlice; ++slice)
  {
    if (g_fRig->slice[slice].in_use == 1 &&
        g_fRig->slice[slice].tx == 1 &&
        g_fRig->slice[slice].mode == "CW")
    {
      char buf[32];
      snprintf(buf, sizeof(buf), "cwx macro send %d", M);
      g_fRig->send(buf);   
      break;
    }
  }
}

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
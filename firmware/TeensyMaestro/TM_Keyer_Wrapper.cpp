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
#include "tm_wk_proto.h"     

// C-hook declaration for Paddle Status
extern "C" void WK_OnPaddleActivity(bool active);

// --- Globals from TeensyMaestro.ino / Other Modules ---
extern TM_Keyer_Engine g_keyerEngine;
extern FlexRig* g_fRig; 

// Settings / State
extern volatile int CWVal;
extern volatile int CWValSave;
extern volatile int WPM;
extern volatile long ElementLen;
extern bool SideTone;
extern int STFreq;
extern volatile bool AbortMsg;
extern volatile bool KeyDown;
extern bool OldKeyDown;
extern String KeyerOut;
extern volatile unsigned int CWIndex;
extern int ClientMenuItem;

// Configuration Globals
extern String KeyMode;
extern int KeyerCompensation;
extern int KeyerFirstExtension;
extern int KeyerFarnsworth;
extern bool KeyerAutospace;

// Hardware Objects
extern Encoder CWMicEnc;
extern int Encoder_9;
extern int CWEncSteps;

// Pins
extern byte DotPin;
extern byte DashPin;
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
extern bool DoRigPTT(bool on);

// Missing Linker Fixes
volatile bool g_KeyerTimingActive = false;

static const int Enc9_CWSpeed = 0;
static const int Enc9_RFPower = 2;
static const int Enc9_Band    = 8;

// ============================================================
// ENGINE CALLBACKS
// ============================================================

// Callback: WPM Changed by Macro/Host
void Engine_Wpm_Callback(uint8_t newWpm) {
    CWVal = newWpm;
    WPM = newWpm;
    if (Encoder_9 == Enc9_CWSpeed) {
        CWMicEnc.write(CWVal * CWEncSteps);
    }
    if (Encoder_9 == Enc9_CWSpeed || Encoder_9 == Enc9_RFPower || Encoder_9 == Enc9_Band) {
        DispCWSpeed();
    }
}

// Callback: PTT State Changed
void Engine_Ptt_Callback(bool on) {
    // This callback should NOT trigger network PTT for local keying.
    // Local keying relies on the radio's VOX or Break-in from the KeyOutPin.
    // It is only used for rigs that need a separate PTT line asserted before keying.
    if (KeyerOut == "LOCAL") {
        // If your setup requires a separate PTT line for a local rig,
        // you would control that GPIO pin here.
        // DoRigPTT() sends a network command, which is incorrect for LOCAL mode.
        // Therefore, we do nothing.
    }
}

// Callback: Key State Changed
void Engine_Key_Callback(bool on) {
  
  // 1. Audio and UI (Always active for practice, regardless of mode)
  KeyDown = on; 
  if (on) {
      if (!OldKeyDown) OldKeyDown = true;
      
      // Sidetone Logic: Should always work if SideTone is enabled
      if (SideTone) { 
          unsigned int safeFreq = (STFreq < 200) ? 800 : STFreq;
          tone(LOCAL_STPin, safeFreq);
      } 
  } else {
      if (OldKeyDown) OldKeyDown = false;
      noTone(LOCAL_STPin);
  }

  // Handle LOCAL physical keying (Local OR Both)
  if (KeyerOut == "LOCAL" || KeyerOut == "BOTH") {
      // Toggle the physical keying pin.
      digitalWrite(LOCAL_KeyOutPin, on ? HIGH : LOW);
  }

  // Handle ETHERNET network keying (Ethernet OR Both)
  if (KeyerOut == "ETHERNET" || KeyerOut == "BOTH") {
      // For ETHERNET, we must be connected and in the correct mode to send commands.
      if (g_fRig && g_fRig->connected) {
          int txSlice = -1;
          for (int s = 0; s < g_fRig->nMaxSlice; ++s) {
              if (g_fRig->slice[s].tx == 1 && g_fRig->slice[s].in_use == 1) {
                  txSlice = s;
                  break;
              }
          }

          // Safety Guard: Only send network command if in CW mode with break-in enabled.
          // If conditions are not met, we do nothing (sidetone continues for practice).
          if (txSlice >= 0 && g_fRig->slice[txSlice].mode == "CW" && g_fRig->transmit.break_in == 1) {
              char buf[128];
              int keyState = on ? 1 : 0;
              snprintf(buf, sizeof(buf), "cw key %d time=0x%X index=%u client_handle=%s", 
                  keyState,
                  (unsigned)(millis() % 0xFFFF), 
                  (unsigned)CWIndex++, 
                  g_fRig->Client_Handle[ClientMenuItem].c_str());

              g_fRig->send(buf);
          }
      }
  }

}

// Callback: Character Sent (Echo back to host)
void Engine_CharSent_Callback(char c) {
  WK_OnCharEcho((uint8_t)c);
}

// Callback: Paddle Activity
void Engine_Paddle_Callback(bool active) {
    WK_OnPaddleActivity(active);
}

// ============================================================
// PUBLIC KEYER API
// ============================================================

void KeyerSetup() {
  // 1. Configure Hardware Pins
  pinMode(DotPin, INPUT_PULLUP);
  pinMode(DashPin, INPUT_PULLUP);
  pinMode(LOCAL_StraightKeyPin, INPUT_PULLUP);
  
  pinMode(LOCAL_KeyOutPin, OUTPUT);
  pinMode(LOCAL_STPin, OUTPUT);

  digitalWriteFast(LOCAL_KeyOutPin, LOW); 

  g_KeyerTimingActive = false;
  
  // 2. Initialize the Engine
  g_keyerEngine.begin();
  
  // 3. Attach Callbacks (Now local to this file!)
  g_keyerEngine.attachKeyCallback(Engine_Key_Callback);
  g_keyerEngine.attachPttCallback(Engine_Ptt_Callback);
  g_keyerEngine.attachWpmChangeCallback(Engine_Wpm_Callback);
  g_keyerEngine.attachCharSentCallback(Engine_CharSent_Callback);
  g_keyerEngine.attachPaddleActivityCallback(Engine_Paddle_Callback); 

  // 4. Set Initial Speed
  if (WPM < 5) WPM = 20;
  g_keyerEngine.setWpm((uint8_t)WPM);

  // 5. Set Keyer Mode
  if      (KeyMode == "A") g_keyerEngine.setMode(KeyerMode::IAMBIC_A);
  else if (KeyMode == "B") g_keyerEngine.setMode(KeyerMode::IAMBIC_B);
  else if (KeyMode == "U") g_keyerEngine.setMode(KeyerMode::ULTIMATIC);
  else if (KeyMode == "S") g_keyerEngine.setMode(KeyerMode::BUG);
  else if (KeyMode == "C") g_keyerEngine.setMode(KeyerMode::SINGLE_PADDLE);
  else                     g_keyerEngine.setMode(KeyerMode::IAMBIC_B);

  // 6. Set Pro Features
  g_keyerEngine.setCompensation((uint8_t)KeyerCompensation);
  g_keyerEngine.setFirstExtension((uint8_t)KeyerFirstExtension);
  g_keyerEngine.setFarnsworth((uint8_t)KeyerFarnsworth);
  g_keyerEngine.setAutospace(KeyerAutospace);
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
  // Stub
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
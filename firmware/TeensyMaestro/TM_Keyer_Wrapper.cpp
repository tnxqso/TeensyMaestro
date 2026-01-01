/*
  TM_Keyer_Wrapper.cpp

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

// Tracks the last speed set by the Host to prevent echo loops
int g_lastHostWpm = -1;

// --- INTERRUPT SAFETY: NETWORK KEYING BUFFER ---
// We use a small Ring Buffer to pass key events from the fast ISR
// to the slower Main Loop without missing edges.
struct NetKeyEvt {
    uint8_t state;      // 1=Down, 0=Up
    uint16_t timestamp; // millis() % 0xFFFF
};

static const int NET_KEY_BUF_SIZE = 64; // Increased slightly for safety
volatile NetKeyEvt g_netKeyBuf[NET_KEY_BUF_SIZE];
volatile int g_netKeyHead = 0;
volatile int g_netKeyTail = 0;

// Helper to push to buffer (Runs in ISR)
inline void pushNetKey(bool on) {
    int next = (g_netKeyHead + 1) % NET_KEY_BUF_SIZE;
    if (next != g_netKeyTail) { // Check for overflow
        g_netKeyBuf[g_netKeyHead].state = on ? 1 : 0;
        g_netKeyBuf[g_netKeyHead].timestamp = (uint16_t)(millis() & 0xFFFF);
        g_netKeyHead = next;
    }
}

// ============================================================
// ENGINE CALLBACKS
// ============================================================

// Callback: WPM Changed by Macro/Host
void Engine_Wpm_Callback(uint8_t newWpm) {
    newWpm = TMU_ClampWpm(newWpm);
    g_lastHostWpm = newWpm;
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
    // No action for LOCAL mode PTT here.
}

// Callback: Key State Changed (CALLED FROM INTERRUPT)
void Engine_Key_Callback(bool on) {
  
  // 1. Audio and UI (Always active for practice)
  KeyDown = on; 
  if (on) {
      if (!OldKeyDown) OldKeyDown = true;
      if (SideTone) { 
          unsigned int safeFreq = (STFreq < 200) ? 800 : STFreq;
          tone(LOCAL_STPin, safeFreq);
      } 
  } else {
      if (OldKeyDown) OldKeyDown = false;
      noTone(LOCAL_STPin);
  }

  // Handle LOCAL physical keying
  if (KeyerOut == "LOCAL" || KeyerOut == "BOTH") {
      digitalWrite(LOCAL_KeyOutPin, on ? HIGH : LOW);
  }

  // Handle ETHERNET network keying
  if (KeyerOut == "ETHERNET" || KeyerOut == "BOTH") {
      // Buffer the event for the main loop to process
      pushNetKey(on);
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
  pinMode(DotPin, INPUT_PULLUP);
  pinMode(DashPin, INPUT_PULLUP);
  pinMode(LOCAL_StraightKeyPin, INPUT_PULLUP);
  
  pinMode(LOCAL_KeyOutPin, OUTPUT);
  pinMode(LOCAL_STPin, OUTPUT);

  digitalWriteFast(LOCAL_KeyOutPin, LOW); 

  g_KeyerTimingActive = false;
  
  g_keyerEngine.begin();
  
  g_keyerEngine.attachKeyCallback(Engine_Key_Callback);
  g_keyerEngine.attachPttCallback(Engine_Ptt_Callback);
  g_keyerEngine.attachWpmChangeCallback(Engine_Wpm_Callback);
  g_keyerEngine.attachCharSentCallback(Engine_CharSent_Callback);
  g_keyerEngine.attachPaddleActivityCallback(Engine_Paddle_Callback); 

  if (WPM < 5) WPM = 20;
  g_keyerEngine.setWpm((uint8_t)WPM);

  if      (KeyMode == "A") g_keyerEngine.setMode(KeyerMode::IAMBIC_A);
  else if (KeyMode == "B") g_keyerEngine.setMode(KeyerMode::IAMBIC_B);
  else if (KeyMode == "U") g_keyerEngine.setMode(KeyerMode::ULTIMATIC);
  else if (KeyMode == "S") g_keyerEngine.setMode(KeyerMode::BUG);
  else if (KeyMode == "C") g_keyerEngine.setMode(KeyerMode::SINGLE_PADDLE);
  else                     g_keyerEngine.setMode(KeyerMode::IAMBIC_B);

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

  if (!preserveBaseline && newWpm == g_lastHostWpm) {
      CWVal = newWpm;
      WPM = newWpm;
      if (Encoder_9 == Enc9_CWSpeed) {
        CWMicEnc.write(CWVal * CWEncSteps);
      }
      return; 
  }

  if (!preserveBaseline && newWpm != g_lastHostWpm) {
       g_lastHostWpm = -1; 
  }

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

// THIS IS CALLED FROM MAIN LOOP (SAFE FOR NETWORK)
void KeyerLoop() {
  // Process all pending events in buffer
  while (g_netKeyHead != g_netKeyTail) {
      
      // FIX: Read fields individually to avoid "volatile" copy-constructor error
      uint8_t  evtState = g_netKeyBuf[g_netKeyTail].state;
      uint16_t evtTime  = g_netKeyBuf[g_netKeyTail].timestamp;
      
      // Advance tail
      g_netKeyTail = (g_netKeyTail + 1) % NET_KEY_BUF_SIZE;

      if (g_fRig && g_fRig->connected) {
          int txSlice = -1;
          for (int s = 0; s < g_fRig->nMaxSlice; ++s) {
              if (g_fRig->slice[s].tx == 1 && g_fRig->slice[s].in_use == 1) {
                  txSlice = s;
                  break;
              }
          }

          if (txSlice >= 0 && g_fRig->slice[txSlice].mode == "CW" && g_fRig->transmit.break_in == 1) {
              char buf[128];
              snprintf(buf, sizeof(buf), "cw key %d time=0x%X index=%u client_handle=%s", 
                  evtState,
                  evtTime, 
                  (unsigned)CWIndex++, 
                  g_fRig->Client_Handle[ClientMenuItem].c_str());

              g_fRig->send(buf);
          }
      }
  }
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
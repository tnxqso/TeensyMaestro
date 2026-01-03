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

// External C-hook implementation for Safe Tune Stop
// Defined here to ensure LOCAL_STPin is visible
extern "C" void Keyer_StopBeep() {
    noTone(LOCAL_STPin);
}

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
// FIX: volatile to prevent ISR data race
volatile int g_lastHostWpm = -1;

// --- INTERRUPT SAFETY GLOBALS ---

// 1. Keyer Output Mode Cache (Integer for ISR safety)
#define KEYOUT_NONE     0
#define KEYOUT_LOCAL    1
#define KEYOUT_ETHERNET 2
#define KEYOUT_BOTH     3
volatile uint8_t g_isrKeyOutMode = KEYOUT_LOCAL; 

// 2. Network Keying Buffer
struct NetKeyEvt {
    uint8_t state;      // 1=Down, 0=Up
    uint16_t timestamp; // millis() % 0xFFFF
};

static const int NET_KEY_BUF_SIZE = 64; 
// Enforce power-of-two for safe bitmasking logic
static_assert((NET_KEY_BUF_SIZE & (NET_KEY_BUF_SIZE - 1)) == 0, "NET_KEY_BUF_SIZE must be power of two");

volatile NetKeyEvt g_netKeyBuf[NET_KEY_BUF_SIZE];
// Use uint8_t for atomic reads/writes on 32-bit architecture without disabling interrupts
volatile uint8_t g_netKeyHead = 0;
volatile uint8_t g_netKeyTail = 0;

// 3. Deferred WPM Update
volatile bool g_wpmUpdatePending = false;
volatile uint8_t g_pendingWpmVal = 0;

// 4. Deferred Sidetone (Fix for tone() in ISR)
volatile bool g_sidetonePending = false;
volatile bool g_sidetoneState   = false;

// 5. Throttle
static const unsigned long MAX_NET_EVENTS_PER_SEC = 40;
static unsigned long g_netThrottleStartMs = 0;
static int g_netEventsCount = 0;

// Helper function to convert String config to Integer (Safe for ISR)
void updateKeyOutMode() {
    if (KeyerOut == "LOCAL")         g_isrKeyOutMode = KEYOUT_LOCAL;
    else if (KeyerOut == "ETHERNET") g_isrKeyOutMode = KEYOUT_ETHERNET;
    else if (KeyerOut == "BOTH")     g_isrKeyOutMode = KEYOUT_BOTH;
    else                             g_isrKeyOutMode = KEYOUT_NONE;
}

// Helper to push to buffer (Runs in ISR)
inline void pushNetKey(bool on) {
    // Masking is faster than modulus and safe for power-of-two sizes
    uint8_t next = (uint8_t)((g_netKeyHead + 1) & (NET_KEY_BUF_SIZE - 1));
    if (next != g_netKeyTail) { 
        g_netKeyBuf[g_netKeyHead].state = on ? 1 : 0;
        g_netKeyBuf[g_netKeyHead].timestamp = 0; // Timestamp filled in main loop
        g_netKeyHead = next;
    }
}

// ============================================================
// ENGINE CALLBACKS
// ============================================================

// Callback: WPM Changed by Macro/Host (MAY RUN IN ISR)
void Engine_Wpm_Callback(uint8_t newWpm) {
    newWpm = TMU_ClampWpm(newWpm);
    
    // SAFE: Update ISR state immediately
    g_lastHostWpm = newWpm;
    
    // DEFER UI UPDATES!
    g_pendingWpmVal = newWpm;
    g_wpmUpdatePending = true;
}

// Callback: PTT State Changed
void Engine_Ptt_Callback(bool on) {
    // No action for LOCAL mode PTT here.
    (void)on;
}

// Callback: Key State Changed (CALLED FROM INTERRUPT)
void Engine_Key_Callback(bool on) {
  
  KeyDown = on; 
  if (on) {
      if (!OldKeyDown) OldKeyDown = true;
  } else {
      if (OldKeyDown) OldKeyDown = false;
  }

  // FIX: Always update pending state.
  // This ensures tone stops if SideTone is disabled mid-key.
  g_sidetoneState   = (on && SideTone);
  g_sidetonePending = true;

  uint8_t mode = g_isrKeyOutMode; 

  // Handle LOCAL physical keying
  if (mode == KEYOUT_LOCAL || mode == KEYOUT_BOTH) {
      // Use digitalWriteFast for lower ISR latency
      digitalWriteFast(LOCAL_KeyOutPin, on ? HIGH : LOW);
  }

  // Handle ETHERNET network keying
  if (mode == KEYOUT_ETHERNET || mode == KEYOUT_BOTH) {
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
  
  // Update the ISR-safe mode flag
  updateKeyOutMode();

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

  // Snapshot volatile variable for safe comparison
  int lastWpm = g_lastHostWpm;
  
  // FIX: Even if we suppress host echo, we MUST update internal state & engine.
  // We only skip the "setLocalBaseline" call to prevent the feedback loop.
  
  bool isEcho = (!preserveBaseline && newWpm == lastWpm);

  if (!preserveBaseline && newWpm != lastWpm) {
       g_lastHostWpm = -1; 
  }

  CWVal = newWpm;
  
  // Only report back to host if it's NOT a direct echo of what host just sent
  if (!preserveBaseline && !isEcho) {
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

// THIS IS CALLED FROM MAIN LOOP (SAFE FOR NETWORK & UI)
void KeyerLoop() {
  
  // 0. Sync Keyer Mode (in case it changed via Menu)
  updateKeyOutMode();

  // 1. Process Deferred Sidetone (Safe Context)
  if (g_sidetonePending) {
      g_sidetonePending = false;
      if (g_sidetoneState) {
          unsigned int safeFreq = (STFreq < 200) ? 800 : STFreq;
          tone(LOCAL_STPin, safeFreq);
      } else {
          noTone(LOCAL_STPin);
      }
  }

  // 2. Process Deferred WPM Updates (Safe UI Update)
  if (g_wpmUpdatePending) {
      g_wpmUpdatePending = false;
      int val = g_pendingWpmVal;
      
      // Update remaining state variables here
      // (g_lastHostWpm was already set in ISR for immediate safety)
      CWVal = val;
      WPM = val;

      if (Encoder_9 == Enc9_CWSpeed) {
          CWMicEnc.write(val * CWEncSteps);
      }
      
      if (Encoder_9 == Enc9_CWSpeed || Encoder_9 == Enc9_RFPower || Encoder_9 == Enc9_Band) {
          DispCWSpeed();
      }
  }

  // 3. Network Keying with "Spam Filter" (Throttle)
  unsigned long now = millis();
  if (now - g_netThrottleStartMs >= 1000) {
      g_netThrottleStartMs = now;
      g_netEventsCount = 0;
  }

  // Use local copy of head for atomic consumption check
  uint8_t head;
  while ((head = g_netKeyHead) != g_netKeyTail) {
      
      uint8_t  evtState = g_netKeyBuf[g_netKeyTail].state;
      uint16_t evtTime  = g_netKeyBuf[g_netKeyTail].timestamp;
      
      // Fix timestamp here in main loop if it was deferred
      if (evtTime == 0) {
          evtTime = (uint16_t)(now & 0xFFFF);
      }
      
      // Advance tail using bitmask
      g_netKeyTail = (uint8_t)((g_netKeyTail + 1) & (NET_KEY_BUF_SIZE - 1));

      if (g_netEventsCount >= MAX_NET_EVENTS_PER_SEC) {
          continue; 
      }

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
              g_netEventsCount++;
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
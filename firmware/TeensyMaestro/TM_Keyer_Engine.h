/*
  TM_Keyer_Engine.h

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
#include <stdint.h>

#ifndef TM_KEYER_QUEUE_SIZE
#define TM_KEYER_QUEUE_SIZE 256
#endif

// --- Event Types ---
enum class KeyerEventType : uint8_t {
  NONE = 0,
  CHAR,           
  SET_WPM,        
  SET_PTT,        
  WAIT_MS,        
  PROSIGN_START,  
  PROSIGN_END     
};

struct KeyerEvent {
  KeyerEventType type;
  uint16_t       value; 
};

// --- Keyer Modes ---
enum class KeyerMode : uint8_t {
  IAMBIC_B,       
  IAMBIC_A,       
  ULTIMATIC,      
  BUG,            
  SINGLE_PADDLE   
};

// --- Callbacks ---
typedef void (*KeyerStateCallback)(bool active);
typedef void (*PttStateCallback)(bool active);
typedef void (*WpmChangeCallback)(uint8_t wpm);
typedef void (*CharSentCallback)(char c);

class TM_Keyer_Engine {
public:
  TM_Keyer_Engine();

  void begin();
  void poll();

  // --- Queue ---
  bool enqueue(KeyerEvent evt);
  bool enqueueChar(char c);
  bool enqueueWpm(uint8_t wpm);
  bool enqueuePtt(bool on);
  bool enqueueWait(uint16_t ms);

  void abortNow();
  void clearQueue();

  // --- Inputs ---
  void updatePaddles(bool dotPressed, bool dashPressed);
  void setStraightKey(bool pressed);
  void onStraightKeyChange(bool pressed);

  // --- Configuration ---
  void setWpm(uint8_t wpm);
  uint8_t getWpm() const { return _wpm; }
  
  void setWeighting(uint8_t weight); 
  void setRatio(uint8_t ratio);
  void setMode(KeyerMode mode);
  
  void setPttLeadTail(uint16_t leadMs, uint16_t tailMs);
  
  // New Pro Features
  void setFarnsworth(uint8_t wpm);       // 0 to disable
  void setCompensation(uint8_t ms);      // Added to KeyDown, subtracted from Space
  void setFirstExtension(uint8_t ms);    // Added to very first element of a sequence
  void setAutospace(bool active);        // Enforce char spacing in manual mode
  void setTune(bool active);             // Constant carrier (with timeout)

  // --- Callbacks ---
  void attachKeyCallback(KeyerStateCallback cb) { _cbKey = cb; }
  void attachPttCallback(PttStateCallback cb)   { _cbPtt = cb; }
  void attachWpmChangeCallback(WpmChangeCallback cb) { _cbWpm = cb; }
  void attachCharSentCallback(CharSentCallback cb)   { _cbChar = cb; }

  // --- Status ---
  bool isBusy() const;
  bool isTransmitting() const;
  uint16_t getQueueSize() const;

private:
  enum class State : uint8_t {
    IDLE,
    PTT_LEAD_DELAY,
    START_ELEMENT,
    TRANSMITTING_ELEMENT,
    ELEMENT_SPACE,
    INTER_CHAR_SPACE,
    INTER_WORD_SPACE,
    BUFFERED_WAIT,
    PTT_TAIL_DELAY,
    TUNE_ACTIVE        // New State for Tune Mode
  };

  State _state = State::IDLE;

  // Config
  uint8_t  _wpm = 20;
  uint8_t  _farnsworthWpm = 0;   // 0 = Disabled
  uint8_t  _weight = 50;
  float    _ratio = 3.0f;
  uint16_t _pttLeadMs = 0;
  uint16_t _pttTailMs = 0;
  uint8_t  _compensationMs = 0;
  uint8_t  _firstExtensionMs = 0;
  bool     _autospace = false;
  KeyerMode _mode = KeyerMode::IAMBIC_B;

  // Runtime State
  uint32_t _nextEventMicros = 0;
  uint32_t _calculatedSpaceMicros = 0; 
  uint32_t _tuneStartMs = 0;      // Safety timeout for tune
  
  bool     _pttActive = false;
  bool     _keyActive = false;
  bool     _inProsign = false;
  bool     _straightKeyActive = false;
  bool     _isManualMode = false;
  bool     _isFirstElement = true; // Tracks if we are starting a new sequence (for Extension)

  uint8_t  _currentMorseCode = 0;
  uint8_t  _currentMorseLen = 0;
  
  bool _paddleDot = false;
  bool _paddleDash = false;
  bool _paddleMemoryDot = false; 
  bool _paddleMemoryDash = false; 
  bool _lastElementWasDot = false;
  bool _ultimaticPriorityDot = false;
  uint32_t _lastPaddleReleaseMicros = 0; // For Autospace timing

  KeyerEvent _queue[TM_KEYER_QUEUE_SIZE];
  uint16_t   _head = 0;
  uint16_t   _tail = 0;

  KeyerStateCallback _cbKey = nullptr;
  PttStateCallback   _cbPtt = nullptr;
  WpmChangeCallback  _cbWpm = nullptr;
  CharSentCallback   _cbChar = nullptr;

  void setKey(bool on);
  void setPtt(bool on);
  void processQueue();
  void checkPaddles(); 
  void startElement(bool isDash);
  
  uint32_t getSafeStartTime();
  // Helper: useFarnsworth=true calculates dot length based on fast WPM
  uint32_t calculateDotMicros(bool useFarnsworth = false) const;
  void lookupMorse(char c, uint8_t &code, uint8_t &len);
};
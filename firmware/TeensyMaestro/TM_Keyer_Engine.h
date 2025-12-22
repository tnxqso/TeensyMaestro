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

enum class KeyerMode : uint8_t {
  IAMBIC_B,       
  IAMBIC_A,       
  ULTIMATIC,      
  BUG,            
  SINGLE_PADDLE   
};

typedef void (*KeyerStateCallback)(bool active);
typedef void (*PttStateCallback)(bool active);
typedef void (*WpmChangeCallback)(uint8_t wpm);
typedef void (*CharSentCallback)(char c);
typedef void (*PaddleActivityCallback)(bool active); // Callback for DXLog break-in detection

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

  // Helper to check if paddles are physically pressed (for status byte)
  bool isPaddleActive() const; 

  // --- Configuration ---
  void setWpm(uint8_t wpm);
  uint8_t getWpm() const { return _wpm; }
  
  void setWeighting(uint8_t weight); 
  void setRatio(uint8_t ratio);
  void setMode(KeyerMode mode);
  
  void setPttLeadTail(uint16_t leadMs, uint16_t tailMs);
  void setFarnsworth(uint8_t wpm); 
  void setCompensation(uint8_t ms);
  void setFirstExtension(uint8_t ms);
  void setAutospace(bool active);
  void setTune(bool active);

  // --- Callbacks ---
  void attachKeyCallback(KeyerStateCallback cb) { _cbKey = cb; }
  void attachPttCallback(PttStateCallback cb)   { _cbPtt = cb; }
  void attachWpmChangeCallback(WpmChangeCallback cb) { _cbWpm = cb; }
  void attachCharSentCallback(CharSentCallback cb)   { _cbChar = cb; }
  void attachPaddleActivityCallback(PaddleActivityCallback cb) { _cbPaddle = cb; }

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
    TUNE_ACTIVE
  };

  State _state = State::IDLE;

  // Config
  uint8_t  _wpm = 20;
  uint8_t  _farnsworthWpm = 0;
  uint8_t  _weight = 50;
  float    _ratio = 3.0f;
  uint16_t _pttLeadMs = 0;
  uint16_t _pttTailMs = 0;
  uint8_t  _compensationMs = 0;
  uint8_t  _firstExtensionMs = 0;
  bool     _autospace = false;
  KeyerMode _mode = KeyerMode::IAMBIC_B;

  // Runtime
  uint32_t _nextEventMicros = 0;
  uint32_t _calculatedSpaceMicros = 0; 
  uint32_t _tuneStartMs = 0;
  
  bool     _pttActive = false;
  bool     _keyActive = false;
  bool     _inProsign = false;
  bool     _straightKeyActive = false;
  bool     _isManualMode = false;   // True if sending via paddles, False if via queue
  bool     _isFirstElement = true;

  uint8_t  _currentMorseCode = 0;
  uint8_t  _currentMorseLen = 0;
  
  bool _paddleDot = false;
  bool _paddleDash = false;
  bool _paddleMemoryDot = false; 
  bool _paddleMemoryDash = false; 
  bool _lastElementWasDot = false;
  bool _ultimaticPriorityDot = false;
  uint32_t _lastPaddleReleaseMicros = 0;

  KeyerEvent _queue[TM_KEYER_QUEUE_SIZE];
  uint16_t   _head = 0;
  uint16_t   _tail = 0;

  KeyerStateCallback _cbKey = nullptr;
  PttStateCallback   _cbPtt = nullptr;
  WpmChangeCallback  _cbWpm = nullptr;
  CharSentCallback   _cbChar = nullptr;
  PaddleActivityCallback _cbPaddle = nullptr;

  void setKey(bool on);
  void setPtt(bool on);
  void processQueue();
  void checkPaddles(); 
  void startElement(bool isDash);
  
  uint32_t getSafeStartTime();
  uint32_t calculateDotMicros(bool useFarnsworth = false) const;
  void lookupMorse(char c, uint8_t &code, uint8_t &len);
};
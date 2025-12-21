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

// Max number of pending events (characters + commands) in the buffer
#ifndef TM_KEYER_QUEUE_SIZE
#define TM_KEYER_QUEUE_SIZE 256
#endif

// --- Event Types for the Unified Queue ---
enum class KeyerEventType : uint8_t {
  NONE = 0,
  CHAR,           // ASCII character to send
  SET_WPM,        // Buffered WPM change
  SET_PTT,        // Buffered PTT On/Off
  WAIT_MS,        // Buffered Wait (milliseconds)
  PROSIGN_START,  // Internal: Start of a prosign (merge characters)
  PROSIGN_END     // Internal: End of a prosign
};

// --- Single Event Structure ---
struct KeyerEvent {
  KeyerEventType type;
  uint16_t       value; 
};

// --- Callback Signatures ---
// We use callbacks to decouple the engine from hardware/FlexRig
typedef void (*KeyerStateCallback)(bool active);    // For KEY OUT (High/Low)
typedef void (*PttStateCallback)(bool active);      // For PTT OUT
typedef void (*WpmChangeCallback)(uint8_t wpm);     // To notify UI of WPM changes from macros

class TM_Keyer_Engine {
public:
  TM_Keyer_Engine();

  // Initialization
  void begin();

  // Main processing loop - call this as often as possible (e.g. from loop())
  // It uses micros() internally to handle non-blocking timing.
  void poll();

  // --- Input Queue (WinKeyer / Macros) ---
  
  // Enqueue a character or command. Returns true if successful, false if queue full.
  bool enqueue(KeyerEvent evt);
  
  // Helpers for common operations
  bool enqueueChar(char c);
  bool enqueueWpm(uint8_t wpm);
  bool enqueuePtt(bool on);
  bool enqueueWait(uint16_t ms);

  // Immediate abort (ESC). Clears queue and stops transmission immediately.
  void abortNow();

  // --- Paddle / Manual Input ---
  // Call these from ISRs or polling loops
  void onPaddleChange(bool dotPressed, bool dashPressed);
  void onStraightKeyChange(bool pressed);

  // --- Configuration (Immediate) ---
  void setWpm(uint8_t wpm);
  uint8_t getWpm() const { return _wpm; }
  
  void setWeighting(uint8_t weight); // 50 = standard 1:1 dot:space ratio within element
  void setRatio(uint8_t ratio);      // Dit/Dah ratio (standard 3.0)
  
  void setPttLeadTail(uint16_t leadMs, uint16_t tailMs);
  void setFarnsworth(uint8_t wpm); // 0 = disabled

  // --- Hardware Callbacks ---
  void attachKeyCallback(KeyerStateCallback cb) { _cbKey = cb; }
  void attachPttCallback(PttStateCallback cb)   { _cbPtt = cb; }
  void attachWpmChangeCallback(WpmChangeCallback cb) { _cbWpm = cb; }

  // --- Status Inspection ---
  bool isBusy() const;        // Returns true if sending or buffer has data
  bool isTransmitting() const; // Returns true only if actively keying or waiting inter-element
  uint16_t getQueueSize() const;

private:
  // Internal State Machine
  enum class State : uint8_t {
    IDLE,
    PTT_LEAD_DELAY,
    START_ELEMENT,
    TRANSMITTING_ELEMENT,
    ELEMENT_SPACE,
    INTER_CHAR_SPACE,
    INTER_WORD_SPACE,
    BUFFERED_WAIT,
    PTT_TAIL_DELAY
  };

  State _state = State::IDLE;

  // Configuration
  uint8_t  _wpm = 20;
  uint8_t  _weight = 50;
  float    _ratio = 3.0f;
  uint16_t _pttLeadMs = 0;
  uint16_t _pttTailMs = 0;

  // Runtime State
  uint32_t _nextEventMicros = 0; // Timestamp when next state transition occurs
  bool     _pttActive = false;
  bool     _keyActive = false;
  bool     _inProsign = false;   // If true, gap between chars is ElementSpace (1 unit) instead of 3

  // Current Element Processing
  uint8_t  _currentMorseCode = 0; // Bitmask for current char
  uint8_t  _currentMorseLen = 0;  // Number of elements left
  
  // Ring Buffer
  KeyerEvent _queue[TM_KEYER_QUEUE_SIZE];
  uint16_t   _head = 0;
  uint16_t   _tail = 0;

  // Callbacks
  KeyerStateCallback _cbKey = nullptr;
  PttStateCallback   _cbPtt = nullptr;
  WpmChangeCallback  _cbWpm = nullptr;

  // Internal Helpers
  void setKey(bool on);
  void setPtt(bool on);
  void processQueue();
  void startElement(bool isDash);
  uint32_t calculateDotMicros() const;
  void lookupMorse(char c, uint8_t &code, uint8_t &len);
};
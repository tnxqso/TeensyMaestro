/*
  TM_Keyer_Engine.cpp

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

#include "TM_Keyer_Engine.h"
#include <ctype.h> 

// Set to 0 to stop log spam, set to 1 to debug
#undef WK_INFO_TRACE
#define WK_INFO_TRACE 0

// Morse Table (LSB = First Element)
static const struct { uint8_t len; uint8_t code; } MorseTable[] = {
    {0, 0},     // Space
    {0, 0},     // !
    {6, 0x12},  // "
    {0, 0},     // #
    {0, 0},     // $
    {0, 0},     // %
    {0, 0},     // &
    {6, 0x1E},  // '
    {5, 0x2D},  // (
    {6, 0x6D},  // )
    {0, 0},     // *
    {5, 0x0A},  // +
    {6, 0x33},  // ,
    {6, 0x21},  // -
    {6, 0x15},  // .
    {5, 0x12},  // /
    {5, 0x1F},  // 0
    {5, 0x0F},  // 1
    {5, 0x07},  // 2
    {5, 0x03},  // 3
    {5, 0x01},  // 4
    {5, 0x00},  // 5
    {5, 0x10},  // 6
    {5, 0x18},  // 7
    {5, 0x1C},  // 8
    {5, 0x1E},  // 9
    {6, 0x07},  // :
    {6, 0x15},  // ;
    {0, 0},     // <
    {5, 0x11},  // =
    {0, 0},     // >
    {6, 0x0C},  // ?
    {5, 0x1A},  // @
    {2, 0x02},  // A
    {4, 0x01},  // B
    {4, 0x05},  // C
    {3, 0x01},  // D
    {1, 0x00},  // E
    {4, 0x04},  // F
    {3, 0x03},  // G
    {4, 0x00},  // H
    {2, 0x00},  // I
    {4, 0x0E},  // J
    {3, 0x05},  // K
    {4, 0x02},  // L
    {2, 0x03},  // M
    {2, 0x01},  // N
    {3, 0x07},  // O
    {4, 0x06},  // P
    {4, 0x0B},  // Q
    {3, 0x02},  // R
    {3, 0x00},  // S
    {1, 0x01},  // T
    {3, 0x04},  // U
    {4, 0x08},  // V
    {3, 0x06},  // W
    {4, 0x09},  // X
    {4, 0x0D},  // Y
    {4, 0x03},  // Z
    {0, 0},     // [
    {0, 0},     // Backslash
    {0, 0},     // ]
    {0, 0},     // ^
    {0, 0},     // _
};

TM_Keyer_Engine::TM_Keyer_Engine() {
}

void TM_Keyer_Engine::begin() {
  _state = State::IDLE;
  _head = 0;
  _tail = 0;
  _pttActive = false;
  _keyActive = false;
  setKey(false);
  setPtt(false);
  
  _paddleDot = false;
  _paddleDash = false;
  _paddleMemoryDot = false;
  _paddleMemoryDash = false;
  _lastElementWasDot = false;
  _straightKeyActive = false;
  _ultimaticPriorityDot = false;
}

// --- Inputs ---

void TM_Keyer_Engine::updatePaddles(bool dotPressed, bool dashPressed) {
  
  // Track priority for Ultimatic mode (Last Pressed Wins)
  if (dotPressed && !_paddleDot) _ultimaticPriorityDot = true;
  if (dashPressed && !_paddleDash) _ultimaticPriorityDot = false;

  _paddleDot = dotPressed;
  _paddleDash = dashPressed;

  #if WK_INFO_TRACE
    static bool _d = false;
    static bool _h = false;
    if (_d != dotPressed || _h != dashPressed) {
       Serial.print("ENG: Paddles Dot="); Serial.print(dotPressed);
       Serial.print(" Dash="); Serial.println(dashPressed);
       _d = dotPressed; _h = dashPressed;
    }
  #endif

  // Break-in Logic
  bool queueBusy = (_head != _tail) || (_currentMorseLen > 0);
  
  if (queueBusy && (dotPressed || dashPressed)) {
      #if WK_INFO_TRACE
        Serial.println("ENG: Paddle Break-in! Aborting buffer.");
      #endif
      abortNow(); 
  }

  // Squeeze Memory Logic
  // Only memorize the OPPOSITE paddle during transmission (Prevent Self-Squeeze)
  if (_state == State::TRANSMITTING_ELEMENT) {
      if (_lastElementWasDot) {
          if (dashPressed) _paddleMemoryDash = true;
      } else {
          if (dotPressed) _paddleMemoryDot = true;
      }
  }
}

void TM_Keyer_Engine::setStraightKey(bool pressed) {
    if (pressed) {
        _straightKeyActive = true;
        if (!_pttActive) setPtt(true);
        if (!_keyActive) setKey(true);
    } else {
        if (_straightKeyActive) {
            setKey(false);
            setPtt(false); 
            _straightKeyActive = false;
        }
    }
}

void TM_Keyer_Engine::onStraightKeyChange(bool pressed) {
    setStraightKey(pressed);
}

// --- Queue ---

bool TM_Keyer_Engine::enqueue(KeyerEvent evt) {
  uint16_t next = (_head + 1) % TM_KEYER_QUEUE_SIZE;
  if (next == _tail) return false;
  _queue[_head] = evt;
  _head = next;
  return true;
}

bool TM_Keyer_Engine::enqueueChar(char c) {
  return enqueue({KeyerEventType::CHAR, (uint16_t)c});
}

bool TM_Keyer_Engine::enqueueWpm(uint8_t wpm) {
  return enqueue({KeyerEventType::SET_WPM, (uint16_t)wpm});
}

bool TM_Keyer_Engine::enqueuePtt(bool on) {
  return enqueue({KeyerEventType::SET_PTT, (uint16_t)(on ? 1 : 0)});
}

bool TM_Keyer_Engine::enqueueWait(uint16_t ms) {
  return enqueue({KeyerEventType::WAIT_MS, ms});
}

uint16_t TM_Keyer_Engine::getQueueSize() const {
  if (_head >= _tail) return _head - _tail;
  return (TM_KEYER_QUEUE_SIZE - _tail) + _head;
}

void TM_Keyer_Engine::abortNow() {
  _head = _tail = 0;
  _currentMorseLen = 0;
  _inProsign = false;
  _nextEventMicros = 0;
  
  if (_keyActive) setKey(false);
  if (_pttActive) setPtt(false); 
  
  _state = State::IDLE;
}

void TM_Keyer_Engine::clearQueue() {
    _head = _tail = 0; 
}

// --- Config ---

void TM_Keyer_Engine::setWpm(uint8_t wpm) {
  if (wpm < 1) wpm = 1;
  if (wpm > 99) wpm = 99;
  if (_wpm != wpm) {
    _wpm = wpm;
    if (_cbWpm) _cbWpm(_wpm);
  }
}

void TM_Keyer_Engine::setMode(KeyerMode mode) {
    _mode = mode;
}

void TM_Keyer_Engine::setWeighting(uint8_t weight) { _weight = weight; }
void TM_Keyer_Engine::setRatio(uint8_t ratio) { 
    _ratio = (float)ratio; 
    if (_ratio < 1.0f) _ratio = 1.0f;
}
void TM_Keyer_Engine::setPttLeadTail(uint16_t leadMs, uint16_t tailMs) {
  _pttLeadMs = leadMs;
  _pttTailMs = tailMs;
}
void TM_Keyer_Engine::setFarnsworth(uint8_t wpm) { (void)wpm; }

// --- Hardware ---

void TM_Keyer_Engine::setKey(bool on) {
  if (_keyActive != on) {
    _keyActive = on;
    #if WK_INFO_TRACE
      Serial.print("ENG: Key "); Serial.println(on ? "DOWN" : "UP");
    #endif
    if (_cbKey) _cbKey(on);
  }
}

void TM_Keyer_Engine::setPtt(bool on) {
  if (_pttActive != on) {
    _pttActive = on;
    #if WK_INFO_TRACE
      Serial.print("ENG: PTT "); Serial.println(on ? "ON" : "OFF");
    #endif
    if (_cbPtt) _cbPtt(on);
  }
}

bool TM_Keyer_Engine::isBusy() const {
  return (_head != _tail) || (_state != State::IDLE);
}

bool TM_Keyer_Engine::isTransmitting() const {
    return _keyActive || (_state == State::TRANSMITTING_ELEMENT) || (_state == State::ELEMENT_SPACE);
}

// --- Logic ---

uint32_t TM_Keyer_Engine::calculateDotMicros() const {
  return 1200000UL / _wpm;
}

void TM_Keyer_Engine::lookupMorse(char c, uint8_t &code, uint8_t &len) {
  c = toupper(c);
  if (c >= ' ' && c <= '_') {
    uint8_t idx = c - ' ';
    len = MorseTable[idx].len;
    code = MorseTable[idx].code;
  } else {
    len = 0; code = 0;
  }
}

// --- Iambic Logic ---
void TM_Keyer_Engine::checkPaddles() {
    
    // Cootie / Single Paddle Mode (Manual Control)
    if (_mode == KeyerMode::SINGLE_PADDLE) {
        bool anyPressed = _paddleDot || _paddleDash;
        if (anyPressed) {
            if (!_pttActive) setPtt(true);
            setKey(true);
        } else {
            setKey(false);
            if (!_straightKeyActive) setPtt(false);
        }
        return; // Skip timing logic
    }

    // Bug Mode (Manual Dash)
    if (_mode == KeyerMode::BUG) {
        if (_paddleDash) {
            // Manual control for Dash
            if (!_pttActive) setPtt(true);
            setKey(true);
            _state = State::IDLE; // Prevent automatic timing from interfering
            return;
        } else {
            // If dash released and we were keying manually, stop
            if (_keyActive && _state == State::IDLE && !_paddleDot) {
                setKey(false);
                // Note: PTT tail logic usually handles the drop
            }
        }
        // Fall through to handle Dots (automatic)
    }

    // --- Automatic Timing Logic (Iambic A/B, Ultimatic, Bug-Dots) ---

    // PTT Logic for manual keying
    if ((_paddleDot || _paddleDash || _paddleMemoryDot || _paddleMemoryDash) && !_pttActive) {
        setPtt(true);
        if (_pttLeadMs > 0) {
            _state = State::PTT_LEAD_DELAY;
            _nextEventMicros = micros() + (_pttLeadMs * 1000UL);
            return;
        }
    }

    bool sendDot = false;
    bool sendDash = false;

    // --- Mode Decision ---

    if (_mode == KeyerMode::ULTIMATIC) {
        if (_paddleDot && _paddleDash) {
            // Both pressed: last one wins
            if (_ultimaticPriorityDot) sendDot = true;
            else sendDash = true;
        } else if (_paddleDot) {
            sendDot = true;
        } else if (_paddleDash) {
            sendDash = true;
        }
    }
    else if (_mode == KeyerMode::IAMBIC_B) {
        if (_paddleMemoryDot || _paddleDot) {
            if (_lastElementWasDot) {
                if (_paddleMemoryDash || _paddleDash) sendDash = true;
                else sendDot = true; 
            } else {
                sendDot = true; 
            }
        } else if (_paddleMemoryDash || _paddleDash) {
            sendDash = true;
        }
    }
    else if (_mode == KeyerMode::IAMBIC_A) {
        // Iambic A does not use memory for squeezed end-of-character
        if (_paddleDot && _paddleDash) {
            // Alternate
            if (_lastElementWasDot) sendDash = true;
            else sendDot = true;
        } else if (_paddleDot) {
            sendDot = true;
        } else if (_paddleDash) {
            sendDash = true;
        }
    }
    else if (_mode == KeyerMode::BUG) {
        // Only Dits are automatic
        if (_paddleDot) sendDot = true;
    }

    // Clear memories (consumed)
    if (sendDot) _paddleMemoryDot = false;
    if (sendDash) _paddleMemoryDash = false;

    // Execute
    if (sendDot) {
        startElement(false);
        _lastElementWasDot = true;
    } else if (sendDash) {
        startElement(true);
        _lastElementWasDot = false;
    } else {
        // Paddles released, handle PTT tail
        if (_pttActive && _state == State::IDLE && !_paddleDot && !_paddleDash) {
             if (_pttTailMs > 0) {
                 _state = State::PTT_TAIL_DELAY;
                 _nextEventMicros = micros() + (_pttTailMs * 1000UL);
             } else {
                 setPtt(false);
             }
        }
    }
}

void TM_Keyer_Engine::poll() {
  uint32_t now = micros();

  if (_nextEventMicros > 0) {
    if ((int32_t)(now - _nextEventMicros) < 0) return;
    _nextEventMicros = 0;
  }

  switch (_state) {
    case State::IDLE:
      checkPaddles();
      if (_state == State::IDLE) {
          processQueue();
      }
      break;

    case State::PTT_LEAD_DELAY:
      _state = State::IDLE;
      break;

    case State::PTT_TAIL_DELAY:
      setPtt(false);
      _state = State::IDLE;
      break;

    case State::BUFFERED_WAIT:
      _state = State::IDLE;
      processQueue();
      break;

    case State::START_ELEMENT:
      if (_currentMorseLen == 0) {
          _state = _inProsign ? State::ELEMENT_SPACE : State::INTER_CHAR_SPACE;
          _nextEventMicros = now + calculateDotMicros() * 3;
          if (_inProsign) _nextEventMicros = now + calculateDotMicros();
      } else {
          bool isDash = (_currentMorseCode & 0x01);
          _currentMorseCode >>= 1;
          _currentMorseLen--;
          startElement(isDash);
      }
      break;

    case State::TRANSMITTING_ELEMENT:
      setKey(false);
      _state = State::ELEMENT_SPACE;
      _nextEventMicros = now + calculateDotMicros();
      break;

    case State::ELEMENT_SPACE:
      if (_currentMorseLen > 0) {
          // Continue buffered character
          _state = State::START_ELEMENT;
          poll(); 
      } else {
          // Iambic A specific cleanup: Clear memories if paddles released during element
          if (_mode == KeyerMode::IAMBIC_A) {
              if (!_paddleDot) _paddleMemoryDot = false;
              if (!_paddleDash) _paddleMemoryDash = false;
          }
          
          _state = State::IDLE;
          checkPaddles(); 
      }
      break;

    case State::INTER_CHAR_SPACE:
    case State::INTER_WORD_SPACE:
      _state = State::IDLE;
      checkPaddles();
      if (_state == State::IDLE) processQueue();
      break;
      
    default:
      break;
  }
}

void TM_Keyer_Engine::startElement(bool isDash) {
  setKey(true);
  _state = State::TRANSMITTING_ELEMENT;
  uint32_t dotLen = calculateDotMicros();
  uint32_t duration = isDash ? (dotLen * (uint32_t)_ratio) : dotLen;
  _nextEventMicros = micros() + duration;
}

void TM_Keyer_Engine::processQueue() {
  if (_head == _tail) {
    if (_pttActive && _state == State::IDLE) {
        if (_pttTailMs > 0) {
            _state = State::PTT_TAIL_DELAY;
            _nextEventMicros = micros() + (_pttTailMs * 1000UL);
        } else {
            setPtt(false);
        }
    }
    return;
  }

  KeyerEvent evt = _queue[_tail];
  
  if (evt.type == KeyerEventType::CHAR) {
    if (!_pttActive) {
      setPtt(true);
      if (_pttLeadMs > 0) {
        _state = State::PTT_LEAD_DELAY;
        _nextEventMicros = micros() + (_pttLeadMs * 1000UL);
        return; 
      }
    }
  }

  _tail = (_tail + 1) % TM_KEYER_QUEUE_SIZE;

  switch (evt.type) {
    case KeyerEventType::SET_WPM:
      setWpm((uint8_t)evt.value);
      processQueue(); 
      break;

    case KeyerEventType::SET_PTT:
      setPtt(evt.value ? true : false);
      processQueue();
      break;

    case KeyerEventType::WAIT_MS:
      _state = State::BUFFERED_WAIT;
      _nextEventMicros = micros() + (evt.value * 1000UL);
      break;

    case KeyerEventType::PROSIGN_START:
      _inProsign = true;
      processQueue();
      break;

    case KeyerEventType::PROSIGN_END:
      _inProsign = false;
      processQueue();
      break;

    case KeyerEventType::CHAR:
      {
        char c = (char)evt.value;
        #if WK_INFO_TRACE
          Serial.print("ENG: Processing '"); Serial.print(c); Serial.println("'");
        #endif
        
        if (_cbChar) _cbChar(c);

        if (c == ' ') {
          _state = State::INTER_WORD_SPACE;
          _nextEventMicros = micros() + (calculateDotMicros() * 4); 
        } else {
          lookupMorse(c, _currentMorseCode, _currentMorseLen);
          if (_currentMorseLen > 0) {
            _state = State::START_ELEMENT;
            startElement((_currentMorseCode & 0x01));
            _currentMorseCode >>= 1;
            _currentMorseLen--;
          } else {
            processQueue();
          }
        }
      }
      break;
    
    default:
      processQueue();
      break;
  }
}
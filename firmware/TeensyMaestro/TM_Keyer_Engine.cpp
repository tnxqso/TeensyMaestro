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
#include <ctype.h> // For toupper()

// Morse Code Lookup Table (Standard)
// Encoding: LSB is first element. 0=Dot, 1=Dash.
// We map ASCII 32 (' ') to 95 ('_')
static const struct { uint8_t len; uint8_t code; } MorseTable[] = {
    {0, 0},     // Space (handled specially)
    {0, 0},     // ! (unsupported usually)
    {6, 0x12},  // " .-..-.
    {0, 0},     // #
    {0, 0},     // $
    {0, 0},     // %
    {0, 0},     // &
    {6, 0x1E},  // ' .----.
    {5, 0x2D},  // ( -.--.
    {6, 0x6D},  // ) -.--.-
    {0, 0},     // * (Prosign marker in some systems, handled externally)
    {5, 0x0A},  // + .-.-. (AR)
    {6, 0x33},  // , --..--
    {6, 0x21},  // - -....-
    {6, 0x15},  // . .-.-.-
    {5, 0x12},  // / -..-.
    {5, 0x3F},  // 0 -----
    {5, 0x2F},  // 1 .----
    {5, 0x27},  // 2 ..---
    {5, 0x23},  // 3 ...--
    {5, 0x21},  // 4 ....-
    {5, 0x20},  // 5 .....
    {5, 0x10},  // 6 -....
    {5, 0x08},  // 7 --...
    {5, 0x04},  // 8 ---..
    {5, 0x02},  // 9 ----.
    {6, 0x07},  // : ---...
    {6, 0x15},  // ; -.-.-.
    {0, 0},     // <
    {5, 0x11},  // = -...- (BT)
    {0, 0},     // >
    {6, 0x0C},  // ? ..--..
    {5, 0x1A},  // @ .--.-. (AC)
    {2, 0x02},  // A .-
    {4, 0x01},  // B -...
    {4, 0x05},  // C -.-.
    {3, 0x01},  // D -..
    {1, 0x00},  // E .
    {4, 0x04},  // F ..-.
    {3, 0x03},  // G --.
    {4, 0x00},  // H ....
    {2, 0x00},  // I ..
    {4, 0x0E},  // J .---
    {3, 0x05},  // K -.-
    {4, 0x02},  // L .-..
    {2, 0x03},  // M --
    {2, 0x01},  // N -.
    {3, 0x07},  // O ---
    {4, 0x06},  // P .--.
    {4, 0x0B},  // Q --.-
    {3, 0x02},  // R .-.
    {3, 0x00},  // S ...
    {1, 0x01},  // T -
    {3, 0x04},  // U ..-
    {4, 0x08},  // V ...-
    {3, 0x06},  // W .--
    {4, 0x09},  // X -..-
    {4, 0x0D},  // Y -.--
    {4, 0x03},  // Z --..
    {0, 0},     // [
    {0, 0},     // \ 
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
}

// --- Queue Management ---

bool TM_Keyer_Engine::enqueue(KeyerEvent evt) {
  uint16_t next = (_head + 1) % TM_KEYER_QUEUE_SIZE;
  if (next == _tail) {
    return false; // Queue Full
  }
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
  // Clear queue
  _head = _tail = 0;
  
  // Hard stop
  setKey(false);
  
  // Release PTT immediately (or maybe respect tail? For abort, immediate is usually best)
  setPtt(false);
  
  _state = State::IDLE;
  _currentMorseLen = 0;
  _inProsign = false;
}

// --- Settings ---

void TM_Keyer_Engine::setWpm(uint8_t wpm) {
  if (wpm < 1) wpm = 1;
  if (wpm > 99) wpm = 99;
  
  if (_wpm != wpm) {
    _wpm = wpm;
    if (_cbWpm) _cbWpm(_wpm);
  }
}

void TM_Keyer_Engine::setWeighting(uint8_t weight) {
  _weight = weight;
}

void TM_Keyer_Engine::setRatio(uint8_t ratio) {
  // WinKey uses integer ratio, e.g. 3 = 3.0
  _ratio = (float)ratio;
  if (_ratio < 1.0f) _ratio = 1.0f;
}

void TM_Keyer_Engine::setPttLeadTail(uint16_t leadMs, uint16_t tailMs) {
  _pttLeadMs = leadMs;
  _pttTailMs = tailMs;
}

void TM_Keyer_Engine::setFarnsworth(uint8_t wpm) {
  // Implementation reserved for future
  (void)wpm;
}

// --- Hardware Abstraction ---

void TM_Keyer_Engine::setKey(bool on) {
  if (_keyActive != on) {
    _keyActive = on;
    if (_cbKey) _cbKey(on);
  }
}

void TM_Keyer_Engine::setPtt(bool on) {
  if (_pttActive != on) {
    _pttActive = on;
    if (_cbPtt) _cbPtt(on);
  }
}

bool TM_Keyer_Engine::isBusy() const {
  return (_state != State::IDLE) || (_head != _tail);
}

bool TM_Keyer_Engine::isTransmitting() const {
    return _keyActive || (_state == State::TRANSMITTING_ELEMENT) || (_state == State::ELEMENT_SPACE);
}

// --- Timing Calculation ---

uint32_t TM_Keyer_Engine::calculateDotMicros() const {
  // Standard: 1200 / WPM = element length in ms
  // Return in micros. 1200000 / WPM.
  // TODO: Add Weighting math here if desired.
  return 1200000UL / _wpm;
}

void TM_Keyer_Engine::lookupMorse(char c, uint8_t &code, uint8_t &len) {
  c = toupper(c);
  if (c >= ' ' && c <= '_') {
    uint8_t idx = c - ' ';
    len = MorseTable[idx].len;
    code = MorseTable[idx].code;
  } else {
    // Unknown char, ignore
    len = 0;
    code = 0;
  }
}

// --- Main Poll Loop ---

void TM_Keyer_Engine::poll() {
  uint32_t now = micros();

  // If we are waiting for a timer to expire, check it
  if (_nextEventMicros > 0) {
    // Handle overflow wrap-around for millis/micros check logic
    if ((int32_t)(now - _nextEventMicros) < 0) {
      return; // Not time yet
    }
    // Timer expired
    _nextEventMicros = 0;
  }

  // State Machine
  switch (_state) {
    
    case State::IDLE:
      processQueue();
      break;

    case State::PTT_LEAD_DELAY:
      // Lead time done, start transmitting
      _state = State::IDLE;
      processQueue(); // Will re-enter and likely trigger CHAR processing
      break;

    case State::PTT_TAIL_DELAY:
      // Tail time done, drop PTT
      setPtt(false);
      _state = State::IDLE;
      break;

    case State::BUFFERED_WAIT:
      // Wait done
      _state = State::IDLE;
      processQueue();
      break;

    case State::START_ELEMENT:
      {
        if (_currentMorseLen == 0) {
          // Character done
          _state = _inProsign ? State::ELEMENT_SPACE : State::INTER_CHAR_SPACE;
          _nextEventMicros = now + calculateDotMicros() * 3; // Standard 3-dot space between chars
          if (_inProsign) _nextEventMicros = now + calculateDotMicros(); // 1-dot space if prosign
          return;
        }

        // Check next element (LSB first in our table)
        bool isDash = (_currentMorseCode & 0x01);
        _currentMorseCode >>= 1;
        _currentMorseLen--;

        startElement(isDash);
      }
      break;

    case State::TRANSMITTING_ELEMENT:
      // Element finished, turn off key
      setKey(false);
      _state = State::ELEMENT_SPACE;
      // Wait 1 dot length (Element Space)
      _nextEventMicros = now + calculateDotMicros();
      break;

    case State::ELEMENT_SPACE:
      // Space between dit/dah finished.
      // Ready for next element in same character
      _state = State::START_ELEMENT;
      // No extra delay, fall through to poll immediately
      poll(); 
      break;

    case State::INTER_CHAR_SPACE:
    case State::INTER_WORD_SPACE:
      // Space finished. Ready for next command/char from queue.
      _state = State::IDLE;
      processQueue();
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
    // Queue empty. 
    // If PTT is active, we might need to start the PTT Tail timer
    if (_pttActive && _pttTailMs > 0 && _state == State::IDLE) {
       _state = State::PTT_TAIL_DELAY;
       _nextEventMicros = micros() + (_pttTailMs * 1000UL);
    } else if (_pttActive && _pttTailMs == 0) {
       // No tail configured, drop immediately if completely idle
       setPtt(false);
    }
    return;
  }

  // Peek at next event
  KeyerEvent evt = _queue[_tail];
  
  // Ensure PTT is ON if we are about to send CHAR
  if (evt.type == KeyerEventType::CHAR) {
    if (!_pttActive) {
      setPtt(true);
      if (_pttLeadMs > 0) {
        _state = State::PTT_LEAD_DELAY;
        _nextEventMicros = micros() + (_pttLeadMs * 1000UL);
        return; // Wait for PTT Lead
      }
    }
  }

  // Consume event
  _tail = (_tail + 1) % TM_KEYER_QUEUE_SIZE;

  switch (evt.type) {
    case KeyerEventType::SET_WPM:
      setWpm((uint8_t)evt.value);
      // Immediately process next item
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
        if (c == ' ') {
          _state = State::INTER_WORD_SPACE;
          // Inter-word is usually 7 dots. We already did 3 dots (INTER_CHAR_SPACE)
          // after the previous char. So we need 4 more.
          _nextEventMicros = micros() + (calculateDotMicros() * 4);
        } else {
          lookupMorse(c, _currentMorseCode, _currentMorseLen);
          if (_currentMorseLen > 0) {
            _state = State::START_ELEMENT;
            // Fall through via recursive poll or next loop
            startElement((_currentMorseCode & 0x01));
            _currentMorseCode >>= 1;
            _currentMorseLen--;
          } else {
            // Unknown char or non-printing, just move on
            processQueue();
          }
        }
      }
      break;
    
    case KeyerEventType::NONE:
    default:
      // Should not happen, just skip
      processQueue();
      break;
  }
}

// Placeholder for Paddle Interface (Manual Mode)
// This will be implemented in Step 2/3 when we merge logic
void TM_Keyer_Engine::onPaddleChange(bool dotPressed, bool dashPressed) {
    // TODO: Implement manual keying logic here.
    // Logic: If queue is NOT empty, manual input is usually ignored or buffered 
    // depending on 'Type-ahead' settings.
    // For now, this new engine is strictly for the WinKeyer queue path.
    (void)dotPressed;
    (void)dashPressed;
}

void TM_Keyer_Engine::onStraightKeyChange(bool pressed) {
    setKey(pressed);
    setPtt(pressed); // Straight key asserts PTT immediately
}
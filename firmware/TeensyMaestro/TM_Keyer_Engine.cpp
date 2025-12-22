/*
  TM_Keyer_Engine.cpp

  TeensyMaestro — Community Edition (CE)
  SPDX-License-Identifier: CC-BY-NC-SA-3.0
  SPDX-FileCopyrightText: 2025 TNX QSO

  A community-maintained edition with open-source utilities
  for ham radio enthusiasts, focusing on FlexRadio® and Wavelog integrations.
*/

#include "TM_Keyer_Engine.h"
#include <ctype.h> 
#include "tm_wk_proto.h"

// Morse Table (LSB = First Element. 0=Dot, 1=Dash)
static const struct { uint8_t len; uint8_t code; } MorseTable[] = {
    {0, 0}, {0, 0}, {6, 0x12}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {6, 0x1E},
    {5, 0x2D}, {6, 0x6D}, {0, 0}, {5, 0x0A}, {6, 0x33}, {6, 0x21}, {6, 0x15}, {5, 0x12},
    {5, 0x1F}, {5, 0x1E}, {5, 0x1C}, {5, 0x18}, {5, 0x10}, 
    {5, 0x00}, {5, 0x01}, {5, 0x03}, {5, 0x07}, {5, 0x0F}, 
    {6, 0x07}, {6, 0x15}, {0, 0}, {5, 0x11}, {0, 0}, {6, 0x0C}, {5, 0x1A},
    {2, 0x02}, {4, 0x01}, {4, 0x05}, {3, 0x01}, {1, 0x00}, {4, 0x04}, {3, 0x03}, {4, 0x00},
    {2, 0x00}, {4, 0x0E}, {3, 0x05}, {4, 0x02}, {2, 0x03}, {2, 0x01}, {3, 0x07}, {4, 0x06},
    {4, 0x0B}, {3, 0x02}, {3, 0x00}, {1, 0x01}, {3, 0x04}, {4, 0x08}, {3, 0x06}, {4, 0x09},
    {4, 0x0D}, {4, 0x03}, 
    {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}
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
  _isManualMode = false;
  _isFirstElement = true;
  _nextEventMicros = 0;
  _calculatedSpaceMicros = 0;
  _lastPaddleReleaseMicros = 0;
}

// --- Status Helpers ---

bool TM_Keyer_Engine::isPaddleActive() const {
    return _paddleDot || _paddleDash;
}

// --- Inputs ---

void TM_Keyer_Engine::updatePaddles(bool dotPressed, bool dashPressed) {
  
  if (dotPressed && !_paddleDot) _ultimaticPriorityDot = true;
  if (dashPressed && !_paddleDash) _ultimaticPriorityDot = false;

  bool wasPressed = _paddleDot || _paddleDash;
  bool isPressed  = dotPressed || dashPressed;

  if (wasPressed && !isPressed) {
      _lastPaddleReleaseMicros = micros();
  }
  
  // 1. Update internal state FIRST
  _paddleDot = dotPressed;
  _paddleDash = dashPressed;
  
  // 2. Notify callback (Protocol will read the NEW state via isPaddleActive)
  if (isPressed != wasPressed) {
      if (_cbPaddle) _cbPaddle(isPressed);

      #if WK_INFO_TRACE
         Serial.print("ENG: Paddles changed -> "); Serial.println(isPressed);
      #endif
  }

  // 3. Break-in Logic
  bool queueBusy = (_head != _tail) || (_currentMorseLen > 0);
  
  // If queue is playing from host, and we touch paddles -> Abort Host
  if (queueBusy && !_isManualMode && isPressed) {
      #if WK_INFO_TRACE
        Serial.println("ENG: Break-in triggered by paddle!");
      #endif
      abortNow(); 
  }

  // Squeeze Memory Logic
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
        _isFirstElement = false; 
    } else {
        if (_straightKeyActive) {
            setKey(false);
            setPtt(false); 
            _straightKeyActive = false;
            _state = State::IDLE; 
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

bool TM_Keyer_Engine::enqueueChar(char c) { return enqueue({KeyerEventType::CHAR, (uint16_t)c}); }
bool TM_Keyer_Engine::enqueueWpm(uint8_t wpm) { return enqueue({KeyerEventType::SET_WPM, (uint16_t)wpm}); }
bool TM_Keyer_Engine::enqueuePtt(bool on) { return enqueue({KeyerEventType::SET_PTT, (uint16_t)(on ? 1 : 0)}); }
bool TM_Keyer_Engine::enqueueWait(uint16_t ms) { return enqueue({KeyerEventType::WAIT_MS, ms}); }

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
  _paddleMemoryDot = false;
  _paddleMemoryDash = false;
  _isManualMode = false;
  _isFirstElement = true;
  
  // Note: We do NOT clear _paddleDot/_paddleDash here, 
  // as they reflect physical state.
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

void TM_Keyer_Engine::setMode(KeyerMode mode) { _mode = mode; }
void TM_Keyer_Engine::setWeighting(uint8_t weight) { _weight = weight; }
void TM_Keyer_Engine::setRatio(uint8_t ratio) { 
    _ratio = (float)ratio; 
    if (_ratio < 1.0f) _ratio = 1.0f;
}
void TM_Keyer_Engine::setPttLeadTail(uint16_t leadMs, uint16_t tailMs) {
  _pttLeadMs = leadMs;
  _pttTailMs = tailMs;
}
void TM_Keyer_Engine::setFarnsworth(uint8_t wpm) { _farnsworthWpm = wpm; }
void TM_Keyer_Engine::setCompensation(uint8_t ms) { _compensationMs = ms; }
void TM_Keyer_Engine::setFirstExtension(uint8_t ms) { _firstExtensionMs = ms; }
void TM_Keyer_Engine::setAutospace(bool active) { _autospace = active; }

void TM_Keyer_Engine::setTune(bool active) {
    if (active) {
        if (!_pttActive) setPtt(true);
        setKey(true);
        _state = State::TUNE_ACTIVE;
        _tuneStartMs = millis();
    } else {
        setKey(false);
        setPtt(false);
        _state = State::IDLE;
    }
}

// --- Hardware ---

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
  return (_head != _tail) || (_state != State::IDLE);
}

bool TM_Keyer_Engine::isTransmitting() const {
    return _keyActive || (_state == State::TRANSMITTING_ELEMENT) || (_state == State::ELEMENT_SPACE);
}

// --- Logic ---

uint32_t TM_Keyer_Engine::calculateDotMicros(bool useFarnsworth) const {
    uint8_t speed = _wpm;
    if (useFarnsworth && _farnsworthWpm > _wpm) speed = _farnsworthWpm;
    if (speed < 1) speed = 1;
    return 1200000UL / speed;
}

void TM_Keyer_Engine::lookupMorse(char c, uint8_t &code, uint8_t &len) {
  c = toupper(c);
  if (c >= ' ' && c <= '_') {
    uint8_t idx = c - ' ';
    len = MorseTable[idx].len;
    code = MorseTable[idx].code;
  } else if (c == '^') { len = 5; code = 0x16; } 
  else if (c == '{') { len = 4; code = 0x0A; } 
  else if (c == '}') { len = 4; code = 0x07; } 
  else if (c == '~') { len = 4; code = 0x0C; } 
  else { len = 0; code = 0; }
}

void TM_Keyer_Engine::checkPaddles() {
    
    // Manual modes
    if (_mode == KeyerMode::SINGLE_PADDLE) {
        if (_paddleDot || _paddleDash) {
            _isManualMode = true; 
            if (!_pttActive) setPtt(true);
            setKey(true);
            _isFirstElement = false;
        } else {
            setKey(false);
            if (!_straightKeyActive) setPtt(false);
            _isFirstElement = true;
        }
        return;
    }

    if (_mode == KeyerMode::BUG) {
        if (_paddleDash) {
            _isManualMode = true;
            if (!_pttActive) setPtt(true);
            setKey(true);
            _state = State::IDLE; 
            _isFirstElement = false;
            return;
        } else {
            if (_keyActive && _state == State::IDLE && !_paddleDot) {
                setKey(false);
            }
        }
    }

    // Automatic modes
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

    if (_mode == KeyerMode::ULTIMATIC) {
        if (_paddleDot && _paddleDash) {
            if (_ultimaticPriorityDot) sendDot = true;
            else sendDash = true;
        } else if (_paddleDot) sendDot = true;
        else if (_paddleDash) sendDash = true;
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
        if (_paddleDot && _paddleDash) {
            if (_lastElementWasDot) sendDash = true;
            else sendDot = true;
        } else if (_paddleDot) sendDot = true;
        else if (_paddleDash) sendDash = true;
    }
    else if (_mode == KeyerMode::BUG) {
        if (_paddleDot) sendDot = true;
    }

    if (sendDot) _paddleMemoryDot = false;
    if (sendDash) _paddleMemoryDash = false;

    if (sendDot) {
        _isManualMode = true; startElement(false); _lastElementWasDot = true;
    } else if (sendDash) {
        _isManualMode = true; startElement(true); _lastElementWasDot = false;
    } else {
        // Paddles released
        bool queueHasData = (_head != _tail);
        if (_pttActive && _state == State::IDLE && !_paddleDot && !_paddleDash && !queueHasData) {
             if (_pttTailMs > 0) {
                 _state = State::PTT_TAIL_DELAY;
                 _nextEventMicros = micros() + (_pttTailMs * 1000UL);
             } else {
                 setPtt(false);
             }
             _isFirstElement = true;
        }
    }
}

// Latency Compensation Helper
uint32_t TM_Keyer_Engine::getSafeStartTime() {
    uint32_t now = micros();
    if (_nextEventMicros != 0 && (int32_t)(now - _nextEventMicros) < 10000) {
        return _nextEventMicros;
    }
    return now;
}

void TM_Keyer_Engine::poll() {
  uint32_t now = micros();

  if (_state == State::TUNE_ACTIVE) {
      if (millis() - _tuneStartMs > 10000) setTune(false);
      return;
  }

  if (_nextEventMicros > 0) {
    if ((int32_t)(now - _nextEventMicros) < 0) return;
  }

  switch (_state) {
    case State::IDLE:
      // Autospace logic placeholder
      if (_autospace && _isManualMode && !_paddleDot && !_paddleDash) {
           // Logic disabled for now
      }

      checkPaddles();
      if (_state == State::IDLE) {
          processQueue();
      }
      if (_state != State::IDLE) poll();
      break;

    case State::PTT_LEAD_DELAY:
      _state = State::IDLE;
      poll(); 
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
          // Character finished.
          if (_isManualMode) {
              _state = State::IDLE;
              _nextEventMicros = getSafeStartTime(); 
              
              checkPaddles();
              if (_state != State::IDLE) poll();
          } else {
              _state = _inProsign ? State::ELEMENT_SPACE : State::INTER_CHAR_SPACE;
              _nextEventMicros = getSafeStartTime() + calculateDotMicros(false) * 2;
              if (_inProsign) _nextEventMicros = getSafeStartTime();
          }
      } else {
          // Next element
          bool isDash = (_currentMorseCode & 0x01);
          _currentMorseCode >>= 1;
          _currentMorseLen--;
          startElement(isDash);
      }
      break;

    case State::TRANSMITTING_ELEMENT:
      setKey(false);
      _state = State::ELEMENT_SPACE;
      _nextEventMicros = getSafeStartTime() + _calculatedSpaceMicros; 
      break;

    case State::ELEMENT_SPACE:
      if (_currentMorseLen > 0) {
          _state = State::START_ELEMENT;
          poll(); 
      } else {
          if (_isManualMode) {
              _state = State::IDLE;
              checkPaddles();
              if (_state != State::IDLE) poll();
          } else {
              _state = State::START_ELEMENT; 
              poll();
          }
      }
      break;

    case State::INTER_CHAR_SPACE:
    case State::INTER_WORD_SPACE:
      _state = State::IDLE;
      _lastElementWasDot = false;
      _paddleMemoryDot = false;
      _paddleMemoryDash = false;
      _isFirstElement = true;
      
      checkPaddles();
      if (_state == State::IDLE) processQueue();
      
      if (_state != State::IDLE) poll();
      break;
      
    default:
      break;
  }
}

void TM_Keyer_Engine::startElement(bool isDash) {
  setKey(true);
  _state = State::TRANSMITTING_ELEMENT;
  
  uint32_t dotLen = calculateDotMicros(true); 
  uint32_t nominalDuration = isDash ? (dotLen * (uint32_t)_ratio) : dotLen;
  
  int32_t weightDelta = 0;
  if (_weight != 50) weightDelta = ((int32_t)_weight - 50) * (int32_t)dotLen / 50;
  
  int32_t compMicros = (int32_t)_compensationMs * 1000;
  uint32_t toneDuration = nominalDuration + weightDelta + compMicros;
  int32_t spaceDuration = (int32_t)dotLen - weightDelta - compMicros;

  if (_isFirstElement) {
      toneDuration += (_firstExtensionMs * 1000);
      _isFirstElement = false;
  }

  if ((int32_t)toneDuration < 1000) toneDuration = 1000;
  if (spaceDuration < 1000) spaceDuration = 1000;

  _calculatedSpaceMicros = (uint32_t)spaceDuration;
  _nextEventMicros = getSafeStartTime() + toneDuration;
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
          Serial.print("ENG: '"); Serial.print(c); Serial.println("'");
        #endif
        
        if (_cbChar) _cbChar(c);

        if (c == ' ') {
          _state = State::INTER_WORD_SPACE;
          _isManualMode = false;
          // Word space = 7 dots total. 
          // 1 (Element Space) + 2 (Inter-char) already waited = 3.
          // Need 4 more.
          _nextEventMicros = micros() + (calculateDotMicros() * 4); 
        } else {
          lookupMorse(c, _currentMorseCode, _currentMorseLen);
          if (_currentMorseLen > 0) {
            _state = State::START_ELEMENT;
            _isManualMode = false; 
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
/*
  tm_wk_proto.cpp

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


#include "tm_wk_proto.h"

// Enable chirps
#ifndef TM_WK_BEEP_ENABLE
#define TM_WK_BEEP_ENABLE 1
#endif

// EXTMEM allocation APIs (Teensy 4.1)
extern "C" void* extmem_malloc(size_t size);
extern "C" void  extmem_free(void* ptr);

// External StopBeep (Implemented in Wrapper)
extern "C" void Keyer_StopBeep();

// Provided by keyer layer
extern void Keyer_Beep(uint16_t freq, uint16_t ms);

// C hooks
extern "C" void WK_OnCharEcho(uint8_t ch) {
  if (TM_WK_Protocol::active()) {
    TM_WK_Protocol::active()->onKeyerCharEcho(ch);
  }
}

extern "C" void WK_OnPaddleActivity(bool active) {
  if (TM_WK_Protocol::active()) {
    TM_WK_Protocol::active()->onPaddleActivity(active);
  }
}

FLASHMEM void TM_WK_Protocol::onAdminSetBaud(uint32_t baud)
{
  _adminRequestedBaud = baud;
  _adminBaudPending   = true;
#if WK_INFO_TRACE
  WK_DEBUGF("WK: Admin baud request = %lu\n", (unsigned long)baud);
#endif
}

TM_WK_Protocol* TM_WK_Protocol::s_active = nullptr;
static uint8_t s_lastRxByte = 0;

// Non-blocking tune helper (State Machine)
void TM_WK_Protocol::startConnectTune(bool connect)
{
  #if TM_WK_BEEP_ENABLE
    _tuneIsConnect = connect;
    _tuneState     = TUNE_TONE1;

    // Start first tone immediately, but don't stop it until its duration has elapsed.
    const uint16_t t1_ms = connect ? 80 : 90;
    if (connect) Keyer_Beep(880,  t1_ms);
    else         Keyer_Beep(1175, t1_ms);

    _tuneTimer = millis() + t1_ms;
  #endif
}

static inline void wk_play_short_tone()
{
  Keyer_Beep(1400,  40);
}

// ========== ctor / active ==========
TM_WK_Protocol::~TM_WK_Protocol() {
  if (_rx) {
    if (_rx_from_extmem) extmem_free(_rx);
    else free(_rx);
    _rx = nullptr;
  }
  if (s_active == this) s_active = nullptr;
}

TM_WK_Protocol::TM_WK_Protocol(TM_IKeyer& keyer, TM_IByteWriter& writer)
: _k(keyer), _w(&writer), _rx(nullptr)
{
#ifdef EXTMEM
  _rx = static_cast<uint8_t*>(extmem_malloc(TM_WK_RXBUF_SIZE));
  if (_rx) _rx_from_extmem = true;
#endif

  if (!_rx) {
    _rx = static_cast<uint8_t*>(malloc(TM_WK_RXBUF_SIZE));
    _rx_from_extmem = false;
  }
  if (_rx) memset(_rx, 0, TM_WK_RXBUF_SIZE);

  s_active = this;
  _lastReportedWpm = _k.getWpm();
  _lastWpmNotifyMs = millis();
}

TM_WK_Protocol* TM_WK_Protocol::active() {
  return s_active;
}

FLASHMEM void TM_WK_Protocol::setLocalBaseline(uint8_t wpm) {
    if (wpm == _lastHostImmediateWpm) {
        return;
    }

    uint8_t newRaw = mapWpmToPotRaw(wpm);
    if (newRaw == _lastPotSentRaw) {
        _baselineWpm = wpm;
        _baselineSaved = true;
        return;
    }

    _baselineWpm = wpm;
    _baselineSaved = true;
    
    // Set flag for main loop
    _pendingPotStatusWpm = wpm;
    _potStatusPending = true;
}

FLASHMEM void TM_WK_Protocol::setExternalBaseline(uint8_t wpm) {
    _lastExternalWpm = wpm;
    _externalWpmHoldUntilMs = millis() + EXTERNAL_WPM_HOLDOFF_MS;
    setLocalBaseline(wpm);
}

// === PADDLE ACTIVITY HANDLER (ISR) ===
// Safe: Sets flag only. No millis().
void TM_WK_Protocol::onPaddleActivity(bool /*active*/) {
    if (!_hostOpen) return;
    
    // Flag update only. Squelch check happens in poll().
    _paddleStatusPending = true;
}

// === CHAR ECHO HANDLER (ISR) ===
// Safe: Pushes to ring buffer only.
void TM_WK_Protocol::onKeyerCharEcho(uint8_t ch)
{
  if (!_hostOpen) return;

  // Use masking for atomic uint8_t logic
  uint8_t next = (_echoHead + 1) & (ECHO_BUF_SIZE - 1);
  if (next != _echoTail) {
      _echoBuf[_echoHead] = ch;
      _echoHead = next;
  }
}

// ========== RX FIFO primitives ==========
bool TM_WK_Protocol::rxPush(uint8_t b) {
  uint16_t nhead = rxNext(_rhead);
  if (nhead == _rtail) {
    return false; // Overflow
  }
  bool wasEmpty = rxEmpty();
  _rx[_rhead] = b;
  _rhead = nhead;
  if (wasEmpty) sendStatusStartIfNeeded();
  return true;
}

bool TM_WK_Protocol::rxPeek(uint16_t offset, uint8_t& out) const {
  if (offset >= rxUsed()) return false;
  uint16_t idx = (uint16_t)((_rtail + offset) & (TM_WK_RXBUF_SIZE - 1));
  out = _rx[idx];
  return true;
}

bool TM_WK_Protocol::rxPop(uint8_t& out) {
  if (rxEmpty()) return false;
  out = _rx[_rtail];
  _rtail = rxNext(_rtail);
  return true;
}

// ========== Host RX entry ==========
void TM_WK_Protocol::onByte(uint8_t b) {

  const uint8_t prev = s_lastRxByte;
  s_lastRxByte = b;

  if (prev == CMD_HOST_OPEN && b == 0x04) {
    _adminEchoPending = true;
    _immCmd  = IMMCMD_NONE;
    _immNeed = 0;
    _immGot  = 0;
    return;
  }

  if (_adminEchoPending) {
    _adminEchoPending = false;
    if (_w) _w->writeByte(b);
    return;
  }

  if (_adminKeyerTypePending) {
    _adminKeyerTypePending = false;
    if (_w) _w->writeByte(0x00);
    _suppressUnsolicitedUntilMs = millis() + 1500;
    return;
  }

  if (b == 0xFF) return;

  if (!_hostOpen) {
    if (b == CMD_STATUS_REQ) {
      if (_w) _w->writeByte(buildStatusByte());
      return;
    }
    
    if (_immCmd == CMD_HOST_OPEN) {
      handleImmediateParam(b);
      // Clean up state immediately
      _immCmd = IMMCMD_NONE;
      _immNeed = 0;
      _immGot = 0;
      return;
    }
    
    if (b == CMD_HOST_OPEN) {
      armImm(CMD_HOST_OPEN, 1);
      return;
    }
    return;
  }

  if (_rxBufferedParamArmed) {
    _rxBufferedParamArmed = false;
    rxPush(b);
    return;
  }

  if (_immCmd != IMMCMD_NONE && _immNeed > 0) {
    _immBuf[_immGot++] = b;
    if (_immGot >= _immNeed) {
      handleImmediateParam(_immBuf[0]);
      _immCmd  = IMMCMD_NONE;
      _immNeed = 0;
      _immGot  = 0;
    }
    return;
  }

  // Buffered commands (Fixed constants)
  if (b == CMD_BUF_SPEED || b == CMD_BUF_PTT || b == CMD_BUF_WAIT) {
    _rxBufferedParamArmed = true;
    rxPush(b);
    _waitingOpcode  = b;
    _waitingSinceMs = millis();
    return;
  }

  if (b == CMD_STATUS_REQ) {
    uint8_t st = buildStatusByte();
    if (_w) _w->writeByte(st);
    _lastStatusSent   = st;
    _lastStatusSentMs = millis();
    return;
  }

  if (b == CMD_GET_SPEED_POT) {
    const uint8_t raw = mapWpmToPotRaw(_k.getWpm());
    if (_w) _w->writeByte(raw);
    return;
  }

  if (b == CMD_CLEAR_BUF) {
    _rtail = _rhead; 
    _rxBufferedParamArmed = false;
    _waitingOpcode        = 0;
    _waitingSinceMs       = 0;
    _armedSpeedValid      = false;
    _txHoldUntilMs        = 0;

    _k.clearTextQueue(); 
    sendStatusIdleNow();
    return;
  }

  if (b == CMD_HOST_OPEN) {          
    armImm(CMD_HOST_OPEN, 1);
    return;
  }

  // Immediate commands (Fixed constants)
  if (b == CMD_SIDETONE || b == CMD_SET_WPM_IMM || b == CMD_SET_WEIGHT || b == CMD_KEY_IMMEDIATE) {
    armImm(b, 1);
    return;
  }

  if (b == CMD_SET_PTT_DELAYS) {     
    armImm(b, 2);
    return;
  }

  if (b == CMD_SET_POT_LIMITS) {     
    armImm(b, 2);
    return;
  }

  if (b == CMD_WK2_MODE) {           
    armImm(b, 1);
    return;
  }
  
  if (b == CMD_SET_COMP || b == CMD_FIRST_EXTENSION || b == CMD_FARNSWORTH) {
     armImm(b, 1);
     return;
  }

  // Assume ASCII or junk
  rxPush(b);
}

FLASHMEM void TM_WK_Protocol::handleImmediateParam(uint8_t p) {
  switch (_immCmd) {

    case CMD_HOST_OPEN: { 
#if WK_INFO_TRACE
      WK_DEBUGF("DBG: RX_CTL <00><%02X>  (HOST_OPEN)\n", (unsigned)p);
#endif
      const bool prevOpen = _hostOpen;
      bool newOpen = _hostOpen;
      bool change  = false;

      if (p == 0x02) {           // CONNECT
        _k.clearTextQueue(); 
        _k.abortNow(); 

        newOpen  = true;
        change   = (newOpen != prevOpen);
        _hostOpen = newOpen;
        _adminBaudPending = false;
        _adminRequestedBaud = 0;

        _armedSpeedValid = false;
        _txHoldUntilMs   = 0;
        _rhead = _rtail;
        
        _suppressUnsolicitedUntilMs = millis() + 1500;
        
        _lastPotSentRaw  = mapWpmToPotRaw(_k.getWpm());

        if (_w) {
          _w->writeByte(WK_REVISION_CODE);
        }

        _asciiSquelch        = true;
        _lastArmLogWpm       = 0xFF;
        _asciiSquelchUntilMs = millis() + ASCII_SQUELCH_MS;
        _asciiReadyBlipArmed = true;

        _baselineWpm   = _k.getWpm();
        _baselineSaved = true;
        
        // --- FORCE INITIAL STATUS ---
        // Vital for RumLogNG. Safe here in main context.
        if (_w) {
            uint8_t raw    = mapWpmToPotRaw(_baselineWpm);
            uint8_t potMsg = makePotStatusByte(raw);
            _w->writeByte(potMsg);
            _lastPotSentRaw = raw;
        }

      } else if (p == 0x03) {    // DISCONNECT
        _hostOpen = false;       // Mark session as closed
        onProtoClosed();         // Reset protocol state
        sendStatusIdleNow();     // Send final status
        startConnectTune(false); // Play disconnect tune
        return;
      } else if (p == 0x04) {    // EchoTest
        _adminEchoPending = true;
        return;
      } else if (p == 0x09) {    
        if (_w) _w->writeByte(WK_REVISION_CODE);
        _suppressUnsolicitedUntilMs = millis() + 1500;
        return;

      } else if (p == 0x0B) {    
        _adminKeyerTypePending = true;
        _suppressUnsolicitedUntilMs = millis() + 1500;
        return;

      } else if (p == 0x0A) {    
        if (_w) _w->writeByte((uint8_t)(WK_REVISION_CODE & 0x0F)); 
        _suppressUnsolicitedUntilMs = millis() + 1500;
        return;

      } else if (p == 0x17) {
        const uint8_t minor = (uint8_t)(WK_REVISION_CODE & 0x0F);
        if (_w) _w->writeByte(minor);

        const bool inHandshake = (_suppressUnsolicitedUntilMs &&
                                 (int32_t)(millis() - _suppressUnsolicitedUntilMs) < 0);
        if (!inHandshake) {
          onAdminSetBaud(1200);
        }

        _suppressUnsolicitedUntilMs = millis() + 1500;
        return;

      } else if (p == 0x18) {
        const uint8_t minor = (uint8_t)(WK_REVISION_CODE & 0x0F);
        if (_w) _w->writeByte(minor);

        const bool inHandshake = (_suppressUnsolicitedUntilMs &&
                                 (int32_t)(millis() - _suppressUnsolicitedUntilMs) < 0);
        if (!inHandshake) {
          onAdminSetBaud(9600);
        }

        _suppressUnsolicitedUntilMs = millis() + 1500;
        return;

      } else {
#if WK_INFO_TRACE
        WK_DEBUGF("WK: HOST OPEN param 0x%02X ignored\n", (unsigned)p);
#endif
        return;
      }

      if (change) {
        startConnectTune(newOpen);
        _lastStatusSent = WK_STATUS_TAG;
      }
      return;
    }

    case CMD_SIDETONE: { 
      break;
    }

    case CMD_SET_WEIGHT: {
       uint8_t w = p;
       // WinKey weight is typically 10..90
       if (w < 10) w = 10;
       if (w > 90) w = 90;
       setWeight(w); // Update internal state
       // Pass to engine if supported (WinKey 50 = 50% ratio)
       // _k.setWeightPermille(w * 10); 
       break;
    }

    // FIX: Consume the parameter for KEY_IMMEDIATE (0x17)
    // Using FIXED constant to avoid macro collisions.
    case CMD_KEY_IMMEDIATE: {
      // Logic for software PTT/Key can be added here if needed.
      // For now, we consume it to avoid the "22" bug.
      break;
    }
    
    case CMD_SET_WPM_IMM: { 
      uint8_t w = p;
      if (w < TM_WK_WPM_MIN) w = TM_WK_WPM_MIN;
      if (w > TM_WK_WPM_MAX) w = TM_WK_WPM_MAX;
      
      if (w < _potMinWpm) w = _potMinWpm;
      if (w > (_potMinWpm + _potRangeWpm)) w = (_potMinWpm + _potRangeWpm);

      // External holdoff: if we just notified the host about a radio-originated
      // WPM change, ignore conflicting host WPM updates for a short window.
      if (_externalWpmHoldUntilMs &&
          (int32_t)(millis() - _externalWpmHoldUntilMs) < 0) {
          if (_lastExternalWpm != 0xFF && w != _lastExternalWpm) {
#if WK_WARN_TRACE
              WK_DEBUGF("WK: ignoring host WPM %u during external holdoff (ext=%u)\n",
                        (unsigned)w, (unsigned)_lastExternalWpm);
#endif
              break;
          }
      }

      _lastHostImmediateWpm = w;
      setWpmProvenance(w, WpmOrigin::HostImmediate, F("host <02>"));
      _armedSpeedValid = false;
      recordHostSetWpm(w);
      
      _k.setWpmImmediate(w); 
      setLocalBaseline(w);

#if WK_INFO_TRACE
      WK_DEBUGF("WK: IMM WPM <02> req=%u clamped=%u\n", (unsigned)p, (unsigned)w);
#endif
      break;
    }

    case CMD_WK2_MODE: {
      const uint8_t mode = _immBuf[0];
      _wkModeReg = mode;
      break;
    }
    
    case CMD_SET_COMP: {
       _k.setKeyCompensation(p);
       break; 
    }
    case CMD_FIRST_EXTENSION: {
       _k.setFirstExtension(p);
       break;
    }
    case CMD_FARNSWORTH: {
       _k.setFarnsworth(p);
       break;
    }

    case CMD_SET_PTT_DELAYS: {
      const uint8_t lead = _immBuf[0];
      const uint8_t tail = _immBuf[1];
      _pttLeadMs = (uint16_t)lead;
      _pttTailMs = (uint16_t)tail;
      break;
    }

    case CMD_SET_POT_LIMITS: {
      uint8_t hostMin   = _immBuf[0];
      uint8_t hostRange = _immBuf[1];

      if (hostMin < 5)   hostMin = 5;
      if (hostMin > 99)  hostMin = 99;
      if (hostRange < 10) hostRange = 10; 
      if (hostRange > 99) hostRange = 99;

      uint16_t checkMax = (uint16_t)hostMin + (uint16_t)hostRange;
      if (checkMax > 99) {
          hostRange = (uint8_t)(99 - hostMin);
          if (hostRange < 5) hostRange = 5; 
      }

      _potMinWpm   = hostMin;
      _potRangeWpm = hostRange;

      uint8_t currentWpm = _k.getWpm();
      uint8_t maxWpm     = _potMinWpm + _potRangeWpm;
      bool wpmChanged = false;

      if (currentWpm < _potMinWpm) {
          _k.setWpmImmediate(_potMinWpm);
          currentWpm = _potMinWpm;
          wpmChanged = true;
      } else if (currentWpm > maxWpm) {
          _k.setWpmImmediate(maxWpm);
          currentWpm = maxWpm;
          wpmChanged = true;
      }

      if (wpmChanged) {
          setLocalBaseline(currentWpm);
      }
      
      // Force status update (by flag)
      _pendingPotStatusWpm = currentWpm;
      _potStatusPending = true;

#if WK_INFO_TRACE
      WK_DEBUGF("WK: POT LIMITS Min=%u Rng=%u Wpm=%u\n",
                (unsigned)_potMinWpm, (unsigned)_potRangeWpm, (unsigned)currentWpm);
#endif
      break;
    }

    case CMD_GET_SPEED_POT: {
      break;
    }

    case CMD_SET_OUTPUTS: {
      break;
    }

    default:
#if WK_INFO_TRACE
      WK_DEBUGF("WK: IMM unknown 0x%02X param=0x%02X\n", (unsigned)_immCmd, (unsigned)p);
#endif
      break;
  }
}

// ========== Status helpers ==========
FLASHMEM void TM_WK_Protocol::sendStatusStartIfNeeded() {
  if (!_hostOpen) return;
  if (inHostParamParse()) return;
  if (_suppressUnsolicitedUntilMs && (int32_t)(millis() - _suppressUnsolicitedUntilMs) < 0) return;
  const uint8_t s = (uint8_t)(WK_STATUS_TAG | WK_SBIT_BUSY);
  const uint32_t now = millis();
  if (s != _lastStatusSent) {
    if (_w) _w->writeByte(s);
    _lastStatusSent   = s;
    _lastStatusSentMs = now;
#if WK_RX_TRACE
    WK_DEBUGLN(F("DBG: TX_STATUS(start) 0xC4"));
#endif  
  }
}

FLASHMEM void TM_WK_Protocol::sendStatusIdleIfPossible() {
  if (!_hostOpen) return;
  if (inHostParamParse()) return;
  if (_suppressUnsolicitedUntilMs && (int32_t)(millis() - _suppressUnsolicitedUntilMs) < 0) return;

  if (!rxEmpty()) return;
  if (_k.isBusy()) return;

  const uint32_t now = millis();
  if (_lastTxActivityMs && (int32_t)(now - _lastTxActivityMs) < 30) return;

  if (_armedSpeedValid) {
    setWpmProvenance(_armedSpeedWpm, WpmOrigin::BufferedApplyIdle, F("buf <1C> @idle"));
    _armedSpeedValid = false;
  }

  const uint8_t s = WK_STATUS_TAG;

  if (s != _lastStatusSent) {
    if (_w) _w->writeByte(s);
    _lastStatusSent   = s;
    _lastStatusSentMs = now;
#if WK_RX_TRACE
    WK_DEBUGLN(F("DBG: TX_STATUS(idle) 0xC0"));
#endif
  }
}

FLASHMEM void TM_WK_Protocol::wk_play_ready_blip() {
  wk_play_short_tone();
  _asciiSquelch = false;
#if WK_INFO_TRACE
  WK_DEBUGF("WK: ASCII squelch released (ready)\n");
#endif
}

FLASHMEM void TM_WK_Protocol::sendStatusIdleNow() {
  if (inHostParamParse()) return;
  if (_suppressUnsolicitedUntilMs && (int32_t)(millis() - _suppressUnsolicitedUntilMs) < 0) return;
  if (_w) {
    _w->writeByte(WK_STATUS_TAG); 
    _lastStatusSent   = WK_STATUS_TAG;
    _lastStatusSentMs = millis();
#if WK_RX_TRACE    
    WK_DEBUGLN(F("DBG: TX_STATUS(idle-now) 0xC0"));
#endif
  }
}

void TM_WK_Protocol::onProtoClosed() {
  _adminEchoPending      = false;
  _adminKeyerTypePending = false;
  _adminBaudPending      = false;
  _adminRequestedBaud    = 0;
  _immCmd  = IMMCMD_NONE;
  _immNeed = 0;
  _immGot  = 0;
  _rtail                = _rhead;
  _rxBufferedParamArmed = false;
  _waitingOpcode        = 0;
  _waitingSinceMs       = 0;
  _armedSpeedValid      = false;
  _txHoldUntilMs        = 0;
  _asciiSquelch               = false;
  _asciiSquelchUntilMs        = 0;
  _asciiReadyBlipArmed        = false;
  _suppressUnsolicitedUntilMs = 0;
  _k.clearTextQueue();
}

void TM_WK_Protocol::onTransportClosed() {
  if (_hostOpen) {
    _hostOpen = false;
    sendStatusIdleNow();
    startConnectTune(false);
  }
  _asciiSquelch = false;
  _asciiSquelchUntilMs = 0;
  _waitingOpcode = 0;
}


// ========== poll() (Main Loop) ==========
FLASHMEM void TM_WK_Protocol::poll() {
  
  // Non-blocking tune state machine
  #if TM_WK_BEEP_ENABLE
  if (_tuneState != TUNE_IDLE) {
      uint32_t now = millis();
      if ((int32_t)(now - _tuneTimer) > 0) {
          switch (_tuneState) {
              case TUNE_TONE1:
                  Keyer_StopBeep(); 
                  _tuneTimer = now + 120; 
                  _tuneState = TUNE_GAP;
                  break;
              case TUNE_GAP:
                  if (_tuneIsConnect) Keyer_Beep(1175, 90);
                  else                Keyer_Beep(880,  80);
                  _tuneTimer = now + 90; 
                  _tuneState = TUNE_TONE2;
                  break;
              case TUNE_TONE2:
                  Keyer_StopBeep(); 
                  _tuneState = TUNE_IDLE;
                  break;
              default:
                  _tuneState = TUNE_IDLE;
                  break;
          }
      }
  }
  #endif

  // FIX: Timeout check for stuck immediate parameters (Escape Hatch)
  if (_immCmd != IMMCMD_NONE && _immNeed > 0) {
      if ((int32_t)(millis() - _immSinceMs) > 250) {
          // Timeout: Drop incomplete command to resync
          _immCmd = IMMCMD_NONE;
          _immNeed = 0;
          _immGot = 0;
          _immSinceMs = 0;
      }
  }

  // 1. Process Pending Status Flag
  if (_potStatusPending) {
      if (_w) {
          uint8_t raw = mapWpmToPotRaw(_pendingPotStatusWpm);
          _w->writeByte(makePotStatusByte(raw));
          _lastPotSentRaw = raw;
      }
      _potStatusPending = false;
  }

  // 2. Process Buffered Char Echoes (From ISR)
  if (_w) {
      const uint32_t now = millis();
      bool sentAny = false;
      
      // FIX: use uint8_t mask for ring buffer
      while (_echoHead != _echoTail) {
          _w->writeByte(_echoBuf[_echoTail]);
          _echoTail = (_echoTail + 1) & (ECHO_BUF_SIZE - 1);
          _lastHostRxMs = now;
          _lastTxActivityMs = now;
          sentAny = true;
      }
      
      // Optimization: Only update status ONCE after draining the buffer
      if (sentAny) {
          sendStatusStartIfNeeded(); 
      }
  }

  // 3. Process Pending Paddle Status (From ISR)
  if (_paddleStatusPending) {
      _paddleStatusPending = false;
      
      // FIX: Check suppression window here in MAIN loop (Safe from ISR)
      if (_suppressUnsolicitedUntilMs && (int32_t)(millis() - _suppressUnsolicitedUntilMs) < 0) {
          // Suppressed - Do nothing
      } else {
          if (_w) {
              uint8_t s = buildStatusByte();
              _w->writeByte(s);
              _lastStatusSent = s; 
          }
      }
  }

  // 4. Squelch Timer
  if (_asciiSquelch && ASCII_SQUELCH_MS > 0) {
    if (_asciiSquelchUntilMs && (int32_t)(millis() - _asciiSquelchUntilMs) >= 0) {
      _asciiSquelch = false;
      _asciiSquelchUntilMs = 0;
      if (_asciiReadyBlipArmed) {
        _asciiReadyBlipArmed = false;
        wk_play_ready_blip();
      }
    }
  }

  // 5. Encoder Check
  const uint8_t cur = _k.getWpm();
  if (_lastObservedWpm == 0xFF) {
    _lastObservedWpm = cur;
  }
  if (cur != _lastObservedWpm) {
    _lastObservedWpm = cur;
    _lastWpmOrigin   = WpmOrigin::None;
  }

  // 6. Timeout check for buffered ops
  if (_waitingOpcode) {
    uint8_t head = 0;
    if (rxPeek(0, head) && head == _waitingOpcode && rxUsed() == 1) {
      if ((int32_t)(millis() - _waitingSinceMs) > 150) { 
        uint8_t dropped = 0;
        rxPop(dropped);
        _waitingOpcode = 0;
      }
    } else {
      _waitingOpcode = 0;
    }
  }

  // 7. Tx Hold
  if (_txHoldUntilMs) {
    uint32_t now = millis();
    if ((int32_t)(now - _txHoldUntilMs) < 0) {
      return;
    }
    _txHoldUntilMs = 0;
  }
  
  // 8. Decode One Command
  decodeAndExecute();
  
  // 9. Status Idle
  sendStatusIdleIfPossible();
}

// ========== Unified dequeue/execute ==========
FLASHMEM void TM_WK_Protocol::decodeAndExecute() {
  uint8_t b0;
  if (!rxPeek(0, b0)) return;

  if (b0 == CMD_BUF_SPEED) {
    uint8_t b1;
    if (!rxPeek(1, b1)) return; 
    uint8_t tmp; rxPop(tmp); rxPop(tmp);
    uint8_t w = b1;
    if (w < TM_WK_WPM_MIN) w = TM_WK_WPM_MIN;
    if (w > TM_WK_WPM_MAX) w = TM_WK_WPM_MAX;
    _armedSpeedWpm   = w;
    _armedSpeedValid = true;
    return;
  }

  if (b0 == CMD_BUF_PTT) {
    uint8_t b1;
    if (!rxPeek(1, b1)) return; 
    uint8_t tmp; rxPop(tmp); rxPop(tmp);
    if (b1 & 0x01) _k.startPTT();
    else           _k.stopPTT();
    return;
  }

  if (b0 == CMD_BUF_WAIT) {
    uint8_t b1;
    if (!rxPeek(1, b1)) return; 
    uint8_t tmp; rxPop(tmp); rxPop(tmp);
    uint32_t ms = (uint32_t)b1 * TM_WK_WAIT_UNIT_MS;
    _txHoldUntilMs = millis() + ms;
    return; 
  }

  if (!isAscii(b0)) {
    uint8_t junk = 0; 
    rxPop(junk);
#if WK_INFO_TRACE
    WK_DEBUGF("WK: Dropped non-ASCII/non-CMD 0x%02X\n", junk);
#endif
    return;
  }

  if (_asciiSquelch) {
      uint8_t junk = 0; 
      rxPop(junk);
#if WK_INFO_TRACE
      WK_DEBUGF("WK: Squelched ASCII 0x%02X\n", junk);
#endif
      return;
  }

  char out[16]; 
  size_t n = 0;
  while (n < sizeof(out)-1) {
    uint8_t b;
    if (!rxPeek(0, b)) break;
    if (!isAscii(b)) break; 
    rxPop(b);
    out[n++] = (char)b;
  }
  out[n] = 0;

  if (n == 0) return;

  if (_armedSpeedValid) {
    setWpmProvenance(_armedSpeedWpm, WpmOrigin::BufferedApplyAscii, F("buf <1C> @ascii"));
    _armedSpeedValid = false;
  }
  
  noInterrupts(); 
  (void)_k.enqueueText(out, n);
  interrupts();

  _lastTxActivityMs = millis();

#if WK_INFO_TRACE
  WK_DEBUGF("WK: TEXT queued n=%u\n", (unsigned)n);
#endif
}

uint8_t TM_WK_Protocol::buildStatusByte() const {
  uint8_t s = WK_STATUS_TAG;

  if (!rxEmpty() || _k.isBusy()) s |= WK_SBIT_BUSY;
  
  if (_k.isPaddlePressed()) {
      s |= WK_SBIT_KEYDOWN;
  }

  uint16_t k_used = (uint16_t)_k.txqUsed();
  uint16_t k_cap  = (uint16_t)_k.txqCapacityBytes();
  if (k_cap && (k_used > (uint16_t)((k_cap * 7U) / 10U))) {
    s |= WK_SBIT_XOFF;
  }

  return s;
}
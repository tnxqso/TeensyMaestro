/*
  tm_wk_proto.cpp

  TeensyMaestro — Community Edition (CE)
  SPDX-License-Identifier: CC-BY-NC-SA-3.0
  SPDX-FileCopyrightText: 2025 TNX QSO

  VERSION 0.9.14
  - FULL INTERRUPT SAFETY.
  - Removed all Serial/SD logging from ISR callbacks (Paddle & CharEcho).
  - Refactored decoder (No infinite loops).
*/

#include "tm_wk_proto.h"

// EXTMEM allocation APIs (Teensy 4.1)
extern "C" void* extmem_malloc(size_t size);
extern "C" void  extmem_free(void* ptr);

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

// Provided by keyer layer
extern void Keyer_Beep(uint16_t freq, uint16_t ms);

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

// Connect/disconnect chirps
#ifndef TM_WK_BEEP_ENABLE
#define TM_WK_BEEP_ENABLE 1
#endif
#if TM_WK_BEEP_ENABLE
static inline void wk_play_connect_tune(bool connect)
{
  if (connect) {
    Keyer_Beep(880,  80);
    delay(120);
    Keyer_Beep(1175, 90);
  } else {
    Keyer_Beep(1175, 90);
    delay(120);
    Keyer_Beep(880,  80);
  }
}
static inline void wk_play_short_tone()
{
  Keyer_Beep(1400,  40);
}
#else
static inline void wk_play_connect_tune(bool) {}
static inline void wk_play_short_tone() {}
#endif

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
    // Loop Prevention
    if (wpm == _lastHostImmediateWpm) {
        return;
    }

    // Jitter Prevention (Smart Squelch)
    uint8_t newRaw = mapWpmToPotRaw(wpm);
    if (newRaw == _lastPotSentRaw) {
        _baselineWpm = wpm;
        _baselineSaved = true;
        return;
    }

    _baselineWpm = wpm;
    _baselineSaved = true;
    
    // Set flag instead of sending immediately to avoid recursion
    _pendingPotStatusWpm = wpm;
    _potStatusPending = true;
}

// === PADDLE ACTIVITY HANDLER ===
// NOTE: Called from ISR. Must NOT do Serial/SD IO.
void TM_WK_Protocol::onPaddleActivity(bool active) {
    if (!_hostOpen) return;

    // Hard Squelch during startup
    if (_suppressUnsolicitedUntilMs && 
       (int32_t)(millis() - _suppressUnsolicitedUntilMs) < 0) {
       return;
    }

    if (_w) {
        uint8_t s = buildStatusByte();
        _w->writeByte(s);
        _lastStatusSent = s; 
        
        // NO LOGGING HERE!
    }
}

// ========== RX FIFO primitives ==========
bool TM_WK_Protocol::rxPush(uint8_t b) {
  uint16_t nhead = rxNext(_rhead);
  if (nhead == _rtail) {
#if WK_WARN_TRACE    
    WK_DEBUGLN(F("WK: RX overflow, dropping byte"));
#endif
    return false;
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

  if (prev == WK_CMD_HOST_OPEN && b == 0x04) {
    _adminEchoPending = true;
    _immCmd  = IMMCMD_NONE;
    _immNeed = 0;
    _immGot  = 0;
#if WK_INFO_TRACE
    WK_DEBUGLN(F("WK: EchoTest armed via raw pattern <00><04> (failsafe)"));
#endif
    return;
  }

  if (_adminEchoPending) {
    _adminEchoPending = false;
    if (_w) _w->writeByte(b);
#if WK_RX_TRACE
    WK_DEBUGF("DBG: ECHO -> 0x%02X\n", (unsigned)b);
#endif
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
    if (b == WK_CMD_STATUS_REQ) {
      if (_w) _w->writeByte(buildStatusByte());
      return;
    }
    if (_immCmd == WK_CMD_HOST_OPEN) {
      handleImmediateParam(b);
      _immCmd = IMMCMD_NONE;
      return;
    }
    if (b == WK_CMD_HOST_OPEN) {
      _immCmd = WK_CMD_HOST_OPEN;
#if WK_ENQ_TRACE
      WK_DEBUGF("WK: -> IMM_CMD (0x00) while closed\n");
#endif
      return;
    }
    return;
  }

  if (_rxBufferedParamArmed) {
    _rxBufferedParamArmed = false;
    rxPush(b);
#if WK_ENQ_TRACE
    WK_DEBUGF("DBG: RX_ENQ param 0x%02X (armed->cleared)\n", (unsigned)b);
#endif
    return;
  }

  if (_immCmd != IMMCMD_NONE && _immNeed > 0) {
    _immBuf[_immGot++] = b;
    if (_immGot >= _immNeed) {
      if (_immCmd == WK_CMD_HOST_OPEN) {
        handleImmediateParam(_immBuf[0]); 
      } else {
        handleImmediateParam(_immBuf[0]);
      }
      _immCmd  = IMMCMD_NONE;
      _immNeed = 0;
      _immGot  = 0;
    }
    return;
  }

  if (b == 0x1C || b == 0x18 || b == 0x1A) {
    _rxBufferedParamArmed = true;
    rxPush(b);
    _waitingOpcode  = b;
    _waitingSinceMs = millis();
#if WK_ENQ_TRACE
    WK_DEBUGF("DBG: RX_ENQ opcode 0x%02X (armed)\n", (unsigned)b);
#endif
    return;
  }

  if (_asciiSquelch && isAscii(b)) {
#if WK_INFO_TRACE
    WK_DEBUGF("WK: squelch ASCII 0x%02X (timer)\n", (unsigned)b);
#endif
    return;
  }

  if (b == WK_CMD_STATUS_REQ) {
    uint8_t st = buildStatusByte();
    if (_w) _w->writeByte(st);
    _lastStatusSent   = st;
    _lastStatusSentMs = millis();
    return;
  }

  if (b == WK_CMD_GET_SPEED_POT) {
    const uint8_t raw = mapWpmToPotRaw(_k.getWpm());
    if (_w) _w->writeByte(raw);
    return;
  }

  if (b == WK_CMD_CLEAR_BUF) {
#if WK_INFO_TRACE
    WK_DEBUGLN(F("WK: CLEAR_BUFFER (<0A>) -> Clearing queue"));
#endif

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

  if (b == WK_CMD_HOST_OPEN) {          
    _immCmd  = b;
    _immNeed = 1;
    _immGot  = 0;
    return;
  }

  if (b == WK_CMD_SIDETONE || b == WK_CMD_SET_WPM_IMM || b == WK_CMD_SET_WEIGHT) {
    _immCmd  = b;
    _immNeed = 1;
    _immGot  = 0;
    return;
  }

  if (b == WK_CMD_SET_PTT_DELAYS) {     
    _immCmd  = b;
    _immNeed = 2;
    _immGot  = 0;
    return;
  }

  if (b == WK_CMD_SET_POT_LIMITS) {     
    _immCmd  = b;
    _immNeed = 2;
    _immGot  = 0;
    return;
  }

  if (b == WK_CMD_WK2_MODE) {           
    _immCmd  = b;
    _immNeed = 1;
    _immGot  = 0;
    return;
  }
  
  if (b == WK_CMD_SET_COMP || b == WK_CMD_FIRST_EXTENSION || b == WK_CMD_FARNSWORTH) {
     _immCmd = b;
     _immNeed = 1;
     _immGot = 0;
     return;
  }

  rxPush(b);
#if WK_ENQ_TRACE
  WK_DEBUGF("DBG: RX_ENQ 0x%02X\n", (unsigned)b);
#endif

}

// NOTE: Called from ISR. Must NOT do Serial/SD IO.
void TM_WK_Protocol::onKeyerCharEcho(uint8_t ch)
{
  if (!_hostOpen) return;

  if (_w) _w->writeByte(ch);
  const uint32_t now = millis();
  _lastHostRxMs     = now;
  _lastTxActivityMs = now;
  
  // NO LOGGING HERE!
  
  sendStatusStartIfNeeded();
}

FLASHMEM void TM_WK_Protocol::handleImmediateCommandByte(uint8_t) {
}

FLASHMEM void TM_WK_Protocol::handleImmediateParam(uint8_t p) {
  switch (_immCmd) {

    case WK_CMD_HOST_OPEN: { 
#if WK_INFO_TRACE
      WK_DEBUGF("DBG: RX_CTL <00><%02X>  (HOST_OPEN)\n", (unsigned)p);
#endif
      const bool prevOpen = _hostOpen;
      bool newOpen = _hostOpen;
      bool change  = false;

      if (p == 0x02) {           // CONNECT
        
        // SAFETY: Clear queues to prevent ghost TX
        _k.clearTextQueue(); 
        _k.abortNow(); 

        newOpen  = true;
        change   = (newOpen != prevOpen);
        _hostOpen = newOpen;
        _adminBaudPending = false;
        _adminRequestedBaud = 0;

#if WK_INFO_TRACE
        WK_DEBUGLN(F("WK: HOST OPEN -> CONNECT"));
#endif
        _armedSpeedValid = false;
        _txHoldUntilMs   = 0;
        _rhead = _rtail;
        
        // Set suppression timer to block NOISE
        _suppressUnsolicitedUntilMs = millis() + 1500;
        
        _lastPotSentRaw  = mapWpmToPotRaw(_k.getWpm());

        if (_w) {
          _w->writeByte(WK_REVISION_CODE);
#if WK_INFO_TRACE
          WK_DEBUGF("WK: send revision 0x%02X on CONNECT\n", (unsigned)WK_REVISION_CODE);
#endif
        }

        _asciiSquelch        = true;
        _lastArmLogWpm       = 0xFF;
        _asciiSquelchUntilMs = millis() + ASCII_SQUELCH_MS;
        _asciiReadyBlipArmed = true;

        _baselineWpm   = _k.getWpm();
        _baselineSaved = true;
        
        // --- FORCE INITIAL STATUS (Bypass Suppression) ---
        // This fixes the "-" display in RumLogNG. 
        if (_w) {
            uint8_t raw    = mapWpmToPotRaw(_baselineWpm);
            uint8_t potMsg = makePotStatusByte(raw);
            _w->writeByte(potMsg);
            _lastPotSentRaw = raw;
        }

      } else if (p == 0x03) {    // DISCONNECT
        onProtoClosed();
        return;
      } else if (p == 0x04) {    // EchoTest
        _adminEchoPending = true;
#if WK_INFO_TRACE
        WK_DEBUGLN(F("WK: HOST OPEN EchoTest armed"));
#endif
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
#if WK_INFO_TRACE
        WK_DEBUGLN(newOpen ? F("WK: BEEP CONNECT") : F("WK: BEEP DISCONNECT"));
#endif
        wk_play_connect_tune(newOpen);
        _lastStatusSent = WK_STATUS_TAG;
      }
      return;
    }

    case WK_CMD_SIDETONE: { 
      break;
    }

    case WK_CMD_SET_WPM_IMM: { 
      uint8_t w = p;
      // 1. Clamp to absolute physical limits
      if (w < TM_WK_WPM_MIN) w = TM_WK_WPM_MIN;
      if (w > TM_WK_WPM_MAX) w = TM_WK_WPM_MAX;
      
      // 2. RUM LOG FIX: Clamp to Active Pot Limits
      if (w < _potMinWpm) w = _potMinWpm;
      if (w > (_potMinWpm + _potRangeWpm)) w = (_potMinWpm + _potRangeWpm);

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

    case WK_CMD_WK2_MODE: {
      const uint8_t mode = _immBuf[0];
      _wkModeReg = mode;
#if WK_INFO_TRACE
      WK_DEBUGF("WK: WK2 mode register=0x%02X\n", (unsigned)mode);
#endif
      break;
    }
    
    case WK_CMD_SET_COMP: {
       _k.setKeyCompensation(p);
       break; 
    }
    case WK_CMD_FIRST_EXTENSION: {
       _k.setFirstExtension(p);
       break;
    }
    case WK_CMD_FARNSWORTH: {
       _k.setFarnsworth(p);
       break;
    }

    case WK_CMD_SET_PTT_DELAYS: {
      const uint8_t lead = _immBuf[0];
      const uint8_t tail = _immBuf[1];
      _pttLeadMs = (uint16_t)lead;
      _pttTailMs = (uint16_t)tail;
      break;
    }

    case WK_CMD_SET_POT_LIMITS: {
      uint8_t hostMin   = _immBuf[0];
      uint8_t hostRange = _immBuf[1];

      // Sanity checks
      if (hostMin < 5)   hostMin = 5;
      if (hostMin > 99)  hostMin = 99;
      if (hostRange < 10) hostRange = 10; 
      if (hostRange > 99) hostRange = 99;

      // Cap max range
      uint16_t checkMax = (uint16_t)hostMin + (uint16_t)hostRange;
      if (checkMax > 99) {
          hostRange = (uint8_t)(99 - hostMin);
          if (hostRange < 5) hostRange = 5; 
      }

      _potMinWpm   = hostMin;
      _potRangeWpm = hostRange;

      // --- SYNC LOGIC ---
      uint8_t currentWpm = _k.getWpm();
      uint8_t maxWpm     = _potMinWpm + _potRangeWpm;
      bool wpmChanged = false;

      // 1. Force WPM into valid range (K1EL Spec)
      if (currentWpm < _potMinWpm) {
          _k.setWpmImmediate(_potMinWpm);
          currentWpm = _potMinWpm;
          wpmChanged = true;
      } else if (currentWpm > maxWpm) {
          _k.setWpmImmediate(maxWpm);
          currentWpm = maxWpm;
          wpmChanged = true;
      }

      // 2. Update Baseline
      if (wpmChanged) {
          setLocalBaseline(currentWpm);
      }
      
      // 3. FORCE SEND STATUS (Bypass Suppression Timer)
      if (_w) {
          uint8_t raw    = mapWpmToPotRaw(currentWpm);
          uint8_t potMsg = makePotStatusByte(raw);
          _w->writeByte(potMsg);
          _lastPotSentRaw = raw;
      }

#if WK_INFO_TRACE
      WK_DEBUGF("WK: POT LIMITS Min=%u Rng=%u Wpm=%u\n",
                (unsigned)_potMinWpm, (unsigned)_potRangeWpm, (unsigned)currentWpm);
#endif
      break;
    }

    case WK_CMD_GET_SPEED_POT: {
      break;
    }

    case WK_CMD_SET_OUTPUTS: {
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
#if WK_INFO_TRACE
    WK_DEBUGF("WK: setWpm=%u [armed->applied @idle]\n", (unsigned)_armedSpeedWpm);
#endif
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

FLASHMEM void TM_WK_Protocol::sendReadyPulse() {
  if (_w) {
    _w->writeByte(WK_READY_PULSE);
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

  if (_baselineSaved) {
    setWpmProvenance(_baselineWpm, WpmOrigin::BaselineRestore, F("baseline restore"));
    recordHostSetWpm(_baselineWpm); 
    _baselineSaved = false;
  }
  if (_baselineWeightSaved) {
    _weight = _baselineWeight;
    _baselineWeightSaved = false;
  }

#if WK_INFO_TRACE
  WK_DEBUGLN(F("WK: PROTO CLOSE (<00><03>) -> internal reset (transport open)"));
#endif
}

void TM_WK_Protocol::onTransportClosed() {
  if (_hostOpen) {
    _hostOpen = false;
#if WK_INFO_TRACE
    WK_DEBUGF("WK: TRANSPORT CLOSED -> DISCONNECT\n");
#endif
    sendStatusIdleNow();
    wk_play_connect_tune(false);
  }

  if (_baselineSaved) {
    setWpmProvenance(_baselineWpm, WpmOrigin::BaselineRestore, F("baseline restore"));
    recordHostSetWpm(_baselineWpm);
    _baselineSaved = false;
  }

  if (_baselineWeightSaved) {
    _weight = _baselineWeight;
    _baselineWeightSaved = false;
  }

  _asciiSquelch = false;
  _asciiSquelchUntilMs = 0;
  _waitingOpcode = 0;
}


// ========== poll() ==========
FLASHMEM void TM_WK_Protocol::poll() {
  
  // 1. Process Pending Status (Safe from recursion)
  if (_potStatusPending) {
      if (_w) {
          uint8_t raw = mapWpmToPotRaw(_pendingPotStatusWpm);
          _w->writeByte(makePotStatusByte(raw));
          _lastPotSentRaw = raw;
      }
      _potStatusPending = false;
  }

  // 2. Squelch Timer
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

  // 3. Encoder Check
  const uint8_t cur = _k.getWpm();
  if (_lastObservedWpm == 0xFF) {
    _lastObservedWpm = cur;
  }
  if (cur != _lastObservedWpm) {
    // We update the observer, but we DO NOT call sendPotStatus() here anymore.
    // This prevents the "echo loop" where a host speed command triggers a pot status reply.
    // The only time sendPotStatus is called is when the user turns the knob (setLocalBaseline).
    
    _lastObservedWpm = cur;
    _lastWpmOrigin   = WpmOrigin::None;
  }

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

  if (_txHoldUntilMs) {
    uint32_t now = millis();
    if ((int32_t)(now - _txHoldUntilMs) < 0) {
      return;
    }
    _txHoldUntilMs = 0;
  }
  
  // 6. Decode ONE command per loop (NO while(true) to prevent infinite hangs)
  decodeAndExecute();
  
  sendStatusIdleIfPossible();
}

// ========== Unified dequeue/execute ==========
FLASHMEM void TM_WK_Protocol::decodeAndExecute() {
  uint8_t b0;
  if (!rxPeek(0, b0)) return;

  // --- Buffered Commands ---
  
  if (b0 == WK_CMD_BUF_SPEED) {
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

  if (b0 == WK_CMD_BUF_PTT) {
    uint8_t b1;
    if (!rxPeek(1, b1)) return; 
    uint8_t tmp; rxPop(tmp); rxPop(tmp);
    if (b1 & 0x01) _k.startPTT();
    else           _k.stopPTT();
    return;
  }

  if (b0 == WK_CMD_BUF_WAIT) {
    uint8_t b1;
    if (!rxPeek(1, b1)) return; 
    uint8_t tmp; rxPop(tmp); rxPop(tmp);
    uint32_t ms = (uint32_t)b1 * TM_WK_WAIT_UNIT_MS;
    _txHoldUntilMs = millis() + ms;
    return; 
  }

  // --- TEXT / ASCII HANDLING ---
  
  // Junk check
  if (!isAscii(b0)) {
    uint8_t junk = 0; // FIX: Initialize variable to suppress warning
    rxPop(junk);
#if WK_INFO_TRACE
    WK_DEBUGF("WK: Dropped non-ASCII/non-CMD 0x%02X\n", junk);
#endif
    return;
  }

  // ASCII Squelch
  if (_asciiSquelch) {
      uint8_t junk = 0; // FIX: Initialize variable
      rxPop(junk);
#if WK_INFO_TRACE
      WK_DEBUGF("WK: Squelched ASCII 0x%02X\n", junk);
#endif
      return;
  }

  // Collect text
  // FIX: Reduced buffer size to minimize time in critical section
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
  
  // --- CRITICAL FIX: PROTECT QUEUE FROM INTERRUPTS ---
  // We must disable interrupts while adding text to the queue.
  // Otherwise, the Keyer Engine (running in ISR) might read/write 
  // simultaneously, corrupting memory and causing a Hard Fault/Freeze.
  noInterrupts(); 
  (void)_k.enqueueText(out, n);
  interrupts();
  // ---------------------------------------------------

  _lastTxActivityMs = millis();

#if WK_INFO_TRACE
  WK_DEBUGF("WK: TEXT queued n=%u\n", (unsigned)n);
#endif
}

// ========== Status byte ==========
// Note: FLASHMEM removed to ensure this can be called from ISR context (onPaddleActivity)
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
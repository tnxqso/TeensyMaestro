/*
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

// EXTMEM allocation APIs (Teensy 4.1). extmem_malloc returns nullptr if PSRAM is absent.
extern "C" void* extmem_malloc(size_t size);
extern "C" void  extmem_free(void* ptr);

// C hook: keyer calls this after each symbol/char so the host sees progress.
extern "C" void WK_OnCharEcho(uint8_t ch) {
  if (TM_WK_Protocol::active()) {
    TM_WK_Protocol::active()->onKeyerCharEcho(ch);
  }
}

// Provided by your keyer layer
extern void Keyer_Beep(uint16_t freq, uint16_t ms);

FLASHMEM void TM_WK_Protocol::onAdminSetBaud(uint32_t baud)
{
  _adminRequestedBaud = baud;
  _adminBaudPending   = true;
#if WK_INFO_TRACE
  WK_DEBUGF("WK: Admin baud request = %lu\n", (unsigned long)baud);
#endif
}

// Static instance pointer
TM_WK_Protocol* TM_WK_Protocol::s_active = nullptr;

static uint8_t s_lastRxByte = 0;

// Optional connect/disconnect chirps
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
    if (_rx_from_extmem) {
      extmem_free(_rx);
    } else {
      free(_rx);
    }
    _rx = nullptr;
  }
  if (s_active == this) {
    s_active = nullptr;
  }
}

TM_WK_Protocol::TM_WK_Protocol(TM_IKeyer& keyer, TM_IByteWriter& writer)
: _k(keyer), _w(&writer), _rx(nullptr)
{
  // Allocate in EXTMEM (PSRAM) first if available
#ifdef EXTMEM
  _rx = static_cast<uint8_t*>(extmem_malloc(TM_WK_RXBUF_SIZE));
  if (_rx) {
    _rx_from_extmem = true;
#if WK_INFO_TRACE
    WK_DEBUGLN(F("WK: Using RAM2 (EXTMEM) for RX buffer"));
#endif
  }
#endif

  // Fallback to RAM1 if EXTMEM is unavailable or allocation failed
  if (!_rx) {
    _rx = static_cast<uint8_t*>(malloc(TM_WK_RXBUF_SIZE));
    _rx_from_extmem = false;
#if WK_WARN_TRACE
    WK_DEBUGLN(F("WK: Using RAM1 for RX buffer"));
#endif
  }

  if (_rx) {
    memset(_rx, 0, TM_WK_RXBUF_SIZE);
  } else {
#if WK_WARN_TRACE
    WK_DEBUGLN(F("WK: CRITICAL: RX buffer allocation failed!"));
#endif
  }

  s_active = this;
  _lastReportedWpm = _k.getWpm();
  _lastWpmNotifyMs = millis();
}


TM_WK_Protocol* TM_WK_Protocol::active() {
  return s_active;
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

  // Failsafe EchoTest pattern: <00><04> is always EchoTest in WinKeyer v2.
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

  // EchoTest: if armed, the very next byte must be echoed immediately.
  if (_adminEchoPending) {
    _adminEchoPending = false;
    if (_w) _w->writeByte(b);
#if WK_RX_TRACE
    WK_DEBUGF("DBG: ECHO -> 0x%02X\n", (unsigned)b);
#endif
    return;
  }

  // ReadKeyerType
  if (_adminKeyerTypePending) {
    _adminKeyerTypePending = false;
    if (_w) _w->writeByte(0x00);
    _suppressUnsolicitedUntilMs = millis() + 1500;
    return;
  }

  // Silently ignore stray 0xFF bytes from some TCP stacks
  if (b == 0xFF) return;

  // Gate: while host is CLOSED, accept only HOST_OPEN as immediate (and consume its param).
  if (!_hostOpen) {

    // Allow status request at any time (closed)
    if (b == WK_CMD_STATUS_REQ) {
      if (_w) _w->writeByte(buildStatusByte());
      return;
    }

    // If HOST_OPEN was latched, this byte is its parameter.
    if (_immCmd == WK_CMD_HOST_OPEN) {  // latch present → this is the param
      handleImmediateParam(b);
      _immCmd = IMMCMD_NONE;
      return;
    }

    // Latch HOST_OPEN opcode itself; parameter will arrive as next byte.
    if (b == WK_CMD_HOST_OPEN) {        // latch <00>
      _immCmd = WK_CMD_HOST_OPEN;
#if WK_ENQ_TRACE
      WK_DEBUGF("WK: -> IMM_CMD (0x00) while closed\n");
#endif
      return;
    }

    // Closed: ignore everything else
    return;
  }


  // Give buffered-control parameter absolute priority over any latched immediate command.
  if (_rxBufferedParamArmed) {
    _rxBufferedParamArmed = false;
    rxPush(b);
#if WK_ENQ_TRACE
    WK_DEBUGF("DBG: RX_ENQ param 0x%02X (armed->cleared)\n", (unsigned)b);
#endif
    return;
  }

  // Immediate parameter collection (variable length)
  if (_immCmd != IMMCMD_NONE && _immNeed > 0) {
    _immBuf[_immGot++] = b;
    if (_immGot >= _immNeed) {
      // Dispatch once we have all params
      if (_immCmd == WK_CMD_HOST_OPEN) {
        handleImmediateParam(_immBuf[0]); // admin subcommand in existing handler
      } else {
        handleImmediateParam(_immBuf[0]);
      }

      _immCmd  = IMMCMD_NONE;
      _immNeed = 0;
      _immGot  = 0;
    }
    return;
  }

  // Arm for a buffered opcode so the next byte (which can be printable) isn't dropped
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

  // Drop printable ASCII during post-CONNECT squelch; control bytes still pass
  if (_asciiSquelch && isAscii(b)) {
#if WK_INFO_TRACE
    WK_DEBUGF("WK: squelch ASCII 0x%02X (timer)\n", (unsigned)b);
#endif
    return;
  }

  // Immediate, zero-parameter commands
  if (b == WK_CMD_STATUS_REQ) {
    uint8_t st = buildStatusByte();
    if (_w) _w->writeByte(st);
    _lastStatusSent   = st;
    _lastStatusSentMs = millis();
    return;
  }

  if (b == WK_CMD_GET_SPEED_POT) {
    // WK2: <07> returns raw pot value (0..31), NOT a status byte.
    const uint8_t raw = mapWpmToPotRaw(_k.getWpm());
    if (_w) _w->writeByte(raw);
    return;
  }

  if (b == WK_CMD_CLEAR_BUF) {
    // WinKeyer Standard <0A>: Clear Buffer.
    
#if WK_INFO_TRACE
    WK_DEBUGLN(F("WK: CLEAR_BUFFER (<0A>) -> Clearing queue"));
#endif

    // 1. Clear the RX FIFO (host side buffer)
    _rtail = _rhead;

    // 2. Reset parser state
    _rxBufferedParamArmed = false;
    _waitingOpcode        = 0;
    _waitingSinceMs       = 0;
    _armedSpeedValid      = false;
    _txHoldUntilMs        = 0;

    // 3. Tell the Keyer Interface to clear its queue.
    _k.clearTextQueue(); 

    // 4. Force status update (WinKeyer spec says return status after clear)
    sendStatusIdleNow();
    return;
  }

  // Immediate commands with parameters (set correct expected length)
  if (b == WK_CMD_HOST_OPEN) {          // <00><admin>
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

  if (b == WK_CMD_SET_PTT_DELAYS) {     // <04><lead><tail>
    _immCmd  = b;
    _immNeed = 2;
    _immGot  = 0;
    return;
  }

  if (b == WK_CMD_SET_POT_LIMITS) {     // <05><min><range><unused>
    _immCmd  = b;
    _immNeed = 3;
    _immGot  = 0;
    return;
  }

  if (b == WK_CMD_WK2_MODE) {           // <0E><modeReg>
    _immCmd  = b;
    _immNeed = 1;
    _immGot  = 0;
    return;
  }

  // Buffered controls and ASCII go into the unified FIFO
  rxPush(b);
#if WK_ENQ_TRACE
  WK_DEBUGF("DBG: RX_ENQ 0x%02X\n", (unsigned)b);
#endif

}

FLASHMEM void TM_WK_Protocol::onKeyerCharEcho(uint8_t ch)
{
  // Echo the character back to the host immediately.
  if (_w) _w->writeByte(ch);

  // Activity touch (some hosts watch busy/idle edges).
  const uint32_t now = millis();
  _lastHostRxMs     = now;
  _lastTxActivityMs = now;

  // Ensure start edge is visible.
  sendStatusStartIfNeeded();
}

// ========== Immediate command handling ==========
FLASHMEM void TM_WK_Protocol::handleImmediateCommandByte(uint8_t) {
  // Unused in this design; we latch immediate command in onByte().
}

FLASHMEM void TM_WK_Protocol::handleImmediateParam(uint8_t p) {
  switch (_immCmd) {

    case WK_CMD_HOST_OPEN: { // <00><02>=CONNECT, <00><03>=DISCONNECT
#if WK_INFO_TRACE
      WK_DEBUGF("DBG: RX_CTL <00><%02X>  (HOST_OPEN)\n", (unsigned)p);
#endif
      const bool prevOpen = _hostOpen;
      bool newOpen = _hostOpen;
      bool change  = false;

      if (p == 0x02) {           // CONNECT
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

        // Suppress unsolicited TX during handshake
        _suppressUnsolicitedUntilMs = millis() + 1500;

        // Prevent an immediate pot notification right after CONNECT.
        _lastPotSentRaw  = mapWpmToPotRaw(_k.getWpm());

        // Per WinKeyer v2 spec: on HOST OPEN/CONNECT, send a single
        // firmware revision byte back to the host.
        if (_w) {
          _w->writeByte(WK_REVISION_CODE);
#if WK_INFO_TRACE
          WK_DEBUGF("WK: send revision 0x%02X on CONNECT\n",
                    (unsigned)WK_REVISION_CODE);
#endif
        }

        // Arm ASCII squelch
        _asciiSquelch        = true;
        _lastArmLogWpm       = 0xFF;
        _asciiSquelchUntilMs = millis() + ASCII_SQUELCH_MS;
        _asciiReadyBlipArmed = true;

        // Capture baseline WPM at the moment of CONNECT.
        _baselineWpm   = _k.getWpm();
        _baselineSaved = true;

      } else if (p == 0x03) {    // DISCONNECT request from host (admin close)
        onProtoClosed();
        return;
      } else if (p == 0x04) {    // EchoTest
        _adminEchoPending = true;
#if WK_INFO_TRACE
        WK_DEBUGLN(F("WK: HOST OPEN EchoTest armed"));
#endif
        return;
      } else if (p == 0x09) {    // SkookumLogger probe
        if (_w) _w->writeByte(WK_REVISION_CODE);
        _suppressUnsolicitedUntilMs = millis() + 1500;
        return;

      } else if (p == 0x0B) {    // ReadKeyerType expects param
        _adminKeyerTypePending = true;
        _suppressUnsolicitedUntilMs = millis() + 1500;
        return;

      } else if (p == 0x0A) {    // Minor version
        if (_w) _w->writeByte((uint8_t)(WK_REVISION_CODE & 0x0F)); 
        _suppressUnsolicitedUntilMs = millis() + 1500;
        return;

      } else if (p == 0x17) {
        // COMPAT: Some hosts use <00><17> as ReadMinorVersion
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
        // WK3 Admin: Set High Baud (9600)
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

        // Do not transmit status bytes during CONNECT
        _lastStatusSent = WK_STATUS_TAG;
      }
      return;
    }

    case WK_CMD_SIDETONE: { // <01><0/1>
      break;
    }

    case WK_CMD_SET_WPM_IMM: { // <02><wpm>
      uint8_t w = p;
      if (w < TM_WK_WPM_MIN) w = TM_WK_WPM_MIN;
      if (w > TM_WK_WPM_MAX) w = TM_WK_WPM_MAX;
      setWpmProvenance(w, WpmOrigin::HostImmediate, F("host <02>"));
      _armedSpeedValid = false;
      recordHostSetWpm(w);
#if WK_RX_TRACE
      WK_DEBUGF("DBG: RX_CTL <02><%02X>  (SetWPM)\n", (unsigned)p);
#endif
#if WK_INFO_TRACE
      WK_DEBUGF("WK: setWpm=%u [immediate]\n", (unsigned)w);
#endif
      break;
    }

    case WK_CMD_SET_WEIGHT: { // <03><weight>
      uint8_t w = p;
      if (w < 25) w = 25;
      if (w > 75) w = 75;
      setWeight(w);
#if WK_INFO_TRACE
      WK_DEBUGF("WK: weight=%u\n", (unsigned)w);
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

    case WK_CMD_SET_PTT_DELAYS: {
      const uint8_t lead = _immBuf[0];
      const uint8_t tail = _immBuf[1];
      _pttLeadMs = (uint16_t)lead;
      _pttTailMs = (uint16_t)tail;
      break;
    }


    case WK_CMD_SET_POT_LIMITS: {
      // WK2: <05><minwpm><range><unused>
      uint8_t hostMin   = _immBuf[0];
      uint8_t hostRange = _immBuf[1];

      // Clamp to a sane numeric range
      if (hostMin < 5)   hostMin = 5;
      if (hostMin > 99)  hostMin = 99;

      if (hostRange < 1) hostRange = 1;
      if (hostRange > 99) hostRange = 99;

      uint16_t maxWpm = (uint16_t)hostMin + (uint16_t)hostRange;
      if (maxWpm > 99) {
          hostRange = (uint8_t)(99 - hostMin);
          if (hostRange < 1) hostRange = 1;
      }

#if WK_INFO_TRACE
      WK_DEBUGF("WK: POT LIMITS host MIN=%u RANGE=%u (raw min=%u range=%u)\n",
                (unsigned)hostMin, (unsigned)hostRange,
                (unsigned)_immBuf[0], (unsigned)_immBuf[1]);
#endif

      if (hostMin >= 10 && hostRange >= 30) {
        _potMinWpm   = hostMin;
        _potRangeWpm = hostRange;
      } else {
        _potMinWpm   = 10;
        _potRangeWpm = 50;
      }

      break;
    }

    case WK_CMD_GET_SPEED_POT: {
      break;
    }

    case WK_CMD_SET_OUTPUTS: {
      break;
    }

    case WK_CMD_SET_COMP: {
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
  // Send C4 if we just transitioned into "busy"
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

  // When both RX FIFO and keyer TXQ are empty, send C0 once.
  if (!rxEmpty()) return;
  if (_k.isBusy()) return;

  // Debounce idle
  const uint32_t now = millis();
  if (_lastTxActivityMs && (int32_t)(now - _lastTxActivityMs) < 30) return;

  // Apply pending speed if armed but unused
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
    _w->writeByte(WK_STATUS_TAG); // 0xC0
    _lastStatusSent   = WK_STATUS_TAG;
    _lastStatusSentMs = millis();
#if WK_RX_TRACE    
    WK_DEBUGLN(F("DBG: TX_STATUS(idle-now) 0xC0"));
#endif
  }
}

void TM_WK_Protocol::onProtoClosed() {
  // Protocol level close (WinKeyer reset)
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

  // Restore baseline
  if (_baselineSaved) {
    setWpmProvenance(_baselineWpm, WpmOrigin::BaselineRestore, F("baseline restore"));
    recordHostSetWpm(_baselineWpm);  // suppress echo
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
  // Transport closed (TCP disconnect)
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

  // Observe WPM changes that did NOT originate from the protocol core.
  const uint8_t cur = _k.getWpm();
  if (_lastObservedWpm == 0xFF) {
    _lastObservedWpm = cur;
  }
  if (cur != _lastObservedWpm) {
    uint32_t now    = millis();
    uint32_t dtHost = now - _lastHostSetMs;

    const bool hostRecentlyDrove =
        (_lastWpmOrigin == WpmOrigin::HostImmediate) &&
        (_lastHostSetWpm == cur) &&
        (dtHost <= HOST_ECHO_WINDOW_MS);

    const bool macroSpeed =
        (_lastWpmOrigin == WpmOrigin::BufferedApplyAscii ||
        _lastWpmOrigin == WpmOrigin::BufferedApplyIdle);

    const bool localChange = !(hostRecentlyDrove || macroSpeed);

    if (localChange) {
      _baselineWpm   = cur;
      _baselineSaved = true;
#if WK_INFO_TRACE
      WK_DEBUGF("WK: baseline updated to %u due to local change\n", (unsigned)cur);
#endif

      // Notify host immediately about local WPM changes via pot status.
      if (_hostOpen && _w) {
        sendPotStatus(0 /*ignored*/, cur);
      }
    }

    _lastObservedWpm = cur;
    _lastWpmOrigin   = WpmOrigin::None;
  }

  // Defensive: drop orphaned buffered opcode
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

  // Wait hold from <1A><nn>?
  if (_txHoldUntilMs) {
    uint32_t now = millis();
    if ((int32_t)(now - _txHoldUntilMs) < 0) {
      // Still waiting
      return;
    }
    _txHoldUntilMs = 0;
  }
  // Always peel consecutive buffered controls at the head before any ASCII work.
  for (;;) {
    uint8_t b0;
    if (!rxPeek(0, b0)) break;

    // Speed: <1C><WPM> — arm and continue peeling
    if (b0 == WK_CMD_BUF_SPEED) {
      uint8_t b1;
      if (!rxPeek(1, b1)) {
        return; // wait for parameter
      }

      uint8_t tmp; rxPop(tmp); rxPop(tmp);
      uint8_t w = b1;
      if (w < TM_WK_WPM_MIN) w = TM_WK_WPM_MIN;
      if (w > TM_WK_WPM_MAX) w = TM_WK_WPM_MAX;
      _armedSpeedWpm   = w;
      _armedSpeedValid = true;
#if WK_INFO_TRACE
      if (_lastArmLogWpm != w) {
        WK_DEBUGF("WK: BUF_SPEED armed=%u\n", (unsigned)w);
        _lastArmLogWpm = w;
      }
#endif
      _lastArmLogWpm = w;
      continue;
    }

    // PTT: <18><flags> — execute immediately and continue peeling
    if (b0 == WK_CMD_BUF_PTT) {
      uint8_t b1;
      if (!rxPeek(1, b1)) {
        return; // wait for parameter
      }
      uint8_t tmp; rxPop(tmp); rxPop(tmp);
      if (b1 & 0x01) {
        _k.startPTT();
      } else {
        _k.stopPTT();
      }
      continue;
    }

    // WAIT: <1A><nn> — set hold and stop this round
    if (b0 == WK_CMD_BUF_WAIT) {
      uint8_t b1;
      if (!rxPeek(1, b1)) {
        return; // wait for parameter
      }
      uint8_t tmp; rxPop(tmp); rxPop(tmp);
      uint32_t ms = (uint32_t)b1 * TM_WK_WAIT_UNIT_MS;
      _txHoldUntilMs = millis() + ms;
      return; // hold active
    }

    break;
  }


  // Dequeue & execute from unified FIFO
  decodeAndExecute();

  // Edge to idle if everything is drained
  sendStatusIdleIfPossible();

}

// ========== Unified dequeue/execute ==========
FLASHMEM void TM_WK_Protocol::decodeAndExecute() {
  // 1) Peel any number of consecutive buffered controls
  while (true) {
    uint8_t b0;
    if (!rxPeek(0, b0)) break;

    if (b0 == WK_CMD_BUF_SPEED) {
      uint8_t b1;
      if (!rxPeek(1, b1)) return;
      uint8_t tmp;
      rxPop(tmp); rxPop(tmp);
      uint8_t w = b1;
      if (w < TM_WK_WPM_MIN) w = TM_WK_WPM_MIN;
      if (w > TM_WK_WPM_MAX) w = TM_WK_WPM_MAX;
      _armedSpeedWpm   = w;
      _armedSpeedValid = true;
      continue;
    }

    if (b0 == WK_CMD_BUF_PTT) {
      uint8_t b1;
      if (!rxPeek(1, b1)) return; 
      uint8_t tmp;
      rxPop(tmp); rxPop(tmp);
      if (b1 & 0x01) {
        _k.startPTT();
      } else {
        _k.stopPTT();
      }
      continue;
    }

    if (b0 == WK_CMD_BUF_WAIT) {
      uint8_t b1;
      if (!rxPeek(1, b1)) return; 
      uint8_t tmp;
      rxPop(tmp); rxPop(tmp);
      uint32_t ms = (uint32_t)b1 * TM_WK_WAIT_UNIT_MS;
      _txHoldUntilMs = millis() + ms;
      return; 
    }

    break;
  }

  // 2) ASCII chunk until next control or FIFO empty
  if (rxEmpty()) return;

  uint8_t first;
  if (!rxPeek(0, first)) return;

  if (!isAscii(first)) {
    if (first == WK_CMD_BUF_SPEED || first == WK_CMD_BUF_PTT || first == WK_CMD_BUF_WAIT) {
      return;
    }
    // junk
    uint8_t junk = 0;
    rxPop(junk);
    return;
  }


  // Collect contiguous ASCII
  char out[64];
  size_t n = 0;
  while (n < sizeof(out)) {
    uint8_t b;
    if (!rxPeek(0, b)) break;
    if (!isAscii(b)) break; // stop at next control
    rxPop(b);
    out[n++] = (char)b;
  }

  if (n == 0) return;

  // Apply armed speed exactly at the boundary before the first ASCII
  if (_armedSpeedValid) {
    setWpmProvenance(_armedSpeedWpm, WpmOrigin::BufferedApplyAscii, F("buf <1C> @ascii"));
    _armedSpeedValid = false;
  }
  // Enqueue text downstream
  (void)_k.enqueueText(out, n);
  _lastTxActivityMs = millis();

#if WK_INFO_TRACE
  WK_DEBUGF("WK: TEXT queued n=%u\n", (unsigned)n);
#endif
}

// ========== Unsolicited status ==========
FLASHMEM void TM_WK_Protocol::sendUnsolicited() {
  // Safe default: nothing periodic here
}

// ========== Status byte ==========
FLASHMEM uint8_t TM_WK_Protocol::buildStatusByte() const {
  uint8_t s = WK_STATUS_TAG;

  // Busy when FIFO has data or keyer is working
  if (!rxEmpty() || _k.isBusy()) s |= WK_SBIT_BUSY;

  // XOFF when downstream TXQ is getting full
  uint16_t k_used = (uint16_t)_k.txqUsed();
  uint16_t k_cap  = (uint16_t)_k.txqCapacityBytes();
  if (k_cap && (k_used > (uint16_t)((k_cap * 7U) / 10U))) {
    s |= WK_SBIT_XOFF;
  }

  return s;
}
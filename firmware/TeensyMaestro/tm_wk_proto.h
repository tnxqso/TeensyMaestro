/*
  tm_wk_proto.h

  TeensyMaestro — Community Edition (CE)
  SPDX-License-Identifier: CC-BY-NC-SA-3.0
  SPDX-FileCopyrightText: 2025 TNX QSO

  A community-maintained edition with open-source utilities
  for ham radio enthusiasts, focusing on FlexRadio® and Wavelog integrations.
*/

#pragma once
#include <Arduino.h>
#include "tm_wk_config.h"
#include "tm_wk_debug.h"
#include "tm_wk_iface.h"

// ---------- Debug trace gates ----------
#ifndef WK_RX_TRACE
#define WK_RX_TRACE   0
#endif
#ifndef WK_ENQ_TRACE
#define WK_ENQ_TRACE  0
#endif
#ifndef WK_INFO_TRACE
#define WK_INFO_TRACE 0
#endif
#ifndef WK_WARN_TRACE
#define WK_WARN_TRACE 0
#endif

// --- WK3-style admin queries ---
#ifndef WK3_CMD_READ_MAJOR_VERSION
  #define WK3_CMD_READ_MAJOR_VERSION 0x09
#endif
#ifndef WK3_CMD_READ_KEYER_TYPE
  #define WK3_CMD_READ_KEYER_TYPE    0x0B
#endif

extern "C" void WK_OnCharEcho(uint8_t ch);

class TM_IByteWriter {
public:
  virtual void writeByte(uint8_t b) = 0;
  virtual void write(const uint8_t* data, size_t len) = 0;
  virtual ~TM_IByteWriter() {}
};

#ifndef WK_REVISION_CODE
  #define WK_REVISION_CODE 0x24
#endif

#ifndef WK_STATUS_TAG
  #define WK_STATUS_TAG     0xC0
#endif
#ifndef WK_SBIT_BUSY
  #define WK_SBIT_BUSY      0x04
#endif
#ifndef WK_SBIT_KEYDOWN
  #define WK_SBIT_KEYDOWN   0x02
#endif
#ifndef WK_SBIT_XOFF
  #define WK_SBIT_XOFF      0x01
#endif

// WinKey command bytes
#ifndef WK_CMD_HOST_OPEN
  #define WK_CMD_HOST_OPEN    0x00
#endif
#ifndef WK_CMD_SIDETONE
  #define WK_CMD_SIDETONE     0x01
#endif
#ifndef WK_CMD_SET_WPM_IMM
  #define WK_CMD_SET_WPM_IMM  0x02
#endif
#ifndef WK_CMD_SET_WEIGHT
  #define WK_CMD_SET_WEIGHT   0x03
#endif
#ifndef WK_CMD_SET_PTT_DELAYS
  #define WK_CMD_SET_PTT_DELAYS 0x04
#endif
#ifndef WK_CMD_SET_POT_LIMITS
  #define WK_CMD_SET_POT_LIMITS 0x05
#endif
#ifndef WK_CMD_GET_SPEED_POT
  #define WK_CMD_GET_SPEED_POT 0x07
#endif
#ifndef WK_CMD_SET_OUTPUTS
  #define WK_CMD_SET_OUTPUTS  0x09
#endif

#ifndef WK_CMD_FARNSWORTH
  #define WK_CMD_FARNSWORTH   0x0D
#endif
#ifndef WK_CMD_WK2_MODE
  #define WK_CMD_WK2_MODE     0x0E
#endif
#ifndef WK_CMD_FIRST_EXTENSION
  #define WK_CMD_FIRST_EXTENSION 0x10
#endif
#ifndef WK_CMD_SET_COMP
  #define WK_CMD_SET_COMP     0x11
#endif
#ifndef WK_CMD_STATUS_REQ
  #define WK_CMD_STATUS_REQ   0x15
#endif

#ifndef WK_CMD_BUF_PTT
  #define WK_CMD_BUF_PTT      0x18
#endif
#ifndef WK_CMD_BUF_WAIT
  #define WK_CMD_BUF_WAIT     0x1A
#endif
#ifndef WK_CMD_BUF_SPEED
  #define WK_CMD_BUF_SPEED    0x1C
#endif

#ifndef TM_WK_RXBUF_SIZE
  #define TM_WK_RXBUF_SIZE 2048
#endif
#ifndef TM_WK_WPM_MIN
  #define TM_WK_WPM_MIN 5
#endif
#ifndef TM_WK_WPM_MAX
  #define TM_WK_WPM_MAX 99
#endif
#ifndef TM_WK_WAIT_UNIT_MS
  #define TM_WK_WAIT_UNIT_MS 10UL
#endif

#ifndef WK_CMD_CLEAR_BUF
  #define WK_CMD_CLEAR_BUF 0x0A
#endif

class TM_WK_Protocol {
public:
  TM_WK_Protocol(TM_IKeyer& keyer, TM_IByteWriter& writer);
  ~TM_WK_Protocol();

  static constexpr uint8_t WK_READY_PULSE = 0x17;

  static TM_WK_Protocol* active();         
  void setWriter(TM_IByteWriter& writer) { _w = &writer; }
  uint8_t getWeight() const { return _weight; }
  void    setWeight(uint8_t w) { _weight = w; }
  
  // NEW: Helper to sync physical knob changes to baseline
  void setLocalBaseline(uint8_t wpm);

  void onByte(uint8_t b);
  void onKeyerCharEcho(uint8_t ch) FLASHMEM;
  void poll() FLASHMEM;
  void sendUnsolicited() FLASHMEM;
  uint8_t buildStatusByte() const FLASHMEM;
  void sendStatusIdleNow() FLASHMEM;
  void onTransportClosed() FLASHMEM;  
  void onProtoClosed() FLASHMEM;      

private:
  static TM_WK_Protocol* s_active;

  TM_IKeyer&      _k;
  TM_IByteWriter* _w;

  uint32_t        _lastHostRxMs = 0;
  static constexpr uint16_t ABORT_DROP_QUIET_MS = 120;

  // RX unified FIFO
  uint8_t* _rx;
  bool     _rx_from_extmem = false;
  uint16_t _rhead = 0;
  uint16_t _rtail = 0;

  inline uint16_t rxUsed() const { return (uint16_t)((_rhead - _rtail) & (TM_WK_RXBUF_SIZE - 1)); }
  inline bool     rxEmpty() const { return _rhead == _rtail; }
  inline uint16_t rxNext(uint16_t i) const { return (uint16_t)((i + 1) & (TM_WK_RXBUF_SIZE - 1)); }
  bool rxPush(uint8_t b);
  bool rxPeek(uint16_t offset, uint8_t& out) const;
  bool rxPop(uint8_t& out);
  
  uint32_t _suppressUnsolicitedUntilMs = 0;
  uint8_t  _waitingOpcode = 0;      
  uint32_t _waitingSinceMs = 0;
  bool _rxBufferedParamArmed = false;

  static constexpr uint8_t IMMCMD_NONE = 0xFF; 
  uint8_t  _immCmd  = IMMCMD_NONE;
  uint8_t  _immNeed = 0;       
  uint8_t  _immGot  = 0;       
  uint8_t  _immBuf[4] = {0};   
  bool     _hostOpen = false;

  bool     _adminEchoPending = false;  
  bool     _adminKeyerTypePending = false; 

  uint32_t _adminRequestedBaud = 0;
  bool     _adminBaudPending   = false;

  void onAdminSetBaud(uint32_t baud);

  bool     _armedSpeedValid = false;
  uint8_t  _armedSpeedWpm   = 24;
  uint8_t  _lastArmLogWpm   = 0xFF;
  uint8_t _wkModeReg = 0x00;

  uint8_t  _potMinWpm   = 10;   
  uint8_t  _potRangeWpm = 50;   

  inline uint8_t mapWpmToPotRaw(uint8_t wpm) const {
    int v = int(wpm) - int(_potMinWpm);
    if (v < 0) v = 0;
    int maxRaw = int(_potRangeWpm) - 1;
    if (maxRaw < 0)  maxRaw = 0;
    if (maxRaw > 31) maxRaw = 31;
    if (v > maxRaw) v = maxRaw;
    return uint8_t(v);
  }

  inline uint8_t makePotStatusByte(uint8_t potRaw) const {
    return uint8_t(0x80 | (potRaw & 0x3F)); 
  }

  bool     _baselineSaved    = false;
  uint8_t  _baselineWpm      = 24;
  uint8_t  _weight                = 50;  
  bool     _baselineWeightSaved   = false;
  uint8_t  _baselineWeight        = 50;

  static constexpr uint16_t HOST_ECHO_WINDOW_MS = 400;

  uint8_t  _lastHostSetWpm   = 0xFF;
  uint32_t _lastHostSetMs    = 0;

  uint8_t  _lastReportedWpm  = 0xFF;
  uint32_t _lastWpmNotifyMs  = 0;
  static constexpr uint16_t WPM_NOTIFY_SUPPRESS_MS = 280;  
  static constexpr uint8_t  POT_IMMEDIATE_DELTA    = 3;   

  enum class WpmOrigin : uint8_t {
    None = 0,
    HostImmediate,
    BufferedApplyAscii,
    BufferedApplyIdle,
    BaselineRestore,
  };

  WpmOrigin _lastWpmOrigin = WpmOrigin::None;
  uint8_t   _lastObservedWpm = 0xFF;

  inline void recordHostSetWpm(uint8_t w) {
    _lastHostSetWpm = w;
    _lastHostSetMs  = millis();
  }

  uint8_t  _lastPotSentRaw  = 0xFF;
  uint32_t _lastPotSentMs   = 0;

  inline void sendPotStatus(uint8_t /*potRaw*/, uint8_t curWpm) {
    if (_suppressUnsolicitedUntilMs &&
        (int32_t)(millis() - _suppressUnsolicitedUntilMs) < 0) return;

    const uint8_t raw    = mapWpmToPotRaw(curWpm);
    const uint8_t potMsg = makePotStatusByte(raw);

    _w->writeByte(potMsg);
    _lastPotSentRaw   = raw;
    _lastPotSentMs    = millis();
    _lastReportedWpm  = curWpm;
  }

  uint8_t  _lastStatusSent   = WK_STATUS_TAG;
  uint32_t _lastStatusSentMs = 0;
  uint32_t _lastTxActivityMs = 0;
  uint32_t _txHoldUntilMs = 0;
  uint16_t _pttLeadMs = 0;
  uint16_t _pttTailMs = 0;

  bool     _asciiSquelch = false;
  uint32_t _asciiSquelchUntilMs = 0; 
  static constexpr uint16_t ASCII_SQUELCH_MS = 2000;

  bool     _asciiReadyBlipArmed = false;

  void sendReadyPulse() FLASHMEM;
  void wk_play_ready_blip() FLASHMEM;

  void sendStatusStartIfNeeded() FLASHMEM;
  void sendStatusIdleIfPossible() FLASHMEM;
  void decodeAndExecute() FLASHMEM;
  void handleImmediateCommandByte(uint8_t b) FLASHMEM;
  void handleImmediateParam(uint8_t param) FLASHMEM;

  static bool isAscii(uint8_t b) { return b >= 0x20; }

  inline bool inHostParamParse() const {
    const bool immParse = (_immCmd != IMMCMD_NONE) && (_immNeed > 0) && (_immGot < _immNeed);
    const bool bufParse = _rxBufferedParamArmed; 
    return immParse || bufParse;
  }
  inline void setWpmProvenance(uint8_t w, WpmOrigin origin, const __FlashStringHelper* why) {
    _lastWpmOrigin = origin;
    _k.setWpm(w);
#if WK_INFO_TRACE
    WK_DEBUGF("WK: setWpm=%u origin=%u (%s)\n", (unsigned)w, (unsigned)origin, why);
#endif
  }

};
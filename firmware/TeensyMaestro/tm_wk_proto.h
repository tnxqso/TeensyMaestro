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

#pragma once
#include <Arduino.h>
#include "tm_wk_config.h"
#include "tm_wk_debug.h"
#include "tm_wk_iface.h"

// ---------- Debug trace gates (flip to 1 temporarily when needed) ----------
#ifndef WK_RX_TRACE
#define WK_RX_TRACE   0   // control/status edges (C4/C0 etc)
#endif
#ifndef WK_ENQ_TRACE
#define WK_ENQ_TRACE  0   // FIFO enqueue/incoming bytes
#endif
#ifndef WK_PEEL_TRACE
#define WK_PEEL_TRACE 0   // peel/await-param traces
#endif
#ifndef WK_CHUNK_TRACE
#define WK_CHUNK_TRACE 0  // ASCII CHUNK_START/END + APPLY logs
#endif
#ifndef WK_INFO_TRACE
#define WK_INFO_TRACE 1   // general informational "WK: ..." prints
#endif
#ifndef WK_WARN_TRACE
#define WK_WARN_TRACE 0   // warnings ("overflow", "timeout", junk)
#endif

// --- WK3-style admin queries seen from SkookumLogger (compat shim) ---
#ifndef WK3_CMD_READ_MAJOR_VERSION
  #define WK3_CMD_READ_MAJOR_VERSION 0x09  // <00><09>  returns major version (Skookum expects 3)
#endif

#ifndef WK3_CMD_READ_KEYER_TYPE
  #define WK3_CMD_READ_KEYER_TYPE    0x0B  // <00><0B><pp> returns keyer type (1 byte)
#endif

// C hook (implemented in tm_wk_proto.cpp)
extern "C" void WK_OnCharEcho(uint8_t ch);

class TM_IByteWriter {
public:
  virtual void writeByte(uint8_t b) = 0;
  virtual void write(const uint8_t* data, size_t len) = 0;
  virtual ~TM_IByteWriter() {}
};

/*
  WinKeyer-compatible protocol core — unified FIFO model
  ------------------------------------------------------
  Design philosophy (aligned with K1EL and K3NG behavior):
  • A single RX FIFO carries everything exactly as sent by the host:
      - ASCII characters to be transmitted as CW
      - <1C><nn> : Buffered WPM (applied at next character boundary)
      - <18><nn> : Buffered PTT flags (bit0 = PTT on/off)
      - <1A><nn> : Buffered WAIT (nn * 10 ms)
    No parallel queues, no “pending token” structures, no recovery logic.
    Deterministic order is guaranteed — the host’s byte stream defines the
    precise transmit sequence.

  • Dequeue / poll() rules:
      - Read strictly in FIFO order.
      - Peel any number of consecutive buffered controls before text.
      - Coalesce multiple <1C><nn>; the last value wins.
      - Apply the armed WPM exactly before the next ASCII symbol.
      - Execute <18> (PTT) and <1A> (WAIT) immediately at dequeue.
      - Maintain complete FIFO ordering — never reorder or skip bytes.

  • Status signaling:
      - 0xC4 (“busy”) sent once when system transitions from idle to active.
      - 0xC0 (“idle”) sent once when both FIFO and keyer queue become empty.
      - No periodic status spam — only edge transitions are reported.

  • Immediate commands (e.g. <02><nn> “Set WPM immediate”) are processed
    directly, independent of the FIFO. Buffered (<1C>) and immediate (<02>)
    remain separate concepts, as in the K1EL reference design.

  • HOST_OPEN (<00><02>) and HOST_CLOSE (<00><00>) behave as true immediate
    commands. A sentinel (IMMCMD_NONE = 0xFF) prevents overlap between the
    “no command latched” state and valid command 0x00.

  • The unified FIFO model eliminates race conditions, preserves host intent,
    and simplifies reasoning. Behavior matches the effective state machine
    of original WinKeyer v2 devices: deterministic, byte-serial, and fully
    in-order.
*/

#ifndef WK_REVISION_CODE
  // Reported firmware revision for WinKeyer v2 emulation.
  #define WK_REVISION_CODE 0x24   // pretend to be WK2 v2.4
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

// WinKey command bytes (subset used here)
#ifndef WK_CMD_HOST_OPEN
  #define WK_CMD_HOST_OPEN    0x00  // <00><02>
#endif
#ifndef WK_CMD_SIDETONE
  #define WK_CMD_SIDETONE     0x01  // <01><0/1> Sidetone enable/disable (WK2)
#endif
#ifndef WK_CMD_SET_WPM_IMM
  #define WK_CMD_SET_WPM_IMM  0x02  // immediate WPM (baseline)
#endif
#ifndef WK_CMD_SET_WEIGHT
  #define WK_CMD_SET_WEIGHT   0x03  // immediate
#endif
#ifndef WK_CMD_SET_PTT_DELAYS
  #define WK_CMD_SET_PTT_DELAYS 0x04 // immediate lead/tail
#endif
#ifndef WK_CMD_SET_POT_LIMITS
  #define WK_CMD_SET_POT_LIMITS 0x05 // immediate (placeholder)
#endif
#ifndef WK_CMD_GET_SPEED_POT
  #define WK_CMD_GET_SPEED_POT 0x07 // <07> returns 1 byte raw pot value (0..31) (WK2)
#endif
#ifndef WK_CMD_SET_OUTPUTS
  #define WK_CMD_SET_OUTPUTS  0x09  // immediate (placeholder)
#endif
#ifndef WK_CMD_WK2_MODE
  #define WK_CMD_WK2_MODE     0x0E  // immediate (placeholder)
#endif
#ifndef WK_CMD_SET_COMP
  #define WK_CMD_SET_COMP     0x11  // immediate (placeholder)
#endif
#ifndef WK_CMD_STATUS_REQ
  #define WK_CMD_STATUS_REQ   0x15  // immediate: send status
#endif

// Buffered controls (treated as part of the unified RX FIFO)
#ifndef WK_CMD_BUF_PTT
  #define WK_CMD_BUF_PTT      0x18  // <18><flags>
#endif
#ifndef WK_CMD_BUF_WAIT
  #define WK_CMD_BUF_WAIT     0x1A  // <1A><units10ms>
#endif
#ifndef WK_CMD_BUF_SPEED
  #define WK_CMD_BUF_SPEED    0x1C  // <1C><wpm>
#endif

// Reasonable bounds (adjust if needed)
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
  #define TM_WK_WAIT_UNIT_MS 10UL    // 1A nn -> nn * 10ms
#endif

// ----- Additional WK2 opcodes used by the protocol core -----

/* According to K1EL WinKeyer (WK1/WK2) documentation:
0x0A (LF) = Clear Buffer
  - Abort current message
  - Abort Tune / Pause
  - Clear serial buffer
  - May cut a character mid-element

It is not parameterized
It is valid at any time
*/
#ifndef WK_CMD_CLEAR_BUF
  #define WK_CMD_CLEAR_BUF 0x0A     // <0A> Clear buffer (WK2)
#endif

class TM_WK_Protocol {
public:
  TM_WK_Protocol(TM_IKeyer& keyer, TM_IByteWriter& writer);
  ~TM_WK_Protocol();

  // Optional early "ready" pulse sent immediately after <00><02> (matches K3NG Keyer behavior)
  static constexpr uint8_t WK_READY_PULSE = 0x17;

  static TM_WK_Protocol* active();         // global access from C-hook
  void setWriter(TM_IByteWriter& writer) { _w = &writer; }
  uint8_t getWeight() const { return _weight; }
  void    setWeight(uint8_t w) { _weight = w; }

  void onByte(uint8_t b);
  void onKeyerCharEcho(uint8_t ch) FLASHMEM;
  void poll() FLASHMEM;
  void sendUnsolicited() FLASHMEM;
  uint8_t buildStatusByte() const FLASHMEM;
  void sendStatusIdleNow() FLASHMEM;
  void onTransportClosed() FLASHMEM;  // called by transport when TCP disconnects
  void onProtoClosed() FLASHMEM;      // <00><03> admin close; transport remains open

private:
  static TM_WK_Protocol* s_active;

  TM_IKeyer&      _k;
  TM_IByteWriter* _w;

  uint32_t        _lastHostRxMs = 0;

  // Tunables: conservative defaults; long enough to eat a macro burst,
  // short enough to not annoy normal typing.
  static constexpr uint16_t ABORT_DROP_QUIET_MS = 120;  // release when no RX for this long

  // ===== RX unified FIFO =====
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
  
  // Suppress unsolicited TX (status edges, pot reports) during host handshake
  uint32_t _suppressUnsolicitedUntilMs = 0;

  // Anti-stall for orphaned buffered opcodes (defensive only)
  uint8_t  _waitingOpcode = 0;      // 0 if none, else 0x1C/0x18/0x1A
  uint32_t _waitingSinceMs = 0;

  // Ingress guard: if previous byte was a buffered opcode (<1C>/<18>/<1A>),
  // the next byte is a parameter and must never be dropped by ASCII squelch.
  bool _rxBufferedParamArmed = false;

  // ===== Immediate command latch (variable parameter length) =====
  static constexpr uint8_t IMMCMD_NONE = 0xFF; // sentinel (distinct from 0x00)
  uint8_t  _immCmd  = IMMCMD_NONE;
  uint8_t  _immNeed = 0;       // how many parameter bytes are expected
  uint8_t  _immGot  = 0;       // how many parameter bytes have been collected
  uint8_t  _immBuf[4] = {0};   // enough for 3-byte immediates (0x05) + margin
  bool     _hostOpen = false;

  // ===== Admin / diagnostic helpers =====
  bool     _adminEchoPending = false;  // when true, next byte received will be echoed back
  bool     _adminKeyerTypePending = false; // when true, next byte is param for <00><0B><pp>

    // WK3 Admin baud requests (0x17/0x18). Protocol records the request; transport may apply it.
  uint32_t _adminRequestedBaud = 0;
  bool     _adminBaudPending   = false;

  void onAdminSetBaud(uint32_t baud);

  // ===== Buffered control state (dequeue side) =====
  bool     _armedSpeedValid = false;
  uint8_t  _armedSpeedWpm   = 24;

  // (debug dedupe) last armed value we actually logged
  uint8_t  _lastArmLogWpm   = 0xFF;
  // Last WK2 mode register value (for future use / debug only)
  uint8_t _wkModeReg = 0x00;

// ===== Speed pot emulation (for host GUI updates) =====
// Host expects unsolicited "pot" bytes (10vvvvvv) when operator changes speed.

// ===== Speed pot emulation (for host GUI updates) =====
// Host expects unsolicited "pot" bytes (10vvvvvv) when operator changes speed.
//
// For interoperability with DXLog and other WK2-style hosts:
// - Default MIN/RANGE is 10/50 (WPM 10..60).
// - Host may override via <05><min><range><unused>.
// - Pot raw is 0..31, so RANGE is effectively clamped to 32 steps.
uint8_t  _potMinWpm   = 10;   // DXLog-friendly default
uint8_t  _potRangeWpm = 50;   // DXLog-friendly default (10..60, but 32 steps max)

inline uint8_t mapWpmToPotRaw(uint8_t wpm) const {
  int v = int(wpm) - int(_potMinWpm);
  if (v < 0) v = 0;

  // RANGE is number of WPM steps; raw is 0..31.
  int maxRaw = int(_potRangeWpm) - 1;
  if (maxRaw < 0)  maxRaw = 0;
  if (maxRaw > 31) maxRaw = 31;

  if (v > maxRaw) v = maxRaw;

#if WK_INFO_TRACE
  // TODO: Delete later
  WK_DEBUGF("TODO: mapWpmToPotRaw wpm=%u min=%u range=%u -> raw=%u\n",
            (unsigned)wpm,
            (unsigned)_potMinWpm,
            (unsigned)_potRangeWpm,
            (unsigned)v);
#endif
  return uint8_t(v);
}

inline uint8_t makePotStatusByte(uint8_t potRaw) const {
  return uint8_t(0x80 | (potRaw & 0x3F)); // 10vvvvvv
}

  // ===== WPM sync (baseline + host/local change tracking) =====
  // Baseline WPM captured at <00><02> and restored on transport close.
  bool     _baselineSaved    = false;
  uint8_t  _baselineWpm      = 24;

  // ===== Weight (protocol-ready; no timing effect yet) =====
  uint8_t  _weight                = 50;  // stored via <03><nn>
  bool     _baselineWeightSaved   = false;
  uint8_t  _baselineWeight        = 50;

  // Host-set WPM suppression window: avoid echoing back immediate changes.
  static constexpr uint16_t HOST_ECHO_WINDOW_MS = 400;

  uint8_t  _lastHostSetWpm   = 0xFF;
  uint32_t _lastHostSetMs    = 0;

  // Last WPM we notified the host about (for local/manual dial changes).
  uint8_t  _lastReportedWpm  = 0xFF;
  uint32_t _lastWpmNotifyMs  = 0;

  static constexpr uint16_t WPM_NOTIFY_SUPPRESS_MS = 280;  // longer host-echo suppression

  // Pot status pacing to avoid GUI jitter
  static constexpr uint8_t  POT_IMMEDIATE_DELTA    = 3;    // immediate send if jump >= 3 steps
  // ===== WPM debugging / provenance =====
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

  // Last pot status actually sent
  uint8_t  _lastPotSentRaw  = 0xFF;
  uint32_t _lastPotSentMs   = 0;

  inline void sendPotStatus(uint8_t /*potRaw*/, uint8_t curWpm) {
    if (_suppressUnsolicitedUntilMs &&
        (int32_t)(millis() - _suppressUnsolicitedUntilMs) < 0) return;

    // Always compute raw from current WPM and active MIN/RANGE.
    const uint8_t raw    = mapWpmToPotRaw(curWpm);
    const uint8_t potMsg = makePotStatusByte(raw);

  #if WK_INFO_TRACE
    // TODO: Delete later
    WK_DEBUGF("TODO: SEND pot: wpm=%u min=%u range=%u raw=%u byte=0x%02X\n",
              (unsigned)curWpm,
              (unsigned)_potMinWpm,
              (unsigned)_potRangeWpm,
              (unsigned)raw,
              (unsigned)potMsg);
  #endif

    _w->writeByte(potMsg);
    _lastPotSentRaw   = raw;
    _lastPotSentMs    = millis();
    _lastReportedWpm  = curWpm;

  #if WK_INFO_TRACE
    WK_DEBUGF("WK: potNotify raw=%u -> WPM=%u (byte=0x%02X)\n",
              (unsigned)raw, (unsigned)curWpm, (unsigned)potMsg);
  #endif
  }

  // ===== Flow/Status =====
  uint8_t  _lastStatusSent   = WK_STATUS_TAG;
  uint32_t _lastStatusSentMs = 0;

  // Last time we had outgoing “TX-side” activity (echoed a char or queued text).
  // Used to debounce idle edge (C0) so we don't flap busy/idle between characters.
  uint32_t _lastTxActivityMs = 0;

  // WAIT hold
  uint32_t _txHoldUntilMs = 0;

  // PTT lead/tail config placeholders (immediate 0x04)
  uint16_t _pttLeadMs = 0;
  uint16_t _pttTailMs = 0;

  // ASCII squelch after CONNECT: drop printable ASCII until GET_REV or timeout.
  bool     _asciiSquelch = false;
  uint32_t _asciiSquelchUntilMs = 0; // 0 = no timeout armed
  static constexpr uint16_t ASCII_SQUELCH_MS = 2000;

  bool     _asciiReadyBlipArmed = false;

  void sendReadyPulse() FLASHMEM;
  void wk_play_ready_blip() FLASHMEM;

  // Local helpers
  void sendStatusStartIfNeeded() FLASHMEM;
  void sendStatusIdleIfPossible() FLASHMEM;
  void decodeAndExecute() FLASHMEM;
  void handleImmediateCommandByte(uint8_t b) FLASHMEM;
  void handleImmediateParam(uint8_t param) FLASHMEM;

  // Utilities
  static bool isAscii(uint8_t b) { return b >= 0x20; }

  // True while we are collecting parameters for a multi-byte host command.
  // During this window, we must not emit unsolicited status/pot bytes,
  // or strict hosts may mis-parse the command stream.
  inline bool inHostParamParse() const {
    const bool immParse = (_immCmd != IMMCMD_NONE) && (_immNeed > 0) && (_immGot < _immNeed);
    const bool bufParse = _rxBufferedParamArmed; // next byte is param for <1C>/<18>/<1A>
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


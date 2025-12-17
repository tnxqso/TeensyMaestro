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
#include <NativeEthernet.h>

#include "tm_wk_iface.h"   // TM_IByteWriter
#include "tm_wk_proto.h"   // TM_WK_Protocol
#include "tm_wk_debug.h"   // WK_DEBUGF()
#include "tm_wk_trace.h"   // TM_WK_TraceRX/TX (compile-time gated)

#ifndef WK_TCP_TRACE
#define WK_TCP_TRACE 0
#endif

class TM_WK_TCP : public TM_IByteWriter {
public:
  TM_WK_TCP(uint16_t port, TM_WK_Protocol& proto)
  : _server(port), _proto(proto) {}

  void begin() {
    _server.begin();
    _lastRxMs = millis();
  }

  void poll() {
    // Detect disconnect transition and notify protocol
    if (_client && !_client.connected()) {
      _client.stop();
      _proto.onTransportClosed();  // <-- disconnect beep + cleanup
    }

    // Accept new client if none
    if (!_client || !_client.connected()) {
      EthernetClient c = _server.available();
      if (c) {
        _client = c;
        _lastRxMs = millis();
      }
    }

    // Read incoming bytes
    if (_client && _client.connected()) {
      while (_client.available() > 0) {
        int bi = _client.read();
        if (bi < 0) break;
        _lastRxMs = millis();
        uint8_t b = static_cast<uint8_t>(bi);

        TM_WK_TraceRX(b);  // does nothing unless TM_WK_TRACE_SD == 1

#if WK_TCP_TRACE
        WK_DEBUGF("SRC:TCP -> onByte 0x%02X\n", (unsigned)b);
#endif
        _proto.onByte(b);
      }
    }
  }

  // --- TM_IByteWriter ---
  void writeByte(uint8_t b) override {
    TM_WK_TraceTX(b);
    if (_client && _client.connected()) _client.write(&b, 1);
  }

  void write(const uint8_t* data, size_t len) override {
    for (size_t i = 0; i < len; ++i) {
      TM_WK_TraceTX(data[i]);
    }
    if (_client && _client.connected()) _client.write(data, len);
  }

private:
  EthernetServer   _server;
  EthernetClient   _client;
  TM_WK_Protocol&  _proto;
  uint32_t         _lastRxMs = 0;
};

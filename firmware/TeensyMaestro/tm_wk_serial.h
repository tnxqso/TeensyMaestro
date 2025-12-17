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
#include "tm_wk_debug.h"   // WK_DEBUGF()
#include "tm_wk_proto.h"   // TM_WK_Protocol (also pulls TM_IByteWriter)
#include "tm_wk_trace.h"   // TM_WK_TraceRX/TX (compile-time gated)

class TM_WK_Serial : public TM_IByteWriter {
public:
  TM_WK_Serial(Stream& serial, TM_WK_Protocol& proto)
  : _serial(serial), _proto(proto) {}

  // Transport poll: read bytes from the serial stream and feed protocol.
  // Protocol::poll() is called separately from TM_WK_Bridge::poll().
  void poll() {
    while (_serial.available() > 0) {
      int v = _serial.read();
      if (v < 0) {
        break;
      }
      uint8_t b = static_cast<uint8_t>(v);
      TM_WK_TraceRX(b);   // does nothing unless TM_WK_TRACE_SD == 1
      _proto.onByte(b);
    }
  }

  // --- TM_IByteWriter ---
  void writeByte(uint8_t b) override {
    TM_WK_TraceTX(b);
    _serial.write(&b, 1);
  }

  void write(const uint8_t* data, size_t len) override {
    for (size_t i = 0; i < len; ++i) {
      TM_WK_TraceTX(data[i]);
    }
    _serial.write(data, len);
  }

private:
  Stream&         _serial;
  TM_WK_Protocol& _proto;
};

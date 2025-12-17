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

#include "tm_wk_bridge.h"
#include "tm_wk_debug.h"
#include "tm_wk_tcp.h"
#include "tm_wk_serial.h"
#include "tm_wk_config.h"

// Minimal writer used until a real backend is selected
struct TM_NullWriter : TM_IByteWriter {
  void writeByte(uint8_t) override {}
  void write(const uint8_t*, size_t) override {}
};

namespace {
  TM_KeyerAdapter   g_keyer;                 // Adapter to your Keyer.ino
  TM_NullWriter     g_null_writer;           // Placeholder until a backend is chosen
  TM_WK_Protocol    g_proto(g_keyer, g_null_writer);

  TM_WK_Serial*     g_serial = nullptr;      // Serial backend (optional)
  TM_WK_TCP*        g_tcp    = nullptr;      // TCP backend (DXLog/N1MM)
}

void TM_WK_Bridge::begin(bool enableSerial, bool enableTCP) {
  WK_DBG_BEGIN();
  WK_DEBUGLN(F("WK: Bridge begin"));

  if (enableSerial && !g_serial) {
    static TM_WK_Serial serialBackend(Serial, g_proto);
    g_serial = &serialBackend;
    WK_DEBUGLN(F("WK: Serial backend enabled"));
  }

  if (enableTCP && !g_tcp) {
      static TM_WK_TCP tcpBackend(CFG_WK_TCP_Port, g_proto);
      g_tcp = &tcpBackend;
      WK_DEBUGLN(F("WK: TCP backend enabled"));
  }

  // Select active writer, prefer TCP if available, otherwise Serial
  if (g_tcp) {
    g_proto.setWriter(*g_tcp);
  } else if (g_serial) {
    g_proto.setWriter(*g_serial);
  }
}

void TM_WK_Bridge::beginFromConfig() {
  WK_DBG_BEGIN();
  WK_DEBUGLN(F("WK: Bridge beginFromConfig"));

  // Respect global ON/OFF switch from MMConfig.ini
  if (!CFG_WK_Enable) {
    WK_DEBUGLN(F("WK: Disabled by config (WinKeyer: OFF)"));
    return;
  }

  // Map INI transport to backend enables
  const bool useSerial =
      (CFG_WK_Transport == TM_WK_TRANSPORT_COM);
  const bool useTcp =
      (CFG_WK_Transport == TM_WK_TRANSPORT_TCP);

  WK_DEBUGLN(F("WK: Using config transport (COM=serial, TCP=ethernet)"));
  TM_WK_Bridge::begin(useSerial, useTcp);
}

void TM_WK_Bridge::poll() {
  if (g_serial) g_serial->poll();
  if (g_tcp)    g_tcp->poll();
  g_proto.poll();
}

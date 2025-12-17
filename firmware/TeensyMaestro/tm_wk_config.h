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

enum TM_WK_Transport {
    TM_WK_TRANSPORT_TCP,
    TM_WK_TRANSPORT_COM
};

extern bool             CFG_WK_Enable;
extern TM_WK_Transport  CFG_WK_Transport;
extern uint16_t         CFG_WK_TCP_Port;

// COM transport is not implemented yet; placeholders only
extern String           CFG_WK_COM_Port;
extern uint32_t         CFG_WK_COM_Baud;
extern uint8_t          CFG_WK_COM_DataBits;
extern uint8_t          CFG_WK_COM_StopBits;
extern String           CFG_WK_COM_Parity;

// Dev helper: ignore CR/LF control bytes (0x0D/0x0A) as commands when typing in PuTTY.
// This prevents LF (0x0A) from being interpreted as WK 'CLEAR'.
// Set to 0 for strict WinKey behavior with real clients.
#ifndef TM_WK_DEV_IGNORE_CRLF
#define TM_WK_DEV_IGNORE_CRLF 1
#endif

// Accept printable ASCII in IDLE (for telnet/PuTTY manual testing).
// 0 = production-safe (ignore stray ASCII bytes unless a text command enabled data mode)
// 1 = allow free ASCII (old behavior) — useful when typing directly in PuTTY
#ifndef TM_WK_ALLOW_FREE_ASCII
#define TM_WK_ALLOW_FREE_ASCII 0
#endif

// Allow entering WinKey command bytes from PuTTY as a single hex line.
// Example: "$ 00 02"  -> sends 0x00 0x02 to protocol.
#ifndef TM_WK_DEV_HEX_LINE
#define TM_WK_DEV_HEX_LINE 1
#endif

// Buffer sizes, keep small in RAM1, large in RAM2 (see tm_wk_memory.h)
#ifndef TM_WK_TXBUF_SIZE
#define TM_WK_TXBUF_SIZE 512   // bytes in RAM2 for CW enqueue
#endif

// Default WPM limits
#ifndef TM_WK_WPM_MIN
#define TM_WK_WPM_MIN 5
#endif

#ifndef TM_WK_WPM_MAX
#define TM_WK_WPM_MAX 60
#endif

// ===== Debug switches =====
#ifndef TM_WK_DEBUG
#define TM_WK_DEBUG 0
#endif

// Idle timeout for TCP Winkey connection (milliseconds). Set 0 to disable.
#ifndef TM_WK_TCP_IDLE_TIMEOUT_MS
#define TM_WK_TCP_IDLE_TIMEOUT_MS 300000UL  // 5 minutes
#endif

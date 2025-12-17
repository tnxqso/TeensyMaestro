/*
  TeensyMaestro — Community Edition (CE)
  WinKeyer transport byte trace to SD (compile-time gated).

  IMPORTANT:
  - Never uses Serial (Serial may be the WinKey COM transport).
  - When TM_WK_TRACE_SD == 0, this compiles to no-ops and uses no RAM buffers.
*/

#pragma once
#include <Arduino.h>

#ifndef TM_WK_TRACE_SD
  #define TM_WK_TRACE_SD 0
#endif

#if TM_WK_TRACE_SD
  void TM_WK_TraceBegin();
  void TM_WK_TracePoll();
  void TM_WK_TraceRX(uint8_t b);
  void TM_WK_TraceTX(uint8_t b);
#else
  // Compile out completely when disabled.
  inline void TM_WK_TraceBegin() {}
  inline void TM_WK_TracePoll() {}
  inline void TM_WK_TraceRX(uint8_t) {}
  inline void TM_WK_TraceTX(uint8_t) {}
#endif

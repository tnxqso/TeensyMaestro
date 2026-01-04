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

// ============================================================================
// tm_config.h  —  Global build configuration for TeensyMaestro
// Centralized compile-time switches, logging defaults, and feature toggles.
// Safe to include from both .h and .cpp files.
// ============================================================================

#pragma once

// ---------------------------------------------------------------------------
// 1) Build / platform sanity
// ---------------------------------------------------------------------------
#ifndef ARDUINO
  #error "tm_config.h expects to be compiled in an Arduino/Teensy environment."
#endif

#ifndef USE_CRASH_REPORT
  #define USE_CRASH_REPORT 1
#endif

// ---------------------------------------------------------------------------
// 1.5) Product identity & version
// ---------------------------------------------------------------------------
//
// Centralized identity strings for firmware, used by UI, logs, and metadata.
// These may be overridden at compile time if needed.
//
// Example overrides:
//   -DTR_PRODUCT_NAME=\"TeensyMaestro\" -DTR_EDITION_LONG=\"Community Edition\"
//   -DTR_EDITION_SHORT=\"CE\" -DTM_VERSION=\"0.9.0-beta.1\"
//
#ifndef TM_PRODUCT_NAME
  #define TM_PRODUCT_NAME   "TeensyMaestro"    // Main product name (immutable upstream identity)
#endif

#ifndef TM_EDITION_LONG
  #define TM_EDITION_LONG   "Community Edition" // Human-friendly edition label for UI
#endif

#ifndef TM_EDITION_SHORT
  #define TM_EDITION_SHORT  "CE"               // Short edition tag for compact displays/logs
#endif

#ifndef TM_VERSION
  #define TM_VERSION        "0.9.34"     // Semantic version with prerelease stage
#endif

#ifndef TM_FULL_NAME
  #define TM_FULL_NAME TM_PRODUCT_NAME " — " TM_EDITION_LONG " (" TM_EDITION_SHORT ")"
#endif

#ifndef TM_FULL_NAME_WITH_VERSION
  #define TM_FULL_NAME_WITH_VERSION TM_FULL_NAME " v" TM_VERSION
#endif

#ifndef TM_SHORT_NAME
  #define TM_SHORT_NAME TM_PRODUCT_NAME " (" TM_EDITION_SHORT ")"
#endif

// ---------------------------------------------------------------------------
// 2) Logging master switch (DEBUG) and log levels
// ---------------------------------------------------------------------------
//
// DEBUG controls whether debug macros emit anything at all.
// Default is ON (1) for development; set to 0 for release builds.
//
#ifndef DEBUG
  #define DEBUG 0 // You may also set the DEBUG flag in FlexRigTeensy.h
#endif

// Verbosity levels
#define TM_LOG_NONE     0
#define TM_LOG_ERROR    1
#define TM_LOG_WARN     2
#define TM_LOG_INFO     3
#define TM_LOG_DEBUG    4
#define TM_LOG_VERBOSE  5

// Global log level. Higher = more verbose.
// Default is INFO if not overridden.
#ifndef TM_LOG_LEVEL
  #define TM_LOG_LEVEL TM_LOG_INFO
#endif

// Optional per-module overrides (uncomment to customize)
// #define TM_LOG_LEVEL_TIME    TM_LOG_DEBUG
// #define TM_LOG_LEVEL_NETUTIL TM_LOG_DEBUG
// #define TM_LOG_LEVEL_STUN    TM_LOG_INFO


// ---------------------------------------------------------------------------
// 3) Feature toggles
// ---------------------------------------------------------------------------
//
// Flip to 0 to exclude code and save flash space.
//
#ifndef TM_FEATURE_NTP
  #define TM_FEATURE_NTP 1
#endif

#ifndef TM_FEATURE_STUN
  #define TM_FEATURE_STUN 1
#endif

#ifndef TM_FEATURE_SPOTS
  #define TM_FEATURE_SPOTS 1
#endif


// ---------------------------------------------------------------------------
// 4) Network defaults
// ---------------------------------------------------------------------------
//
// NTP defaults
#ifndef TM_NTP_DEFAULT_SERVER
  #define TM_NTP_DEFAULT_SERVER "pool.ntp.org"
#endif
#ifndef TM_NTP_LOCAL_UDP_PORT
  #define TM_NTP_LOCAL_UDP_PORT 2390
#endif
#ifndef TM_NTP_RESYNC_INTERVAL_MS
  #define TM_NTP_RESYNC_INTERVAL_MS (3600UL * 1000UL)  // 1 hour
#endif
#ifndef TM_NTP_MAX_TRIES
  #define TM_NTP_MAX_TRIES 3
#endif

// STUN defaults
#ifndef TM_STUN_PRIMARY_HOST
  #define TM_STUN_PRIMARY_HOST "stun.stunprotocol.org"
#endif
#ifndef TM_STUN_FALLBACK_HOST
  #define TM_STUN_FALLBACK_HOST "stun.l.google.com"
#endif
#ifndef TM_STUN_PORT
  #define TM_STUN_PORT 3478
#endif


// ---------------------------------------------------------------------------
// 5) Lightweight assert
// ---------------------------------------------------------------------------
//
// When DEBUG==0, assertions are stripped.
// When DEBUG==1, failing assertion will trap in an infinite loop.
//
#if DEBUG
  #ifndef TM_ASSERT
    #define TM_ASSERT(cond) do { if(!(cond)) { while(true) { /* trap */ } } } while(0)
  #endif
#else
  #ifndef TM_ASSERT
    #define TM_ASSERT(cond) do { } while(0)
  #endif
#endif


// ---------------------------------------------------------------------------
// 6) Module-specific log level helper
// ---------------------------------------------------------------------------
//
// Example usage in a module (tm_time.cpp):
//
//   #ifndef TM_THIS_LOG_LEVEL
//     #define TM_THIS_LOG_LEVEL TM_LOG_LEVEL_TIME
//   #endif
//   #ifndef TM_THIS_LOG_LEVEL
//     #define TM_THIS_LOG_LEVEL TM_LOG_LEVEL
//   #endif
//
// Then guard verbose code with:
//   #if (DEBUG && TM_THIS_LOG_LEVEL >= TM_LOG_DEBUG)
//     debugf("something: %d\n", x);
//   #endif
//
// Note: debug macros themselves are defined in tm_logging.h
// Include order should always be:
//   #include "tm_config.h"
//   #include "tm_logging.h"
// ---------------------------------------------------------------------------


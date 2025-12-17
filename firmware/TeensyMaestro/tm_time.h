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
#include "tm_config.h"
#include "tm_logging.h"
#include <Arduino.h>
#include <AceTime.h>  // ace_time::TimeZone, OffsetDateTime, etc.

// ---------------- Public API ----------------

// Call once after Ethernet is up (e.g., end of your network init)
void TMTime_begin(const char* ntp_server = "pool.ntp.org",
                  uint16_t local_udp_port = 2390,
                  uint32_t resync_interval_ms = 3600UL * 1000UL,   // 1h
                  uint8_t max_tries = 3);

// Call regularly from loop() (non-blocking, does internal pacing)
void TMTime_loop();

// Returns true if we have a valid epoch from NTP
bool TMTime_hasTime();

// UTC epoch (seconds since 1970-01-01)
time_t TMTime_nowUTC();

// Attach externally created TimeZone objects (e.g., from setup.ini parsing).
// These pointers must remain valid for the lifetime of the program.
// If not attached, TMTime falls back to UTC for both zones.
void TMTime_attachZones(const ace_time::TimeZone* utc, const ace_time::TimeZone* local);

// Convert a UTC epoch to local epoch using the current attached TZ.
time_t TMTime_toLocalEpoch(time_t utc_epoch);

// Local offset (seconds) at a given UTC epoch, using the configured local TZ.
// Positive means local is ahead of UTC. Falls back to 0 if local TZ invalid.
int32_t TMTime_localOffsetSeconds(time_t utc_epoch);

// Generic, lightweight strftime-like formatter on local time (AceTime-based TZ/DST).
// Supported subset: %%, %a, %b, %d, %-d, %m, %-m, %Y, %H, %-H, %M, %-M, %S, %-S, %I, %-I, %p
// If localTz is not available, falls back to UTC.
void TMTime_strftime(const char* fmt, char* out, size_t n, time_t utc_epoch);

// Debug print of current time status (respects tm_logging.h DEBUG)
void TMTime_debugPrint();

// ---------------- Clock format config (STRICT whitelist) ----------------

enum TMTimeClockFmt : uint8_t {
  TM_FMT_24_HHMM,       // "%H:%M"
  TM_FMT_24_HHMMSS,     // "%H:%M:%S"
  TM_FMT_12_HHMM_AP,    // "%I:%M %p"
};

// App-wide configuration (owned/defined in tm_time.cpp)
extern TMTimeClockFmt CFG_ClockFmt;

// Helpers (DECLARATIONS ONLY here; definitions in tm_time.cpp)
bool TMTime_is12h();       // true for TM_FMT_12_HHMM_AP
bool TMTime_showSeconds(); // true only for TM_FMT_24_HHMMSS

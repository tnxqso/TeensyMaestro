/*
  TeensyMaestro — Community Edition (CE)
  WinKeyer transport byte trace to SD (compile-time gated).

  Format (one event per line):
    <millis> RX <hh>
    <millis> TX <hh>

  Notes:
  - Uses small fixed buffers, no dynamic allocation for line buffering.
  - If SD/file open fails, tracing silently disables itself.
*/

#include "tm_wk_trace.h"

#if TM_WK_TRACE_SD

#include <SD.h>
#include "tm_config.h" // Access to TM_VERSION
#include "tm_time.h"   // Access to TMTime_strftime, TMTime_nowUTC, etc.

// Provided elsewhere in the project.
extern bool TM_SD_Ensure();

static File g_wkTraceFile;
static bool g_traceOk = false;

// Constant for filename to avoid hardcoding strings in multiple places
static const char* kTraceFilename = "WK_TRACE.LOG";

// Small fixed buffer to avoid RAM growth.
static char   g_line[32]; // Increased slightly for safety
static uint32_t g_lastFlushMs = 0;

static inline char hexNib(uint8_t v) {
  v &= 0x0F;
  return (v < 10) ? ('0' + v) : ('A' + (v - 10));
}

static void writeLine(char dir, uint8_t b) {
  if (!g_traceOk) return;

  // Build: "<ms> <dir>X <HH>\n"
  // Example: "123456 RX 7F\n"
  const uint32_t ms = millis();

  // Convert ms to decimal (no sprintf to save flash/stack).
  char tmp[11];
  uint8_t ti = 0;
  uint32_t x = ms;
  do {
    tmp[ti++] = char('0' + (x % 10));
    x /= 10;
  } while (x && ti < sizeof(tmp));

  uint8_t i = 0;
  while (ti > 0 && i < sizeof(g_line) - 10) g_line[i++] = tmp[--ti];

  g_line[i++] = ' ';
  g_line[i++] = dir;      // 'R' or 'T'
  g_line[i++] = (dir == 'R') ? 'X' : 'X'; // keep "RX"/"TX" style
  g_line[i - 1] = 'X';    // explicit
  g_line[i++] = ' ';
  g_line[i++] = hexNib(b >> 4);
  g_line[i++] = hexNib(b);
  g_line[i++] = '\n';
  g_line[i] = 0;

  g_wkTraceFile.write((const uint8_t*)g_line, i);

  const uint32_t now = ms;
  if (now - g_lastFlushMs >= 250) { // flush 4 times/sec while tracing
    g_wkTraceFile.flush();
    g_lastFlushMs = now;
  }
}

void TM_WK_TraceBegin() {
  g_traceOk = false;

  if (!TM_SD_Ensure()) {
    return;
  }

  // Always start with a fresh file to avoid confusing old data
  if (SD.exists(kTraceFilename)) {
      SD.remove(kTraceFilename);
  }

  g_wkTraceFile = SD.open(kTraceFilename, FILE_WRITE);
  if (!g_wkTraceFile) {
    return;
  }

  g_traceOk = true;
  g_lastFlushMs = millis();

  // --- Write Header ---
  g_wkTraceFile.println(F("--- WK TRACE SESSION ---"));
  
  // 1. Version
  g_wkTraceFile.print(F("Firmware: "));
  #ifdef TM_VERSION
    g_wkTraceFile.println(TM_VERSION);
  #else
    g_wkTraceFile.println(F("Unknown"));
  #endif

  // 2. Timestamp (if NTP is synced)
  if (TMTime_hasTime()) {
    char timeBuf[32];
    // Format: YYYY-MM-DD HH:MM:SS (Uses local timezone if configured in tm_time)
    TMTime_strftime("%Y-%m-%d %H:%M:%S", timeBuf, sizeof(timeBuf), TMTime_nowUTC());
    g_wkTraceFile.print(F("Date/Time: "));
    g_wkTraceFile.println(timeBuf);
  } else {
    g_wkTraceFile.println(F("Date/Time: (Not Synced)"));
  }
  
  g_wkTraceFile.println(F("------------------------"));
  g_wkTraceFile.flush();
}

void TM_WK_TracePoll() {
  // Placeholder: currently we only flush on timer during write.
}

void TM_WK_TraceRX(uint8_t b) { writeLine('R', b); }
void TM_WK_TraceTX(uint8_t b) { writeLine('T', b); }

#endif // TM_WK_TRACE_SD
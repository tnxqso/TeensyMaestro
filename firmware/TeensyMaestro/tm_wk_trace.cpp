/*
  TeensyMaestro — Community Edition (CE)
  WinKeyer transport byte trace to SD (compile-time gated).

  Format (one event per line):
    <millis> RX <hh>
    <millis> TX <hh>

  Notes:
  - Uses small fixed buffers, no dynamic allocation.
  - If SD/file open fails, tracing silently disables itself.
*/

#include "tm_wk_trace.h"

#if TM_WK_TRACE_SD

#include <SD.h>

// Provided elsewhere in the project (you showed the implementation).
extern bool TM_SD_Ensure();

static File g_wkTraceFile;
static bool g_traceOk = false;

// Small fixed buffer to avoid RAM growth.
static char   g_line[24]; // enough for "1234567890 RX FF\n"
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

  // Convert ms to decimal (no sprintf).
  char tmp[11];
  uint8_t ti = 0;
  uint32_t x = ms;
  do {
    tmp[ti++] = char('0' + (x % 10));
    x /= 10;
  } while (x && ti < sizeof(tmp));

  uint8_t i = 0;
  while (ti > 0) g_line[i++] = tmp[--ti];

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

  g_wkTraceFile = SD.open("WK_TRACE.LOG", FILE_WRITE);
  if (!g_wkTraceFile) {
    return;
  }

  g_traceOk = true;
  g_lastFlushMs = millis();

  // Write a session separator.
  const char* hdr = "\n--- WK TRACE SESSION ---\n";
  g_wkTraceFile.write((const uint8_t*)hdr, strlen(hdr));
  g_wkTraceFile.flush();
}

void TM_WK_TracePoll() {
  // Placeholder: currently we only flush on timer during write.
  // Keeping this for future expansion (e.g., buffered ring, rotation).
}

void TM_WK_TraceRX(uint8_t b) { writeLine('R', b); }
void TM_WK_TraceTX(uint8_t b) { writeLine('T', b); }

#endif // TM_WK_TRACE_SD

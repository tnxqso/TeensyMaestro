/* =============================================================================
   About GetConfigFile(), InSetup, and MenuActive
   - GetConfigFile() is called exactly in two places, once during setup(), and
     once from the UI menu action “Reload CW Messages”. Both calls read the
     entire MMConfig.ini, not only the CW section.
   - While parsing MMConfig.ini, the global flag InSetup is true. Use
     `&& InSetup` as a guard on all config-key conditionals, so parsing only
     happens during a deliberate config load.
   - In normal runtime InSetup is false, which prevents stray buffers or UI
     strings from being misinterpreted as config keys.
   - MenuActive is true only while the on-device menu is visible. It should be
     used to adapt UI feedback, for example show “Please wait…” during a
     manual reload, but it does not control parsing by itself.
   - Menu changes only alter in-RAM state. Persistence comes exclusively from
     MMConfig.ini via GetConfigFile(). A subsequent reload will overwrite any
     temporary menu toggles that differ from the file.
   - Keep config parsing side-effect free outside InSetup. Display updates and
     runtime logic should avoid modifying configuration when InSetup is false.
   ========================================================================== */

#include "tm_sketch_api.h"
#include "tm_time.h"
#include <AceTime.h>
#include <zonedbx/zone_registry.h>
#include "Display_Driver.h"
#include "ui_clockpanel.h"

extern ace_time::ExtendedZoneManager zoneManager;
using namespace ace_time;

// --- External globals from Ethernet_Routines.ino ---
extern byte   CFG_FlexIp[4];
extern int CFG_FlexControlPort;
extern String CFG_FlexHost;
extern int    Myip[4], MyMask[4], MyGateway[4], MyDNS[4];
extern String tm_strip_inline_comment(const String &s);

extern TimeZone utcTz;
extern TimeZone localTz;

// Global (declared in tm_time.cpp / header as extern)
extern TMTimeClockFmt CFG_ClockFmt;

// --- New Keyer Globals ---
extern int KeyerCompensation;
extern int KeyerFirstExtension;
extern int KeyerFarnsworth;
extern bool KeyerAutospace;

// ------ Lightweight stats carrier for one config load ------
struct ConfigLoadStats {
  bool     sd_ok            = false;
  bool     file_ok          = false;
  bool     from_menu        = false;     // true if MenuActive was true on entry
  uint32_t bytes_read       = 0;
  uint32_t lines_total      = 0;         // includes blank and comment lines
  uint32_t lines_parsed     = 0;         // lines that reached ParseInBuf()
  uint32_t blank_lines      = 0;         // empty after trimming CR/LF and comments
  uint32_t comment_lines    = 0;         // lines starting with ';' after trim
  int32_t  error_line       = -1;        // first line where g_parseError became true
  String   error_message;                // copied from g_parseErrorMsg if provided
};

// Optional, if your parser can signal errors. Provide weak defaults so link succeeds.
volatile bool   __attribute__((weak)) g_parseError = false;
String          __attribute__((weak)) g_parseErrorMsg = "";

// Forward decl you already have
void ParseInBuf();

/***************************** GetConfigFile (void + prints stats at end) ************
 * Reads MMConfig.ini line by line and feeds ParseInBuf().
 * Behavior:
 *  - Shows a “please wait” screen if MenuActive is true when called.
 *  - InSetup = true only at initial load parsing, resets to false before return.
 *  - Robust to missing trailing newline.
 *  - Prints stats to Serial at the very end (using debugf()).
 *************************************************************************************/
FLASHMEM void GetConfigFile()
{
  ConfigLoadStats stats;
  stats.from_menu = MenuActive;

  if (MenuActive)
  {
    tft.fillScreen(COLOR_NAVY);
    tft.setTextColor(COLOR_YELLOW);
    tft.setCursor(0, 0);
    tft.setFont(Arial_18_Bold);
    tft.println("Please wait while MMConfig.ini");
    tft.println("is read from the SD card.");
    tft.println();
  }

  // Initialize SD card via shared helper
  if (!TM_SD_Ensure())
  {
    debugln("SD initialization failed!");
    if (MenuActive) tft.println("SD initialization failed!");
    // Print end-of-function stats summary
    debugf("ConfigLoad: sd_ok=%d file_ok=%d from_menu=%d bytes=%lu lines=%lu parsed=%lu comments=%lu blanks=%lu err_line=%ld\n",
           (int)stats.sd_ok, (int)stats.file_ok, (int)stats.from_menu,
           (unsigned long)stats.bytes_read,
           (unsigned long)stats.lines_total,
           (unsigned long)stats.lines_parsed,
           (unsigned long)stats.comment_lines,
           (unsigned long)stats.blank_lines,
           (long)stats.error_line);
    if (stats.error_line > 0 && stats.error_message.length())
      debugf("ConfigLoad: error_message=%s\n", stats.error_message.c_str());
    return;
  }
  stats.sd_ok = true;
  debugln("SD initialization success.");

  // Open config file
  ConfigFile = SD.open("MMConfig.ini", FILE_READ);
  if (!ConfigFile)
  {
    debugln("Error opening MMConfig.ini");
    if (MenuActive) tft.println("Error opening MMConfig.ini");
    // Print end-of-function stats summary
    debugf("ConfigLoad: sd_ok=%d file_ok=%d from_menu=%d bytes=%lu lines=%lu parsed=%lu comments=%lu blanks=%lu err_line=%ld\n",
           (int)stats.sd_ok, (int)stats.file_ok, (int)stats.from_menu,
           (unsigned long)stats.bytes_read,
           (unsigned long)stats.lines_total,
           (unsigned long)stats.lines_parsed,
           (unsigned long)stats.comment_lines,
           (unsigned long)stats.blank_lines,
           (long)stats.error_line);
    if (stats.error_line > 0 && stats.error_message.length())
      debugf("ConfigLoad: error_message=%s\n", stats.error_message.c_str());
    return;
  }
  stats.file_ok = true;

  debugln("Parsing MMConfig.ini");

  // Read file line-by-line and feed the parser
  InBuf = "";
  InBuf.reserve(256);  // typical line length, reduces heap churn

  g_parseError = false;
  g_parseErrorMsg = "";

  while (ConfigFile.available())
  {
    int r = ConfigFile.read();
    if (r < 0) break;
    char c = (char)r;
    stats.bytes_read++;

    if (c == '\r') continue;

    if (c == '\n')
    {
      stats.lines_total++;

      // Classify quickly for stats (ParseInBuf will still make its own checks)
      String line = InBuf;
      line.trim();
      if (line.length() == 0) {
        stats.blank_lines++;
      } else if (line.charAt(0) == ';') {
        stats.comment_lines++;
      }

      // Feed parser once per line
      ParseInBuf();
      stats.lines_parsed++;

      if (g_parseError && stats.error_line < 0) {
        stats.error_line = (int32_t)stats.lines_total;
        stats.error_message = g_parseErrorMsg;
        // keep going to finish stats, or break here if you prefer early exit
        // break;
      }

      InBuf = "";
      continue;
    }

    InBuf += c;
  }

  // If file did not end with newline, parse the remaining buffer
  if (InBuf.length() > 0)
  {
    stats.lines_total++;

    String line = InBuf;
    line.trim();
    if (line.length() == 0) {
      stats.blank_lines++;
    } else if (line.charAt(0) == ';') {
      stats.comment_lines++;
    }

    ParseInBuf();
    stats.lines_parsed++;

    if (g_parseError && stats.error_line < 0) {
      stats.error_line = (int32_t)stats.lines_total;
      stats.error_message = g_parseErrorMsg;
    }

    InBuf = "";
  }

  // Done parsing
  ConfigFile.close();
  
  {
    char buf[64];
    if (stats.error_line < 0) {
      UI_Info_SetConfigStatus("Config: OK");
    } else {
      snprintf(buf, sizeof(buf), "Config: ERR line %ld", (long)stats.error_line);
      UI_Info_SetConfigStatus(buf);
    }
  }


  InSetup = false;

  // ---- Print stats only at the end of the function ----
  debugf("ConfigLoad: sd_ok=%d file_ok=%d from_menu=%d bytes=%lu lines=%lu parsed=%lu comments=%lu blanks=%lu err_line=%ld\n",
         (int)stats.sd_ok, (int)stats.file_ok, (int)stats.from_menu,
         (unsigned long)stats.bytes_read,
         (unsigned long)stats.lines_total,
         (unsigned long)stats.lines_parsed,
         (unsigned long)stats.comment_lines,
         (unsigned long)stats.blank_lines,
         (long)stats.error_line);
  if (stats.error_line > 0 && stats.error_message.length()) {
      Serial.print("ConfigLoad: error_message=");
      Serial.println(stats.error_message);
  }
  debugln();
}

// ============================================================================
// Parsing helpers with error signalling
// ----------------------------------------------------------------------------
// These helpers parse values from "Key: value" INI-style lines and now also
// report parse errors consistently via `setParseError(key, val, why)`.
//
// Behavior principles:
// - If the key is NOT present in the line, helpers keep previous/default values
//   and DO NOT signal errors.
// - If the key IS present but the value is invalid (bad format, out of range,
//   unknown token), helpers call setParseError(...) and return the previous/
//   default value.
//
// ============================================================================

// Centralized parse-error setter (sets once, first error wins)
static inline void setParseError(const String& key, const String& val, const char* why) {
  if (!g_parseError) {
    g_parseError = true;
    g_parseErrorMsg = "Invalid value for " + key + ": '" + val + "' (" + why + ")";
  }
}

// ---------------------------------------------------------------------------
// tm_collapse_csv_spaces
// Removes only extra ' ' (space) characters that appear immediately after a comma.
// - If there's exactly one space after a comma, it stays as one.
// - If there are multiple spaces after a comma, they collapse to exactly one.
// - If there are zero spaces after a comma, nothing is added.
// - Spaces elsewhere in the string are not touched.
// - Tabs or other non-space characters after a comma are not modified.
// ---------------------------------------------------------------------------
static String tm_collapse_csv_spaces(const String& s) {
  String out;
  out.reserve(s.length());  // we never grow much, just dropping extras

  for (size_t i = 0; i < s.length(); ++i) {
    char c = s[i];
    out += c;

    if (c == ',') {
      // Count only ' ' (space) characters immediately after the comma
      size_t j = i + 1;
      size_t spaceCount = 0;
      while (j < s.length() && s[j] == ' ') { ++spaceCount; ++j; }

      if (spaceCount >= 1) {
        // Keep exactly one space
        out += ' ';
        // Skip all spaces we just handled (including the single one we kept)
        i = j - 1; // for-loop ++i will land on the first non-space char
      }
      // If spaceCount == 0, we do nothing (no insertion)
    }
  }

  return out;
}

// Uniform parsed echo (keeps logs consistent)
static inline void parsedEcho(const String& key, const String& value) {
  debugf("Parsed %s %s\n", key.c_str(), value.c_str());
}

// ----------------------------------------------------------------------------
// ParseBoolYN
// Accepts YES/NO/Y/N/ON/OFF/1/0 (case-insensitive).
// - If key exists and value is none of the above, signals error and returns defaultVal.
// - If key not found (no ':'), returns defaultVal.
// ----------------------------------------------------------------------------
static bool ParseBoolYN(const String& line, bool defaultVal) {
  // Strip inline comments first so ':' detection is reliable.
  String s = tm_strip_inline_comment(line);

  // Find the key/value separator.
  int colon = s.indexOf(':');
  if (colon < 0) return defaultVal;  // no key/value pair on this line

  // Build a clean key label and ensure it ends with ":" for logging.
  String keyLabel = s.substring(0, colon);
  keyLabel.trim();
  if (!keyLabel.endsWith(":")) keyLabel += ":";

  // Extract and normalize the value substring.
  s = s.substring(colon + 1);
  s.trim();

  // Echo parsed key/value (always with a colon in the key label).
  parsedEcho(keyLabel, s);

  // Evaluate boolean tokens (case-insensitive).
  String su = s;
  su.toUpperCase();
  if (su.startsWith("YES") || su.startsWith("Y") || su.startsWith("ON") || su == "1") return true;
  if (su.startsWith("NO")  || su.startsWith("N") || su.startsWith("OFF")|| su == "0") return false;

  // Key present but invalid token -> signal error, keep default.
  setParseError(keyLabel, s, "expected YES/NO/ON/OFF/1/0");
  return defaultVal;
}

static int tm_index_of_key_ci(const String &haystack, const char *key);
static bool tm_parse_ipv4(const String &s, byte out[4]);

// ----------------------------------------------------------------------------
// ParseStringValue
// Parses a "Key: value" style line from the INI.
// - If the key is not found, returns currentVal.
// - Removes inline comments and trims whitespace.
// - If the value is empty, returns currentVal.
// ----------------------------------------------------------------------------
static String ParseStringValue(const String& line,
                               const char* key,
                               const String& currentVal)
{
  int k = tm_index_of_key_ci(line, key);  // case-insensitive key index
  if (k < 0) return currentVal;

  String v = line.substring(k + strlen(key));
  v = tm_strip_inline_comment(v);
  v.trim();
  parsedEcho(key, tm_collapse_csv_spaces(v));

  if (v.length() == 0) {
    setParseError(key, "<empty>", "non-empty string required");
    return currentVal;
  }
  return v;
}

/* ParseIntValue
   - If key not found, returns defaultVal.
   - If present but empty or non-numeric, signals a parse error and returns defaultVal.
   - If range (minVal, maxVal) is non-zero, enforces inclusive bounds.
*/
int ParseIntValue(const String& line, const char* key, int defaultVal, int minVal = 0, int maxVal = 0)
{
  int k = tm_index_of_key_ci(line, key);
  if (k < 0) return defaultVal;

  String v = line.substring(k + strlen(key));
  v = tm_strip_inline_comment(v);
  v.trim();
  parsedEcho(key, v);

  if (v.length() == 0) {
    setParseError(key, "<empty>", "integer required");
    return defaultVal;
  }

  // Validate numeric form
  const size_t len = v.length();
  bool numeric = true;
  for (size_t i = 0; i < len; ++i) {
    char c = v[i];
    if (i == 0 && (c == '+' || c == '-')) continue;
    if (c < '0' || c > '9') { numeric = false; break; }
  }
  if (!numeric) {
    setParseError(key, v, "invalid integer");
    return defaultVal;
  }

  long parsed = v.toInt();

  // Apply independent bounds if provided (0 means 'not set')
  bool out_of_range = false;
  if (minVal != 0 && parsed < minVal) out_of_range = true;
  if (maxVal != 0 && parsed > maxVal) out_of_range = true;
  if (out_of_range) {
    String why = String("must be in range ")
               + (minVal ? String(minVal) : String("-inf"))
               + ".."
               + (maxVal ? String(maxVal) : String("+inf"));
    setParseError(key, v, why.c_str());
    return defaultVal;
  }

  return (int)parsed;
}

/* ParseFloatValue
   - If key not found, returns defaultVal.
   - If present but empty or non-numeric, signals a parse error and returns defaultVal.
   - If range (minVal, maxVal) is non-zero, enforces inclusive bounds.
     NOTE: Passing 0.0 for minVal or maxVal means "no bound" (same convention as ParseIntValue).
*/
float ParseFloatValue(const String& line, const char* key, float defaultVal, float minVal = 0.0f, float maxVal = 0.0f)
{
  int k = tm_index_of_key_ci(line, key);
  if (k < 0) return defaultVal;

  String v = line.substring(k + strlen(key));
  v = tm_strip_inline_comment(v);
  v.trim();
  parsedEcho(key, v);

  if (v.length() == 0) {
    setParseError(key, "<empty>", "float required");
    return defaultVal;
  }

  // Validate numeric form: optional sign, digits, optional single '.', optional digits.
  bool numeric = true;
  int dots = 0;
  for (size_t i = 0; i < v.length(); ++i) {
    char c = v[i];
    if (i == 0 && (c == '+' || c == '-')) continue;
    if (c == '.') { dots++; if (dots > 1) { numeric = false; break; } continue; }
    if (c < '0' || c > '9') { numeric = false; break; }
  }
  if (!numeric) {
    setParseError(key, v, "invalid float");
    return defaultVal;
  }

  float parsed = v.toFloat();

  // Apply independent bounds if provided (0 means 'not set')
  bool out_of_range = false;
  if (minVal != 0.0f && parsed < minVal) out_of_range = true;
  if (maxVal != 0.0f && parsed > maxVal) out_of_range = true;
  if (out_of_range) {
    String why = String("must be in range ")
               + (minVal ? String(minVal, 2) : String("-inf"))
               + ".."
               + (maxVal ? String(maxVal, 2) : String("+inf"));
    setParseError(key, v, why.c_str());
    return defaultVal;
  }

  return parsed;
}

// Validates a small strftime-like subset used for DATE formatting.
// Supported tokens for DATE strings: %%, %a, %b, %d, %-d, %m, %-m, %Y
bool isValidStrfdateFormat(const String& fmt) {
  auto is_numeric_token = [](char t) -> bool {
    // Numeric fields where '-' (no leading zero) is supported for date only
    return (t == 'd' || t == 'm');
  };

  const char* p = fmt.c_str();
  while (*p) {
    if (*p != '%') { ++p; continue; }

    // Saw '%'
    ++p;
    if (!*p) return false; // stray '%' at end

    bool dash = false;
    if (*p == '-') {
      dash = true;
      ++p;
      if (!*p) return false; // '-' at end
    }

    const char t = *p;

    switch (t) {
      case '%':               // literal '%'
      case 'a':               // weekday abbreviation (Sun..Sat)
      case 'b':               // month abbreviation (Jan..Dec)
      case 'Y':               // 4-digit year
      case 'd':               // day of month
      case 'm':               // month number
        // If '-' used, ensure token supports it
        if (dash && !is_numeric_token(t)) return false;
        break;

      default:
        // time tokens (H, M, S, I, p, etc.) are NOT allowed here
        return false;
    }

    ++p; // consume the token character
  }
  return true;
}

// Validates the *time* format from INI against a strict whitelist.
// Allowed: "%H:%M", "%H:%M:%S", "%I:%M %p"
bool isValidStrftimeFormat(const String& fmt) {
  return (fmt == "%H:%M") ||
         (fmt == "%H:%M:%S") ||
         (fmt == "%I:%M %p");
}

// ---------------------------------------------------------------------------
// ParseIPValue
// Parses IPv4 from "Key: a.b.c.d".
// - If key not found, returns false.
// - If key found but value is empty or invalid, signals parse error and returns false.
// - On success, writes out[4] and returns true.
// ---------------------------------------------------------------------------
static bool ParseIPValue(const String& line, const char* key, int out[4]) {
  int k = tm_index_of_key_ci(line, key);
  if (k < 0) return false;

  String v = line.substring(k + strlen(key));
  v = tm_strip_inline_comment(v);
  v.trim();
  parsedEcho(key, v);

  if (v.length() == 0) {
    setParseError(key, "<empty>", "IPv4 required as a.b.c.d");
    return false;
  }

  byte tmp[4];
  if (!tm_parse_ipv4(v, tmp)) {
    setParseError(key, v, "invalid IPv4, expected a.b.c.d");
    return false;
  }

  out[0] = (int)tmp[0];
  out[1] = (int)tmp[1];
  out[2] = (int)tmp[2];
  out[3] = (int)tmp[3];
  return true;
}

// Extracts the value after a case-insensitive "key=" up to next space/EOL.
// Returns empty string if the key isn't present.
static String tm_get_param_ci(const String& line, const char* key_eq) {
  int k = tm_index_of_key_ci(line, key_eq); // e.g., "freq=" or "name="
  if (k < 0) return "";
  int start = k + strlen(key_eq);
  int end = line.indexOf(' ', start);
  if (end < 0) end = line.length();
  String v = line.substring(start, end);
  v = tm_strip_inline_comment(v);
  v.trim();
  return v;
}

static bool isValidSerialFormat(const String &s) {
  if (s.length() != 19) return false;
  for (int i = 0; i < 19; ++i) {
    if (i == 4 || i == 9 || i == 14) {
      if (s[i] != '-') return false;
    } else {
      if (!isDigit(static_cast<unsigned char>(s[i]))) return false;
    }
  }
  return true;
}

// Simple IPv4 dotted-quad validation
static bool isValidIPv4(const String &s) {
  size_t start = 0;
  int parts = 0;
  while (start < s.length()) {
    int dot = s.indexOf('.', start);
    if (dot == -1) dot = s.length();
    if (++parts > 4) return false;
    String part = s.substring(start, dot);
    if (part.length() == 0) return false;
    for (size_t i = 0; i < part.length(); ++i) {
      if (!isDigit(static_cast<unsigned char>(part[i]))) return false;
    }
    int val = part.toInt();
    if (val < 0 || val > 255) return false;
    start = dot + 1;
  }
  return parts == 4;
}

// Hostname validation (letters, digits, hyphens, dots)
static bool isValidHostname(const String &s) {
  if (s.length() < 1 || s.length() > 253) return false;
  size_t start = 0;
  while (start < s.length()) {
    int dot = s.indexOf('.', start);
    if (dot == -1) dot = s.length();
    int len = dot - start;
    if (len < 1 || len > 63) return false;
    char first = s[start];
    char last  = s[dot - 1];
    if (!isalnum(static_cast<unsigned char>(first)) ||
        !isalnum(static_cast<unsigned char>(last))) return false;
    for (int i = start; i < dot; ++i) {
      char c = s[i];
      if (!(isalnum(static_cast<unsigned char>(c)) || c == '-')) return false;
    }
    start = dot + 1;
  }
  return true;
}

// Safe name normalizer: max 16 chars
static String tm_normalize_spot_name(const String& name) {
  String n = name;
  //n.toLowerCase();
  if (n.length() > 16) n = n.substring(0, 16);
  return n;
}

static inline String tm_trim(const String &s) {
  String t = s; t.trim(); return t;
}

static inline bool tm_iequals(const String &a, const String &b) {
  String aa = a, bb = b;
  aa.trim(); bb.trim();
  aa.toLowerCase(); bb.toLowerCase();
  return aa == bb;
}

static bool tm_parse_ipv4(const String &s, byte out[4]) {
  int a = s.indexOf('.');
  int b = s.indexOf('.', a + 1);
  int c = s.indexOf('.', b + 1);
  if (a <= 0 || b <= a + 1 || c <= b + 1) return false;
  int o0 = tm_trim(s.substring(0, a)).toInt();
  int o1 = tm_trim(s.substring(a + 1, b)).toInt();
  int o2 = tm_trim(s.substring(b + 1, c)).toInt();
  int o3 = tm_trim(s.substring(c + 1)).toInt();
  if (o0 < 0 || o0 > 255 || o1 < 0 || o1 > 255 || o2 < 0 || o2 > 255 || o3 < 0 || o3 > 255) return false;
  out[0] = (byte)o0; out[1] = (byte)o1; out[2] = (byte)o2; out[3] = (byte)o3; // Write to variable
  return true;
}

// --- helpers: case-insensitive startsWith

// Case-insensitive "indexOf" for keys like "Flex Control Port:" in a config line
static int tm_index_of_key_ci(const String &haystack, const char *key) {
  String a = haystack;
  String b = String(key);
  a.toLowerCase();
  b.toLowerCase();
  return a.indexOf(b);
}

// ---------------------------------------------------------------------------
// ParseBand
// Parses a comma-separated band row into BandTable[Bnd][0..9].
// ---------------------------------------------------------------------------
void ParseBand(const String& buf, int Bnd) {
  if (Bnd < 0 || Bnd >= BandTable_Max) {
    setParseError("Band", String(Bnd), "invalid band index");
    return;
  }

  int start = 0;
  const int nFields = 10;

  for (int col = 0; col < nFields; ++col) {
    while (start < (int)buf.length() && isspace(buf[start])) start++;

    int end = buf.indexOf(',', start);
    String token = (end >= 0) ? buf.substring(start, end) : buf.substring(start);
    token.trim();

    BandTable[Bnd][col] = token.length() ? token.toInt() : 0;

    if (end < 0) {
      // Fill remaining columns with zeros
      for (int j = col + 1; j < nFields; ++j) BandTable[Bnd][j] = 0;
      break;
    }
    start = end + 1;
  }
}

static bool parseBandRow(const String& line) {
  // Skip leading spaces; if first non-space isn't a digit, it's not a band row.
  int p = 0;
  while (p < (int)line.length() && isspace(line[p])) ++p;
  if (p >= (int)line.length() || !isDigit(line[p])) return false;

  // Map "<key>" -> BandTable row index.
  // NOTE: "60 CW:" and "60 PHONE:" are aliases pointing to 60A indices.
  struct Map { const char* key; uint8_t idx; };
  static const Map M[] = {
    {"160 CW:", 0},   {"160 PHONE:", 1},
    {"80 CW:",  2},   {"80 PHONE:",  3},

    {"60 CW:",   4},  {"60 PHONE:",   5},  // plain 60m aliases -> 60A rows
    {"60A CW:",  4},  {"60A PHONE:",  5},
    {"60B CW:",  6},  {"60B PHONE:",  7},
    {"60C CW:",  8},  {"60C PHONE:",  9},
    {"60D CW:", 10},  {"60D PHONE:", 11},
    {"60E CW:", 12},  {"60E PHONE:", 13},

    {"40 CW:",  14},  {"40 PHONE:",  15},
    {"30 CW:",  16},  {"30 PHONE:",  17},
    {"20 CW:",  18},  {"20 PHONE:",  19},
    {"17 CW:",  20},  {"17 PHONE:",  21},
    {"15 CW:",  22},  {"15 PHONE:",  23},
    {"12 CW:",  24},  {"12 PHONE:",  25},
    {"10 CW:",  26},  {"10 PHONE:",  27},
    {"6 CW:",   28},  {"6 PHONE:",   29},
    {"2 CW:",   30},  {"2 PHONE:",   31},
    {"1.25 CW:",32},  {"1.25 PHONE:",33},
    {"70CM CW:",34},  {"70CM PHONE:",35},
    {"33CM CW:",36},  {"33CM PHONE:",37},
  };

  // Helper: case-insensitive starts-with at position p.
  auto starts_with_key_ci = [&](const String& s, int start, const char* key) -> bool {
    const size_t n = strlen(key);
    if ((size_t)(s.length() - start) < n) return false;
    for (size_t i = 0; i < n; ++i) {
      char a = s[start + (int)i];
      char b = key[i];
      if (a >= 'A' && a <= 'Z') a = a - 'A' + 'a';
      if (b >= 'A' && b <= 'Z') b = b - 'A' + 'a';
      if (a != b) return false;
    }
    return true;
  };

  for (size_t i = 0; i < sizeof(M)/sizeof(M[0]); ++i) {
    if (starts_with_key_ci(line, p, M[i].key)) {
      const size_t keylen = strlen(M[i].key);

      // Slice values strictly after the matched key (supports arbitrary spaces).
      String values = line.substring(p + (int)keylen);
      values.trim();

      // Parse into BandTable row M[i].idx (ParseBand is silent).
      ParseBand(values, (int)M[i].idx);
      return true;
    }
  }

  // Line started with a digit but didn't match a known key: not ours.
  return false;
}

// ---------------------------------------------------------------------------
// ParseInBuf
// Reads and parses a single configuration line from MMConfig.ini.
//
// Processing steps:
//   1. Trim leading/trailing whitespace.
//   2. Discard empty lines or full-line comments starting with ';'.
//   3. Remove any inline comment (everything after the first ';').
//   4. Prepare helper strings:
//        - line    = cleaned input (original casing, used for values)
//        - InBufUC = uppercased copy (used for case-insensitive key search)
//        - InBufLC = lowercased copy (used in some value parsing)
//
// Usage notes:
//   - Always use `line` when extracting values (to preserve case).
//   - Use `InBufUC` (or `InBufLC`) only for key detection.
//   - This function is the central dispatcher: it checks for known keys
//     and updates global config variables accordingly.
//   - Early `return` is used after each successful parse.
//
// ---------------------------------------------------------------------------
void ParseInBuf()
{
  // Work on a local copy
  String line = InBuf;
  line.trim();
  if (line.length() == 0) return;

  // Delete inline comments (from first ';' to end of line)
  int sc = line.indexOf(';');
  if (sc == 0) return;  // full-line comment
  if (sc > 0) line = line.substring(0, sc);
  line.trim();
  if (line.length() == 0) return;

  // Prepare uppercase/lowercase copies for case-insensitive matching
  String InBufUC = line; InBufUC.toUpperCase();
  String InBufLC = line; InBufLC.toLowerCase();

  if (InBuf.indexOf(";") == 0)
  {
    return;
  }

  if (InBuf.indexOf(";") > 0)
  {
    InBuf = InBuf.substring(0, InBuf.indexOf(";"));
  }

  if (InSetup) debugln(tm_collapse_csv_spaces(line));
 
  // Connect: ANY = no specific serial, otherwise a specific serial number, e.g. 1319-9535-6600-0028 for discovery
  if (InSetup && InBufUC.indexOf("CONNECT:") >= 0) {
    String v  = ParseStringValue(line, "Connect:", ConnectSerialNum); // returns the raw value after "Connect:"
    String vu = v;
    vu.toUpperCase();

    if (vu == "ANY") {
      ConnectSerialNum = vu;
    } else if (isValidSerialFormat(v)) {
      ConnectSerialNum = v;
    } else {
      setParseError("Connect:", v, "expected 'ANY' or serial in form 1234-5678-9012-3456");
    }
    return;
  }

  // Connection Mode: Auto | Fixed | Fixed+Failover
  if (InSetup && InBufUC.indexOf("CONNECTION MODE:") >= 0) {
    String v  = ParseStringValue(line, "Connection Mode:", "");
    String vu = v;
    vu.toUpperCase();

    if (vu == "AUTO") {
      CFG_ConnMode = TM_CONN_AUTO;
    } 
    else if (vu == "FIXED") {
      CFG_ConnMode = TM_CONN_FIXED;
    } 
    else if (vu == "FIXED+FAILOVER") {
      CFG_ConnMode = TM_CONN_FIXED_FAILOVER;
    } 
    else {
      setParseError("Connection Mode:", v,
                    "expected 'Auto', 'Fixed', or 'Fixed+Failover'");
    }
    return;
  }

  // Flex Host: hostname or IPv4 
  if (InSetup && InBufUC.indexOf("FLEX HOST:") >= 0) {
    String v = ParseStringValue(line, "Flex Host:", CFG_FlexHost);
    
    if (isValidIPv4(v) || isValidHostname(v)) {
      CFG_FlexHost = v;
    } else {
      setParseError("Flex Host:", v, "expected valid hostname (e.g. localhost, host.example.com) or IPv4 (a.b.c.d)");
    }
    return;
  }

  // --- UI clock & NTP options ---
  // Show Date/Time: Yes|No
  if (InSetup && InBufUC.indexOf("SHOW DATE/TIME:") >= 0) {
    CFG_ShowDateTime = ParseBoolYN(line, CFG_ShowDateTime);
    return;
  }

  // Time Zone: IANA tz database name (e.g., Europe/Stockholm, UTC, GMT+2)
  // Accepts IANA names and extended aliases like "GMT+2". No custom UTC+HH:MM parser.
  if (InSetup && InBufUC.indexOf("TIME ZONE:") >= 0) {
    String v = ParseStringValue(line, "Time Zone:", CFG_TimeZone);  // already trimmed

    utcTz   = zoneManager.createForZoneName("UTC");
    localTz = zoneManager.createForZoneName(v.c_str());
    if (!localTz.isError()) {
      CFG_TimeZone = v;
    } else {
      setParseError("Time Zone", v,
        "must be a valid IANA TZ (e.g. 'Europe/Stockholm') or an alias like 'GMT+2' or 'UTC'");
    }
    return;
  }

  // Date Format: strftime-like pattern
  if (InSetup && InBufUC.indexOf("DATE FORMAT:") >= 0) {
    String v = ParseStringValue(line, "Date Format:", CFG_DateFormat);
    if (isValidStrfdateFormat(v)) {
      CFG_DateFormat = v;
    } else {
      setParseError("Date Format:", v, "expected Allowed: %%H:%%M, %%H:%%M:%%S, %%I:%%M %%p");
    }
    return;
  }

  // Time Format: strftime-like pattern
  if (InSetup && InBufUC.indexOf("TIME FORMAT:") >= 0) {
    String v = ParseStringValue(line, "Time Format:", CFG_TimeFormat);
    if (isValidStrftimeFormat(v)) {
      CFG_TimeFormat = v;
    } else {
      setParseError("Time Format:", v, "Allowed: %%H:%%M, %%H:%%M:%%S, %%I:%%M %%p");
    }
    return;
  }

  // NTP Server: hostname or IPv4 (case-insensitive key, comment-safe)
  if (InSetup && InBufUC.indexOf("NTP SERVER:") >= 0) {
    String v = ParseStringValue(line, "NTP Server:", CFG_NTPServer);
    v.trim();

    if (isValidIPv4(v) || isValidHostname(v)) {
      CFG_NTPServer = v;  // keep exact user input (DNS is case-insensitive anyway)
    } else {
      setParseError("NTP Server:", v,
                    "expected valid hostname (e.g. pool.ntp.org) or IPv4 (a.b.c.d)");
    }
    return;
  }

  // Flex Control Port: <number>   ; optional, defaults to 4992
  if (InBufUC.indexOf("FLEX CONTROL PORT:") >= 0) {
    CFG_FlexControlPort = ParseIntValue(line, "Flex Control Port:", CFG_FlexControlPort, 1, 65535);
    return;
  }

  // --- Callsign ---
  if (InSetup && InBufUC.indexOf("MYCALL:") >= 0) {
    MyCall = ParseStringValue(InBufUC, "MyCall:", MyCall);
    return;
  }

  // --- License class names (1..5) ---
  if (InSetup && InBufUC.indexOf("LICENSE CLASS ") >= 0) {
    for (int i = 0; i < 5; i++) {
      String keyMixed = "License Class " + String(i + 1) + ":";
      String keyUC    = keyMixed; keyUC.toUpperCase();

      if (InBufUC.indexOf(keyUC) >= 0) {
        LClassText[i] = ParseStringValue(line, keyMixed.c_str(), LClassText[i]);
        return;
      }
    }
  }

  // --- Active license selection (maps to LClass index*2) ---
  if (InSetup && InBufUC.indexOf("MYLICENSE:") >= 0) {
    // Parse license string from INI
    MyLicense = ParseStringValue(line, "MyLicense:", MyLicense);

    // Map parsed license to numeric index (LClass)
    LClass = 0; // default
    for (int i = 0; i < 5; i++) {
      if (MyLicense.equalsIgnoreCase(LClassText[i])) {
        LClass = i * 2;
        break;
      }
    }
    return;
  }

  // --- TeensyMaestroID generation (from MAC) ---
  // Even if the INI contains "TeensyMaestroID:" we ignore any supplied value and
  // rebuild TMID from the device MAC in lowercase hex without separators.
  if (InSetup && InBufUC.indexOf("TEENSYMAESTROID:") >= 0) {
    String TeensyMaestroID = ParseStringValue(line, "TeensyMaestroID:", TeensyMaestroID);

    teensyMAC(mac);
    TMID = "";  // ensure we don't append to an existing string
    for (int i = 0; i < 6; i++) {
      if (mac[i] < 0x10) {
        TMID += "0";
      }
      TMID += String(mac[i], HEX); // Arduino String( , HEX ) is lowercase by default
    }
    debugf("TMID: %s\n", TMID.c_str()); 
    return;
  }

  if (InSetup && InBufUC.indexOf("SCREEN SAVE:") >= 0) {
    // Parse screen saver timeout (seconds → ms)
    int parsedSeconds = ParseIntValue(line, "Screen Save:", ScreenSave, 60, 7200);
    ScreenSave = parsedSeconds * 1000;
    return;
  }
  
  if (InSetup && InBufUC.indexOf("SPLASH:") >= 0) {
    // Parse splash screen duration (ms)
    SplashTimer = ParseIntValue(line, "Splash:", SplashTimer);
    return;
  }

  // --- Power Button: Power | Power Fast | Reset ---
  if (InSetup && InBufUC.indexOf("POWER BUTTON:") >= 0) {
    String v = ParseStringValue(line, "Power Button:", PowerBtn);
    String vu = v; 
    vu.toUpperCase();

    if (vu == "POWER" || vu == "POWER FAST" || vu == "RESET") {
      PowerBtn = vu;   // intentionally uppercase which is how it's implemented
    } else {
      setParseError("Power Button", v, "must be 'Power', 'Power Fast' or 'Reset'");
    }
    return;
  }

  // --- WPM (range 5–70) ---
  if (InSetup && InBufUC.indexOf("WPM:") >= 0) {
    // Parse WPM with bounds; if key missing or invalid, keep previous WPM
    CWVal = ParseIntValue(line, "WPM:", WPM, 5, 70);
    WPM          = CWVal;
    ElementLen   = ((1200000 / WPM));
    GotSpeedParm = true;
    return;
  }
  
  // --- New Pro Features Parsing ---
  if (InSetup && InBufUC.indexOf("CW KEYING COMPENSATION:") >= 0) {
    KeyerCompensation = ParseIntValue(line, "CW Keying Compensation:", KeyerCompensation, 0, 255);
    return;
  }

  if (InSetup && InBufUC.indexOf("CW FIRST EXTENSION:") >= 0) {
    KeyerFirstExtension = ParseIntValue(line, "CW First Extension:", KeyerFirstExtension, 0, 255);
    return;
  }

if (InSetup && InBufUC.indexOf("CW FARNSWORTH:") >= 0) {
    // Range 0 (Disabled) or up to 70 WPM. 
    // We parse 0-70 first, then enforce the minimum logic below.
    int val = ParseIntValue(line, "CW Farnsworth:", KeyerFarnsworth, 0, 70);
    
    // If enabled (not 0) but less than 6, clamp to minimum useful speed (6 WPM)
    if (val > 0 && val < 6) {
        val = 6;
    }
    
    KeyerFarnsworth = val;
    return;
  }

  if (InSetup && InBufUC.indexOf("CW AUTOSPACE:") >= 0) {
    KeyerAutospace = ParseBoolYN(line, KeyerAutospace);
    return;
  }

  // --- CW Delay or Hang Time(range 0–2000) ---
  if (InSetup && InBufUC.indexOf("CW DELAY:") >= 0) {
    // Parse WPM with bounds; if key missing or invalid, keep previous WPM
    CWDelay = ParseIntValue(line, "CW Delay:", CWDelay, 0, 2000);
    return;
  }

  if (InSetup && InBufUC.indexOf("SHOW CONTEST SERIAL NUMBER:") >= 0) {
    ShowContestSerialNumber = ParseBoolYN(line, ShowContestSerialNumber);
    return;
  }

  // --- VFO Tracking ---
  if (InSetup && InBufUC.indexOf("VFO TRACKING:") >= 0) {
    // Yes/No/On/Off → bool
    VFOTrack = ParseBoolYN(line, VFOTrack);
    VFOTrackInd = VFOTrack ? "ON" : "OFF";

    if (MenuActive) {
      // Keep existing menu update behavior
      MenuItem[MenuIDX][3] = "VFO Tracking: " + VFOTrackInd;
    }
    return;
  }

  // --- VFO default rates ---
  if (InSetup && InBufUC.indexOf("VFO A SSB DEFAULT RATE:") >= 0) {
    TuningRateSSB[A] = ParseIntValue(line, "VFO A SSB Default Rate:", TuningRateSSB[A]);
    return;
  }
  if (InSetup && InBufUC.indexOf("VFO B SSB DEFAULT RATE:") >= 0) {
    TuningRateSSB[B] = ParseIntValue(line, "VFO B SSB Default Rate:", TuningRateSSB[B]);
    return;
  }
  if (InSetup && InBufUC.indexOf("VFO A CW DEFAULT RATE:") >= 0) {
    TuningRateCW[A] = ParseIntValue(line, "VFO A CW Default Rate:", TuningRateCW[A]);
    return;
  }
  if (InSetup && InBufUC.indexOf("VFO B CW DEFAULT RATE:") >= 0) {
    TuningRateCW[B] = ParseIntValue(line, "VFO B CW Default Rate:", TuningRateCW[B]);
    return;
  }

  // --- VFO default steps ---
  if (InSetup && InBufUC.indexOf("VFO A SSB DEFAULT STEP:") >= 0) {
    VFOStepSSB[A] = ParseIntValue(line, "VFO A SSB Default Step:", VFOStepSSB[A]);
    return;
  }
  if (InSetup && InBufUC.indexOf("VFO B SSB DEFAULT STEP:") >= 0) {
    VFOStepSSB[B] = ParseIntValue(line, "VFO B SSB Default Step:", VFOStepSSB[B]);
    return;
  }
  if (InSetup && InBufUC.indexOf("VFO A CW DEFAULT STEP:") >= 0) {
    VFOStepCW[A] = ParseIntValue(line, "VFO A CW Default Step:", VFOStepCW[A]);
    return;
  }
  if (InSetup && InBufUC.indexOf("VFO B CW DEFAULT STEP:") >= 0) {
    VFOStepCW[B] = ParseIntValue(line, "VFO B CW Default Step:", VFOStepCW[B]);
    return;
  }

  // --- VFO acceleration ---
  if (InSetup && InBufUC.indexOf("VFO ACCELERATION:") >= 0) {
    VFOaccel = ParseBoolYN(line, VFOaccel);
    VFOaccelInd = VFOaccel ? "ON" : "OFF";
    return;
  }
  if (InSetup && InBufUC.indexOf("VFO ACCELERATION FACTOR:") >= 0) {
    AccelFactor = ParseIntValue(line, "VFO Acceleration Factor:", AccelFactor);
    return;
  }
  // --- VFO ACCELERATION: On/Off factors (floats) ---
  if (InSetup && InBufUC.indexOf("VFO ACCEL ON FACTOR:") >= 0) {
    // Valid range: 1.2 .. 2.8 (typical 1.5..2.5). Lower -> earlier engage.
    VFOAccel_OnFactor = ParseFloatValue(line, "VFO Accel On Factor:", VFOAccel_OnFactor, 1.2f, 2.8f);
    return;
  }
  if (InSetup && InBufUC.indexOf("VFO ACCEL OFF FACTOR:") >= 0) {
    // Valid range: 1.0 .. 2.2 (typical 1.3..2.0). Higher -> longer hold before release.
    VFOAccel_OffFactor = ParseFloatValue(line, "VFO Accel Off Factor:", VFOAccel_OffFactor, 1.0f, 2.2f);
    return;
  }

  // --- VFO ACCELERATION: Deadband (int) ---
  if (InSetup && InBufUC.indexOf("VFO ACCEL MIN DEADBAND:") >= 0) {
    // Valid range: 0 .. 12. Lower -> more sensitive at very low speeds.
    // NOTE: min=0 won't be enforced by helper (0 means "no min"); we still clamp the max.
    VFOAccel_MinDeadband = ParseIntValue(line, "VFO Accel Min Deadband:", VFOAccel_MinDeadband, 0, 12);
    return;
  }

  // --- VFO ACCELERATION: EMA denominators (ints) ---
  if (InSetup && InBufUC.indexOf("VFO ACCEL COUNT EMA DEN:") >= 0) {
    // Valid range: 2 .. 6. Higher -> slower to react (smoother).
    VFOAccel_CountEmaDen = ParseIntValue(line, "VFO Accel Count EMA Den:", VFOAccel_CountEmaDen, 2, 6);
    return;
  }
  if (InSetup && InBufUC.indexOf("VFO ACCEL ACCEL EMA DEN:") >= 0) {
    // Valid range: 4 .. 10. Higher -> more glide, less nervous.
    VFOAccel_AccelEmaDen = ParseIntValue(line, "VFO Accel Accel EMA Den:", VFOAccel_AccelEmaDen, 4, 10);
    return;
  }

  // --- VFO ACCELERATION: Slew limits (ints, multipliers of Step) ---
  if (InSetup && InBufUC.indexOf("VFO ACCEL MAX UP MULT:") >= 0) {
    // Valid range: 20 .. 200. Lower -> gentler onset, higher -> faster acceleration.
    VFOAccel_MaxUpMult = ParseIntValue(line, "VFO Accel Max Up Mult:", VFOAccel_MaxUpMult, 20, 200);
    return;
  }
  if (InSetup && InBufUC.indexOf("VFO ACCEL MAX DOWN MULT:") >= 0) {
    // Valid range: 10 .. 60. Lower -> holds momentum longer, higher -> releases faster.
    VFOAccel_MaxDownMult = ParseIntValue(line, "VFO Accel Max Down Mult:", VFOAccel_MaxDownMult, 10, 60);
    return;
  }

  // --- VFO ACCELERATION: Absolute safety cap (int, Hz) ---
  if (InSetup && InBufUC.indexOf("VFO ACCEL MAX ABS HZ:") >= 0) {
    // Valid range: 10000 .. 60000 Hz. Higher can cause jumpy behaviour.
    VFOAccel_MaxAbsHz = ParseIntValue(line, "VFO Accel Max Abs Hz:", VFOAccel_MaxAbsHz, 10000, 60000);
    return;
  }

  // --- Press timings / clicks ---
  if (InSetup && InBufUC.indexOf("LONG PRESS:") >= 0) {
    LongPress = ParseIntValue(line, "Long Press:", LongPress);
    return;
  }
  if (InSetup && InBufUC.indexOf("SHORT PRESS CLICK:") >= 0) {
    ShortPressClick = ParseBoolYN(line, ShortPressClick);
    return;
  }
  if (InSetup && InBufUC.indexOf("LONG PRESS CLICK:") >= 0) {
    LongPressClick = ParseBoolYN(line, LongPressClick);
    return;
  }

  // --- Selected timeout ---
  if (InSetup && InBufUC.indexOf("SELECTED TIMEOUT:") >= 0) {
    SelectedTimeout = ParseIntValue(line, "Selected Timeout:", SelectedTimeout);
    return;
  }

  // --- Encoder steps ---
  if (InSetup && InBufUC.indexOf("VOL A ENCODER STEPS:") >= 0) {
    VolAEncSteps = ParseIntValue(line, "Vol A Encoder Steps:", VolAEncSteps);
    return;
  }

  if (InSetup && InBufUC.indexOf("VOL B ENCODER STEPS:") >= 0) {
    VolBEncSteps = ParseIntValue(line, "Vol B Encoder Steps:", VolBEncSteps);
    return;
  }

  if (InSetup && InBufUC.indexOf("AGC A ENCODER STEPS:") >= 0) {
    AGCAEncSteps = ParseIntValue(line, "AGC A Encoder Steps:", AGCAEncSteps);
    return;
  }

  if (InSetup && InBufUC.indexOf("AGC B ENCODER STEPS:") >= 0) {
    AGCBEncSteps = ParseIntValue(line, "AGC B Encoder Steps:", AGCBEncSteps);
    return;
  }

  if (InSetup && InBufUC.indexOf("LOW A ENCODER STEPS:") >= 0) {
    LowAEncSteps = ParseIntValue(line, "Low A Encoder Steps:", LowAEncSteps);
    return;
  }

  if (InSetup && InBufUC.indexOf("LOW B ENCODER STEPS:") >= 0) {
    LowBEncSteps = ParseIntValue(line, "Low B Encoder Steps:", LowBEncSteps);
    return;
  }

  if (InSetup && InBufUC.indexOf("HIGH A ENCODER STEPS:") >= 0) {
    HighAEncSteps = ParseIntValue(line, "High A Encoder Steps:", HighAEncSteps);
    return;
  }

  if (InSetup && InBufUC.indexOf("HIGH B ENCODER STEPS:") >= 0) {
    HighBEncSteps = ParseIntValue(line, "High B Encoder Steps:", HighBEncSteps);
    return;
  }

  if (InSetup && InBufUC.indexOf("CW ENCODER STEPS:") >= 0) {
    CWEncSteps = ParseIntValue(line, "CW Encoder Steps:", CWEncSteps);
    return;
  }

  // --- Button debounce ---
  if (InSetup && InBufUC.indexOf("BUTTON DEBOUNCE:") >= 0) {
    BtnDebounce = ParseIntValue(line, "Button Debounce:", BtnDebounce);
    return;
  }

  // --- Default startup profile ---
  // Ex: "Profile: <SSB - 40m>"
  if (InSetup && InBufUC.indexOf("PROFILE:") >= 0) {
    String raw = ParseStringValue(line, "Profile:", Profile);

    int l = raw.indexOf('<');
    int r = raw.indexOf('>', l + 1);

    if (l >= 0 && r > l) {
      Profile = raw.substring(l + 1, r);
      Profile.trim();
    } else {
      setParseError("Profile", raw, "missing <profile_name>");
    }
    return;
  }

  // Mic Profiles 1 and 2, example: "Mic Profile 1: <- VoiceMeeter>"
  if (InSetup && InBufUC.indexOf("MIC PROFILE ") >= 0) {
    for (int i = 0; i < 2; i++) {
      String keyMixed = "Mic Profile " + String(i + 1) + ":"; // for parse+log
      String keyUC    = keyMixed; keyUC.toUpperCase();        // for match

      if (InBufUC.indexOf(keyUC) >= 0) {
        String raw = ParseStringValue(line, keyMixed.c_str(), MicProf[i]);
        int l = raw.indexOf('<');
        int r = raw.indexOf('>', l + 1);
        if (l >= 0 && r > l) {
          String prof = raw.substring(l + 1, r);
          prof.trim();
          MicProf[i] = prof;
        } else {
          setParseError(keyMixed, raw, "missing <profile_name>");
        }
        return;
      }
    }
  }

  // --- Mic Profile Switch Overrides Global ---
  if (InSetup && InBufUC.indexOf("MIC PROFILE SWITCH OVERRIDES GLOBAL:") >= 0) {
    MicProfOvr = ParseBoolYN(line, MicProfOvr);
    return;
  }

  // --- Menus ON/OFF toggles ---
  if (InSetup && InBufUC.indexOf("FILTER MENU:") >= 0) {
    FilterMenu = ParseBoolYN(line, FilterMenu);
    return;
  }

  if (InSetup && InBufUC.indexOf("PROFILE MENU:") >= 0) {
    ProfileMenu = ParseBoolYN(line, ProfileMenu);
    return;
  }

  if (InSetup && InBufUC.indexOf("MEMORY MENU:") >= 0) {
    MemoryMenu = ParseBoolYN(line, MemoryMenu);
    return;
  }

  if (InSetup && InBufUC.indexOf("CW MENU:") >= 0) {
    CWMenuOpt = ParseBoolYN(line, CWMenuOpt);
    return;
  }

  if (InSetup && InBufUC.indexOf("CW MSG MENU:") >= 0) {
    CWMsgMenuOpt = ParseBoolYN(line, CWMsgMenuOpt);
    return;
  }

  if (InSetup && InBufUC.indexOf("MODE MENU:") >= 0) {
    ModeMenuOn = ParseBoolYN(line, ModeMenuOn);
    return;
  }

  if (InSetup && InBufUC.indexOf("BAND MENU:") >= 0) {
    BandMenuOn = ParseBoolYN(line, BandMenuOn);
    return;
  }

  if (InSetup && InBufUC.indexOf("TRANSMIT MENU:") >= 0) {
    TransmitMenuOn = ParseBoolYN(line, TransmitMenuOn);
    return;
  }

  if (InSetup && InBufUC.indexOf("CLIENT MENU:") >= 0) {
    ClientMenuOn = ParseBoolYN(line, ClientMenuOn);
    return;
  }

  if (InSetup && InBufUC.indexOf("ANTENNA MENU:") >= 0) {
    AntennaMenuOn = ParseBoolYN(line, AntennaMenuOn);
    return;
  }

  // CW Paddles: Right | Left  (only these two; preserves W4WKU special case)
  if (InSetup && InBufUC.indexOf("CW PADDLES:") >= 0) {
    String v = ParseStringValue(line, "CW Paddles:", Handed);
    String vu = v; vu.toUpperCase();

    if (vu == "RIGHT") {
      Handed    = 0;
      HandedTxt = "Right Handed";
      DotPin    = 30;
      DashPin   = 31;
    } else if (vu == "LEFT") {
      Handed    = 1;
      HandedTxt = "Left Handed";
      DotPin    = 31;
      DashPin   = 30;
    } else {
      setParseError("CW Paddles", v, "must be 'Right' or 'Left'");
      return;
    }
    return;
  }

  // CW Mode: A | B | C | S | U  (single-letter; keeps previous if invalid)
  if (InBufUC.indexOf("CW MODE:") >= 0) {
    String v = ParseStringValue(line, "CW Mode:", KeyMode);
    String vu = v; vu.trim(); vu.toUpperCase();
    if (vu.length() > 0) vu = vu.substring(0,1);  // take first letter
    if (vu == "A" || vu == "B" || vu == "C" || vu == "S" || vu == "U") {
      KeyMode = vu;  // store as single-letter string, as before
    } else {
      setParseError("CW Mode", v, "must be one of A, B, C, S, U");
    }
    return;
  }

  // CW Sidetone: ON/OFF  (bool)
  if (InSetup && InBufUC.indexOf("CW SIDETONE:") >= 0) {
    SideTone = ParseBoolYN(line, SideTone);
    return;
  }

  // CW Sidetone Freq: 100..2000 Hz (int)
  if (InSetup && InBufUC.indexOf("CW SIDETONE FREQ:") >= 0) {
    STFreq = ParseIntValue(line, "CW Sidetone Freq:", STFreq, 100, 2000);
    return;
  }

  // Keyer Out: Local | Ethernet (string; only these two allowed)
  if (InSetup && InBufUC.indexOf("KEYER OUT:") >= 0) {
    String v  = ParseStringValue(line, "Keyer Out:", KeyerOut);
    String vu = v; vu.toUpperCase();

    if (vu == "LOCAL" || vu == "ETHERNET") {
      KeyerOut = vu;
    } else {
      setParseError("Keyer Out", v, "must be 'Local' or 'Ethernet'");
      return;
    }
    return;
  }

  // CW Msg Source: Teensy | Flex  (keeps previous if invalid)
  if (InSetup && InBufUC.indexOf("CW MSG SOURCE:") >= 0) {
    String v = ParseStringValue(line, "CW Msg Source:", CWMsgSource);
    String vu = v; vu.toUpperCase();
    if (vu == "TEENSY" || vu == "FLEX") {
      // normalize to canonical capitalization
      CWMsgSource = (vu == "TEENSY") ? "Teensy" : "Flex";
    } else {
      setParseError("CW Msg Source", v, "must be 'Teensy' or 'Flex'");
    }
    return;
  }

  // ===== WinKeyer Emulation =====

  // WinKeyer: ON/OFF
  if (InSetup && InBufUC.indexOf("WINKEYER:") >= 0) {
    CFG_WK_Enable = ParseBoolYN(line, CFG_WK_Enable);
    return;
  }

  // WinKeyer Transport: TCP | COM
  if (InSetup && InBufUC.indexOf("WINKEYER TRANSPORT:") >= 0) {
   String v = ParseStringValue(line, "WinKeyer Transport:", (CFG_WK_Transport == TM_WK_TRANSPORT_TCP) ? "TCP" : "COM");
    String vu = v; vu.toUpperCase();
    if (vu == "TCP") {
      CFG_WK_Transport = TM_WK_TRANSPORT_TCP;
    } else if (vu == "COM") {
      CFG_WK_Transport = TM_WK_TRANSPORT_COM;
    } else {
      setParseError("WinKeyer Transport:", v, "expected TCP or COM");
    }
    return;
  }

  // WinKeyer TCP Port: 8891
  if (InSetup && InBufUC.indexOf("WINKEYER TCP PORT:") >= 0) {
    CFG_WK_TCP_Port = ParseIntValue(line, "WinKeyer TCP Port:", CFG_WK_TCP_Port, 1024, 65535);
    return;
  }

  // Remote Command Server: ON/OFF
  if (InSetup && InBufUC.indexOf("REMOTE COMMAND SERVER:") >= 0) {
    CFG_RCS_Enable = ParseBoolYN(line, CFG_RCS_Enable);
    return;
  }

  // Remote Command TCP Port: 5020
  if (InSetup && InBufUC.indexOf("REMOTE COMMAND TCP PORT:") >= 0) {
    CFG_RCS_TCP_Port = ParseIntValue(line, "Remote Command TCP Port:", CFG_RCS_TCP_Port, 1024, 65535);
    return;
  }

  // Menu Encoder: CW Speed | Mic Gain | RF Power | Tune Power | WNB Level | Mon Level | VOX Level | VOX Delay | Band
  if (InSetup && InBufUC.indexOf("MENU ENCODER:") >= 0) {
    String v = ParseStringValue(line, "Menu Encoder:", Encoder_9);
    String vu = v; vu.toUpperCase();

    if      (vu == "CW SPEED")   { Encoder_9 = Enc9_CWSpeed; }
    else if (vu == "MIC GAIN")   { Encoder_9 = Enc9_MicGain; }
    else if (vu == "RF POWER")   { Encoder_9 = Enc9_RFPower; }
    else if (vu == "TUNE POWER") { Encoder_9 = Enc9_TunePower; }
    else if (vu == "WNB LEVEL")  { Encoder_9 = Enc9_WNBLevel; }
    else if (vu == "MON LEVEL")  { Encoder_9 = Enc9_MonLevel; }
    else if (vu == "VOX LEVEL")  { Encoder_9 = Enc9_VOXLevel; }
    else if (vu == "VOX DELAY")  { Encoder_9 = Enc9_VOXDelay; }
    else if (vu == "BAND")       { Encoder_9 = Enc9_Band; }
    else {
      Encoder_9 = Enc9_CWSpeed;
      setParseError("Menu Encoder", v, "invalid value");
      return;
    }
    return;
  }

  // CW Msg: <string>   (append up to 12, ignore empty)
  // Note: This item can be reloaded outside of setup mode (!InSetup)
  if (InBufUC.indexOf("CW MSG:") >= 0) {
    if (CWMsgNum < 12) {
      String msg = ParseStringValue(line, "CW Msg:", "");
      if (msg.length() > 0) {
        CWMsg[CWMsgNum] = msg;
        CWMsgNum++;
      }
    }
    return;
  }

  // CW Word Spaces: <int> (0–100, then +1 as per legacy behavior)
  // Note: This item can be reloaded outside of setup mode (!InSetup)
  if (InBufUC.indexOf("CW WORD SPACES:") >= 0) {
    int parsed = ParseIntValue(line, "CW Word Spaces:", WordSp - 1, 0, 100);  
    WordSp = parsed + 1;  // legacy quirk: increment after parsing
    return;
  }

  // ShowSpots: Yes|No (accepts Y/N, On/Off etc.)
  if (InSetup && InBufUC.indexOf("SHOWSPOTS:") >= 0) {
    ShowSpots = ParseBoolYN(line, ShowSpots);
    return;
  }

  // SPOT: freq=... name=...
  if (InSetup && InBufUC.indexOf("SPOT:") >= 0) {
    String raw = ParseStringValue(line, "Spot:", "");

    // Extract parameters from the payload
    String freq = tm_get_param_ci(raw, "freq=");
    String name = tm_get_param_ci(raw, "name=");

    // Normalize name
    name = tm_normalize_spot_name(name);

    // Capacity check
    const int SPOT_MAX = (int)(sizeof(SpotFreq) / sizeof(SpotFreq[0]));

    if (SpotIDX < SPOT_MAX) {
      // Store values exactly as before
      SpotFreq[SpotIDX] = freq;
      SpotText[SpotIDX] = name;

      SpotIDX++;
    } else {
      setParseError("Spot", raw, "maximum number of spots reached");
    }
    return;
  }

  // TeensyIP
  if (InSetup && InBufUC.indexOf("TEENSYIP:") >= 0) {
    ParseIPValue(line, "TeensyIP:", Myip);
    return;
  }

  // TeensyGateway
  if (InSetup && InBufUC.indexOf("TEENSYGATEWAY:") >= 0) {
    ParseIPValue(line, "TeensyGateway:", MyGateway);
    return;
  }

  // TeensyMask
  if (InSetup && InBufUC.indexOf("TEENSYMASK:") >= 0) {
    ParseIPValue(line, "TeensyMask:", MyMask);
    return;
  }

  // TeensyDNS
  if (InSetup && InBufUC.indexOf("TEENSYDNS:") >= 0) {
    ParseIPValue(line, "TeensyDNS:", MyDNS);
    return;
  }

  // Out Of Band Indicator: ON/OFF
  if (InSetup && InBufUC.indexOf("OUT OF BAND INDICATOR:") >= 0) {
    OOBindicator = ParseBoolYN(line, OOBindicator);
    return;
  }

  // Out Of Band Spot Time: <int>
  if (InSetup && InBufUC.indexOf("OUT OF BAND SPOT TIME:") >= 0) {
    OOBSpotTime = ParseIntValue(line, "Out Of Band Spot Time:", OOBSpotTime);
    return;
  }

  // Snap to Step: ON/OFF
  if (InSetup && InBufUC.indexOf("SNAP TO STEP:") >= 0) {
    SnapToStep = ParseBoolYN(line, SnapToStep);
    SnapToSteptxt = SnapToStep ? "ON" : "OFF";
    return;
  }

  // Startup Delay: <int>   (keeps raw integer semantics)
  if (InSetup && InBufUC.indexOf("STARTUP DELAY:") >= 0) {
    StartUpDelay = ParseIntValue(line, "Startup Delay:", StartUpDelay);
    return;
  }

  // Disable GUI Client: Yes/No
  if (InSetup && InBufUC.indexOf("DISABLE GUI CLIENT:") >= 0) {
    DisableGUIClient = ParseBoolYN(line, DisableGUIClient);
    return;
  }

  // Display SPI Clock
  if (InSetup && InBufUC.indexOf("DISPLAY SPI CLOCK:") >= 0) {
    // INI specifies MHz; internal SPIClock is in Hz → multiply by 1,000,000
    constexpr long one_million = 1000000L;
    int mhz = ParseIntValue(line, "Display SPI Clock:", SPIClock / one_million, 1, 100);
    SPIClock = mhz * one_million;
    return;
  }

  // Use imperial units
  if (InSetup && InBufUC.indexOf("USE IMPERIAL UNITS:") >= 0) {
    CFG_ImperialUnits = ParseBoolYN(line, CFG_ImperialUnits);
    return;
  }

  // --- Profile Selector: timing -------------------------------------------------
  if (InSetup && InBufUC.indexOf("PROFILE SELECTOR TIMEOUT MS:") >= 0) {
    CFG_Profile_Selector_Timeout_Ms = ParseIntValue(
        line, "Profile Selector Timeout Ms:", CFG_Profile_Selector_Timeout_Ms, 500, 30000);
    return;
  }

  if (InSetup && InBufUC.indexOf("PROFILE SELECTOR CLOSE DELAY MS:") >= 0) {
    CFG_Profile_Selector_Close_Delay_Ms = ParseIntValue(
        line, "Profile Selector Close Delay Ms:", CFG_Profile_Selector_Close_Delay_Ms, 0, 2000);
    return;
  }

  // --- Profile Selector: mode visibility ---------------------------------------
  if (InSetup && InBufUC.indexOf("MODE VISIBLE SSB:") >= 0) {
    CFG_Mode_Visible_SSB = ParseBoolYN(line, CFG_Mode_Visible_SSB);
    return;
  }
  if (InSetup && InBufUC.indexOf("MODE VISIBLE CW:") >= 0) {
    CFG_Mode_Visible_CW = ParseBoolYN(line, CFG_Mode_Visible_CW);
    return;
  }
  if (InSetup && InBufUC.indexOf("MODE VISIBLE FM:") >= 0) {
    CFG_Mode_Visible_FM = ParseBoolYN(line, CFG_Mode_Visible_FM);
    return;
  }
  if (InSetup && InBufUC.indexOf("MODE VISIBLE DIGU:") >= 0) {
    CFG_Mode_Visible_DIGU = ParseBoolYN(line, CFG_Mode_Visible_DIGU);
    return;
  }

  // --- Profile Selector: explicit profile name overrides -----------------------
  // NOTE: detect with InBufUC, but parse from 'line' to preserve case/spacing.

  // CW
  if (InSetup && InBufUC.indexOf("PROFILE MAP CW 160M:") >= 0) {
    CFG_Profile_Map_CW_160M = ParseStringValue(line, "Profile Map CW 160m:", CFG_Profile_Map_CW_160M);
    return;
  }
  if (InSetup && InBufUC.indexOf("PROFILE MAP CW 80M:") >= 0) {
    CFG_Profile_Map_CW_80M  = ParseStringValue(line, "Profile Map CW 80m:",  CFG_Profile_Map_CW_80M);
    return;
  }
  if (InSetup && InBufUC.indexOf("PROFILE MAP CW 60M:") >= 0) {
    CFG_Profile_Map_CW_60M  = ParseStringValue(line, "Profile Map CW 60m:",  CFG_Profile_Map_CW_60M);
    return;
  }
  if (InSetup && InBufUC.indexOf("PROFILE MAP CW 40M:") >= 0) {
    CFG_Profile_Map_CW_40M  = ParseStringValue(line, "Profile Map CW 40m:",  CFG_Profile_Map_CW_40M);
    return;
  }
  if (InSetup && InBufUC.indexOf("PROFILE MAP CW 30M:") >= 0) {
    CFG_Profile_Map_CW_30M  = ParseStringValue(line, "Profile Map CW 30m:",  CFG_Profile_Map_CW_30M);
    return;
  }
  if (InSetup && InBufUC.indexOf("PROFILE MAP CW 20M:") >= 0) {
    CFG_Profile_Map_CW_20M  = ParseStringValue(line, "Profile Map CW 20m:",  CFG_Profile_Map_CW_20M);
    return;
  }
  if (InSetup && InBufUC.indexOf("PROFILE MAP CW 17M:") >= 0) {
    CFG_Profile_Map_CW_17M  = ParseStringValue(line, "Profile Map CW 17m:",  CFG_Profile_Map_CW_17M);
    return;
  }
  if (InSetup && InBufUC.indexOf("PROFILE MAP CW 15M:") >= 0) {
    CFG_Profile_Map_CW_15M  = ParseStringValue(line, "Profile Map CW 15m:",  CFG_Profile_Map_CW_15M);
    return;
  }
  if (InSetup && InBufUC.indexOf("PROFILE MAP CW 12M:") >= 0) {
    CFG_Profile_Map_CW_12M  = ParseStringValue(line, "Profile Map CW 12m:",  CFG_Profile_Map_CW_12M);
    return;
  }
  if (InSetup && InBufUC.indexOf("PROFILE MAP CW 10M:") >= 0) {
    CFG_Profile_Map_CW_10M  = ParseStringValue(line, "Profile Map CW 10m:",  CFG_Profile_Map_CW_10M);
    return;
  }
  if (InSetup && InBufUC.indexOf("PROFILE MAP CW 6M:") >= 0) {
    CFG_Profile_Map_CW_6M   = ParseStringValue(line, "Profile Map CW 6m:",   CFG_Profile_Map_CW_6M);
    return;
  }

  // SSB
  if (InSetup && InBufUC.indexOf("PROFILE MAP SSB 160M:") >= 0) {
    CFG_Profile_Map_SSB_160M = ParseStringValue(line, "Profile Map SSB 160m:", CFG_Profile_Map_SSB_160M);
    return;
  }
  if (InSetup && InBufUC.indexOf("PROFILE MAP SSB 80M:") >= 0) {
    CFG_Profile_Map_SSB_80M  = ParseStringValue(line, "Profile Map SSB 80m:",  CFG_Profile_Map_SSB_80M);
    return;
  }
  if (InSetup && InBufUC.indexOf("PROFILE MAP SSB 60M:") >= 0) {
    CFG_Profile_Map_SSB_60M  = ParseStringValue(line, "Profile Map SSB 60m:",  CFG_Profile_Map_SSB_60M);
    return;
  }
  if (InSetup && InBufUC.indexOf("PROFILE MAP SSB 40M:") >= 0) {
    CFG_Profile_Map_SSB_40M  = ParseStringValue(line, "Profile Map SSB 40m:",  CFG_Profile_Map_SSB_40M);
    return;
  }
  if (InSetup && InBufUC.indexOf("PROFILE MAP SSB 30M:") >= 0) {
    CFG_Profile_Map_SSB_30M  = ParseStringValue(line, "Profile Map SSB 30m:",  CFG_Profile_Map_SSB_30M);
    return;
  }
  if (InSetup && InBufUC.indexOf("PROFILE MAP SSB 20M:") >= 0) {
    CFG_Profile_Map_SSB_20M  = ParseStringValue(line, "Profile Map SSB 20m:",  CFG_Profile_Map_SSB_20M);
    return;
  }
  if (InSetup && InBufUC.indexOf("PROFILE MAP SSB 17M:") >= 0) {
    CFG_Profile_Map_SSB_17M  = ParseStringValue(line, "Profile Map SSB 17m:",  CFG_Profile_Map_SSB_17M);
    return;
  }
  if (InSetup && InBufUC.indexOf("PROFILE MAP SSB 15M:") >= 0) {
    CFG_Profile_Map_SSB_15M  = ParseStringValue(line, "Profile Map SSB 15m:",  CFG_Profile_Map_SSB_15M);
    return;
  }
  if (InSetup && InBufUC.indexOf("PROFILE MAP SSB 12M:") >= 0) {
    CFG_Profile_Map_SSB_12M  = ParseStringValue(line, "Profile Map SSB 12m:",  CFG_Profile_Map_SSB_12M);
    return;
  }
  if (InSetup && InBufUC.indexOf("PROFILE MAP SSB 10M:") >= 0) {
    CFG_Profile_Map_SSB_10M  = ParseStringValue(line, "Profile Map SSB 10m:",  CFG_Profile_Map_SSB_10M);
    return;
  }
  if (InSetup && InBufUC.indexOf("PROFILE MAP SSB 6M:") >= 0) {
    CFG_Profile_Map_SSB_6M   = ParseStringValue(line, "Profile Map SSB 6m:",   CFG_Profile_Map_SSB_6M);
    return;
  }

  // FM
  if (InSetup && InBufUC.indexOf("PROFILE MAP FM 160M:") >= 0) {
    CFG_Profile_Map_FM_160M = ParseStringValue(line, "Profile Map FM 160m:", CFG_Profile_Map_FM_160M);
    return;
  }
  if (InSetup && InBufUC.indexOf("PROFILE MAP FM 80M:") >= 0) {
    CFG_Profile_Map_FM_80M  = ParseStringValue(line, "Profile Map FM 80m:",  CFG_Profile_Map_FM_80M);
    return;
  }
  if (InSetup && InBufUC.indexOf("PROFILE MAP FM 60M:") >= 0) {
    CFG_Profile_Map_FM_60M  = ParseStringValue(line, "Profile Map FM 60m:",  CFG_Profile_Map_FM_60M);
    return;
  }
  if (InSetup && InBufUC.indexOf("PROFILE MAP FM 40M:") >= 0) {
    CFG_Profile_Map_FM_40M  = ParseStringValue(line, "Profile Map FM 40m:",  CFG_Profile_Map_FM_40M);
    return;
  }
  if (InSetup && InBufUC.indexOf("PROFILE MAP FM 30M:") >= 0) {
    CFG_Profile_Map_FM_30M  = ParseStringValue(line, "Profile Map FM 30m:",  CFG_Profile_Map_FM_30M);
    return;
  }
  if (InSetup && InBufUC.indexOf("PROFILE MAP FM 20M:") >= 0) {
    CFG_Profile_Map_FM_20M  = ParseStringValue(line, "Profile Map FM 20m:",  CFG_Profile_Map_FM_20M);
    return;
  }
  if (InSetup && InBufUC.indexOf("PROFILE MAP FM 17M:") >= 0) {
    CFG_Profile_Map_FM_17M  = ParseStringValue(line, "Profile Map FM 17m:",  CFG_Profile_Map_FM_17M);
    return;
  }
  if (InSetup && InBufUC.indexOf("PROFILE MAP FM 15M:") >= 0) {
    CFG_Profile_Map_FM_15M  = ParseStringValue(line, "Profile Map FM 15m:",  CFG_Profile_Map_FM_15M);
    return;
  }
  if (InSetup && InBufUC.indexOf("PROFILE MAP FM 12M:") >= 0) {
    CFG_Profile_Map_FM_12M  = ParseStringValue(line, "Profile Map FM 12m:",  CFG_Profile_Map_FM_12M);
    return;
  }
  if (InSetup && InBufUC.indexOf("PROFILE MAP FM 10M:") >= 0) {
    CFG_Profile_Map_FM_10M  = ParseStringValue(line, "Profile Map FM 10m:",  CFG_Profile_Map_FM_10M);
    return;
  }
  if (InSetup && InBufUC.indexOf("PROFILE MAP FM 6M:") >= 0) {
    CFG_Profile_Map_FM_6M   = ParseStringValue(line, "Profile Map FM 6m:",   CFG_Profile_Map_FM_6M);
    return;
  }

  // DIGU
  if (InSetup && InBufUC.indexOf("PROFILE MAP DIGU 160M:") >= 0) {
    CFG_Profile_Map_DIGU_160M = ParseStringValue(line, "Profile Map DIGU 160m:", CFG_Profile_Map_DIGU_160M);
    return;
  }
  if (InSetup && InBufUC.indexOf("PROFILE MAP DIGU 80M:") >= 0) {
    CFG_Profile_Map_DIGU_80M  = ParseStringValue(line, "Profile Map DIGU 80m:",  CFG_Profile_Map_DIGU_80M);
    return;
  }
  if (InSetup && InBufUC.indexOf("PROFILE MAP DIGU 60M:") >= 0) {
    CFG_Profile_Map_DIGU_60M  = ParseStringValue(line, "Profile Map DIGU 60m:",  CFG_Profile_Map_DIGU_60M);
    return;
  }
  if (InSetup && InBufUC.indexOf("PROFILE MAP DIGU 40M:") >= 0) {
    CFG_Profile_Map_DIGU_40M  = ParseStringValue(line, "Profile Map DIGU 40m:",  CFG_Profile_Map_DIGU_40M);
    return;
  }
  if (InSetup && InBufUC.indexOf("PROFILE MAP DIGU 30M:") >= 0) {
    CFG_Profile_Map_DIGU_30M  = ParseStringValue(line, "Profile Map DIGU 30m:",  CFG_Profile_Map_DIGU_30M);
    return;
  }
  if (InSetup && InBufUC.indexOf("PROFILE MAP DIGU 20M:") >= 0) {
    CFG_Profile_Map_DIGU_20M  = ParseStringValue(line, "Profile Map DIGU 20m:",  CFG_Profile_Map_DIGU_20M);
    return;
  }
  if (InSetup && InBufUC.indexOf("PROFILE MAP DIGU 17M:") >= 0) {
    CFG_Profile_Map_DIGU_17M  = ParseStringValue(line, "Profile Map DIGU 17m:",  CFG_Profile_Map_DIGU_17M);
    return;
  }
  if (InSetup && InBufUC.indexOf("PROFILE MAP DIGU 15M:") >= 0) {
    CFG_Profile_Map_DIGU_15M  = ParseStringValue(line, "Profile Map DIGU 15m:",  CFG_Profile_Map_DIGU_15M);
    return;
  }
  if (InSetup && InBufUC.indexOf("PROFILE MAP DIGU 12M:") >= 0) {
    CFG_Profile_Map_DIGU_12M  = ParseStringValue(line, "Profile Map DIGU 12m:",  CFG_Profile_Map_DIGU_12M);
    return;
  }
  if (InSetup && InBufUC.indexOf("PROFILE MAP DIGU 10M:") >= 0) {
    CFG_Profile_Map_DIGU_10M  = ParseStringValue(line, "Profile Map DIGU 10m:",  CFG_Profile_Map_DIGU_10M);
    return;
  }
  if (InSetup && InBufUC.indexOf("PROFILE MAP DIGU 6M:") >= 0) {
    CFG_Profile_Map_DIGU_6M   = ParseStringValue(line, "Profile Map DIGU 6m:",   CFG_Profile_Map_DIGU_6M);
    return;
  }

  if (parseBandRow(line)) return;

  setParseError("Unrecognized key", line, "no parser matched");

}
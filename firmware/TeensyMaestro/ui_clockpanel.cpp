// UI_ClockPanel.cpp
// Render-only module for the Slice-B clock panel (time row + 6 info lines).
// No business logic here; this file draws on 'tft' and reads a few globals
// (layout anchors + CFG_TimeZone + fRig.slice[B].in_use).

#include "ui_clockpanel.h"
#include "ui_state.h"        // UI-facing state (keeps UI independent of radio model)
#include "Display_Driver.h"
#include "Display_Colors.h"
#include "Display_Fonts.h"
#include <cstring>
#include <cstdlib>

// Injected layout (set via ui_clockpanel_set_layout)
static UIClockLayout s_layout = { /* defaults, safe-ish */ 0, 0, 240, 160};

// Accessors to avoid typo/rename bugs
static inline int L_FreqX()     { return s_layout.freqX; }
static inline int L_FreqY()     { return s_layout.freqY; }
static inline int L_FreqW()     { return s_layout.freqWidth; }
static inline int L_MidScreen() { return s_layout.midScreen; } // may be unused here, kept for consistency

// Config strings
extern String CFG_TimeZone;

// Public setter
void ui_clockpanel_set_layout(const UIClockLayout& layout) {
  s_layout = layout;
}

// ======= Original content from your Untitled-1.cpp (lightly wrapped) =======

// ===== Clock panel (Slice B), top-left anchored =====
#define CLOCK_DEBUG_LAYOUT 0   // 1 = draw red boxes for layout checks

static const int CLOCK_PAD_L = 3;     // left padding
static const int CLOCK_PAD_T = 0;     // top padding
static const int CLOCK_V_SP  = 15;    // vertical gap: time row -> info line #1

// Spacing and fine-tuning
static const int CLOCK_W_EXTRA_TOTAL = 18; // extra spacing distributed between boxes
static const int COLON1_ADJ_X        = 5;  // fine-tune colon #1 horizontally (+right)

// Artifact fix: clear margins left/right when updating tight boxes
static const int CLEAR_MARGIN_X = 3;  // nukes ~2 px residue at right of minutes, etc.

// Fonts
static const ILI9341_t3_font_t& FONT_TIME = Arial_40_Bold;  // HH, MM, colon
static const ILI9341_t3_font_t& FONT_SECS = Arial_32_Bold;  // SS or AM/PM overlay
static const ILI9341_t3_font_t& FONT_INFO = Arial_14;       // 6 information lines

// ---------- Fixed positions computed from prefix bounds of "88:88:88" ----------
struct ClockSegLayout {
  int AX, AY;        // top-left anchor of time row
  int baseX, baseY;  // baseline for FONT_TIME
  int hLine;         // height of "88:88:88"
  int wPair;         // width of "88" in FONT_TIME
  int wColon;        // width of ":"  in FONT_TIME
  int wSecs;         // width of "88" in FONT_SECS
  int offHH, offColon1, offMM, offSS;  // left offsets for boxes
};

static ClockSegLayout s_seg = {};
static bool s_segInit = false;

static void UI_Clock_InitSegLayout() {
  if (s_segInit) return;

  // Slice-B panel is already anchored by the injected layout.
  s_seg.AX = L_FreqX() + CLOCK_PAD_L;
  s_seg.AY = L_FreqY() - 2 + CLOCK_PAD_T;

  tft.setTextWrap(false);

  // --- Measure FONT_TIME blocks ---
  tft.setFont(FONT_TIME);

  int16_t x_all, y_all; uint16_t w_all, h_all;
  tft.getTextBounds("88:88:88", 0, 0, &x_all, &y_all, &w_all, &h_all);

  s_seg.baseX = s_seg.AX - x_all;
  s_seg.baseY = s_seg.AY - y_all;
  s_seg.hLine = (int)h_all;

  int16_t x1, y1; uint16_t bw, bh;
  tft.getTextBounds("88", 0, 0, &x1, &y1, &bw, &bh);
  s_seg.wPair = (int)bw;
  tft.getTextBounds(":",  0, 0, &x1, &y1, &bw, &bh);
  s_seg.wColon = (int)bw;

  auto right_edge = [&](const char* prefix) -> int {
    int16_t xp, yp; uint16_t wp, hp;
    tft.getTextBounds(prefix, 0, 0, &xp, &yp, &wp, &hp);
    return (int)(xp + (int)wp);
  };
  const int r_88     = right_edge("88");
  const int r_88c    = right_edge("88:");
  const int r_88c88  = right_edge("88:88");

  tft.setFont(FONT_SECS);
  int16_t xs, ys; uint16_t ws, hs;
  tft.getTextBounds("88", 0, 0, &xs, &ys, &ws, &hs);
  s_seg.wSecs = (int)ws;

  const int extra = CLOCK_W_EXTRA_TOTAL;
  const int per   = extra / 3;
  const int rem   = extra % 3;
  const int gap1  = per + ((rem > 0) ? 1 : 0);
  const int gap2  = per + ((rem > 1) ? 1 : 0);
  const int gap3  = per;

  s_seg.offHH     = 0;
  s_seg.offColon1 = (r_88     - x_all) + gap1 + COLON1_ADJ_X;
  s_seg.offMM     = (r_88c    - x_all) + gap1 + gap2;
  s_seg.offSS     = (r_88c88  - x_all) + gap1 + gap2 + gap3;

  s_segInit = true;
}

// ---------- Info block (6 lines under the time row) ----------
struct ClockInfoLayout {
  int X;
  int Y0;
  int lineH;
};
static ClockInfoLayout s_info = {};
static bool s_infoInit = false;

static void UI_Info_InitLayout() {
  if (s_infoInit) return;

  // Time-row height in FONT_TIME to position info block
  tft.setFont(FONT_TIME);
  int16_t x1, y1; uint16_t bw, bh;
  tft.getTextBounds("88:88:88", 0, 0, &x1, &y1, &bw, &bh);
  const int time_h = (int)bh;

  // Left anchor is the same as the clock (Slice-B panel origin + padding)
  s_info.X = L_FreqX() + CLOCK_PAD_L;

  // First info line top
  s_info.Y0 = (L_FreqY() - 2 + CLOCK_PAD_T) + time_h + CLOCK_V_SP + 1;

  // Measure one line height in FONT_INFO
  tft.setFont(FONT_INFO);
  int16_t ix, iy; uint16_t iw, ih;
  tft.getTextBounds("Ag", 0, 0, &ix, &iy, &iw, &ih);
  s_info.lineH = (int)ih;

  s_infoInit = true;
}

static inline int UI_Info_LineTop(int lineIndex1to6) {
  UI_Info_InitLayout();
  return s_info.Y0 + (lineIndex1to6 - 1) * s_info.lineH;
}

// ---- Helpers ----
static inline void UI_Clock_ClearBox(int x, int y, int w, int h) {
  tft.fillRect(x, y, w, h, COLOR_BLACK);
}

static void UI_Clock_DrawStringAt(const char* s, int x, int y,
                                  const ILI9341_t3_font_t& font,
                                  uint16_t fg) {
  if (!s) return;
  tft.setTextWrap(false);
  tft.setFont(font);
  int16_t bx, by; uint16_t bw, bh;
  tft.getTextBounds(s, 0, 0, &bx, &by, &bw, &bh);
  tft.setTextColor(fg);         // transparent
  tft.setCursor(x - bx, y - by);
  tft.print(s);
}

// ------------------ Public UI API ------------------

// Draw HH + colon #1 + MM (minute-change path)
void UI_Clock_Draw_HH_MM_Colons(const char* hh, const char* mm) {
  if (!hh || !mm) return;
  UI_Clock_InitSegLayout();

#if CLOCK_DEBUG_LAYOUT
  tft.drawRect(s_seg.AX + s_seg.offHH,     s_seg.AY, s_seg.wPair,  s_seg.hLine, COLOR_RED);
  tft.drawRect(s_seg.AX + s_seg.offColon1, s_seg.AY, s_seg.wColon, s_seg.hLine, COLOR_RED);
  tft.drawRect(s_seg.AX + s_seg.offMM,     s_seg.AY, s_seg.wPair,  s_seg.hLine, COLOR_RED);
  {
    tft.setFont(FONT_TIME);
    int16_t cx, cy; uint16_t cw, ch;
    tft.getTextBounds(":", 0, 0, &cx, &cy, &cw, &ch);
    const int colonY = s_seg.AY + (s_seg.hLine - (int)ch) / 2;
    UI_Clock_DrawStringAt(":", s_seg.AX + s_seg.offColon1, colonY, FONT_TIME, COLOR_YELLOW);
  }
  return;
#endif

  // HH
  UI_Clock_ClearBox(s_seg.AX + s_seg.offHH - CLEAR_MARGIN_X,
                    s_seg.AY, s_seg.wPair + 2*CLEAR_MARGIN_X, s_seg.hLine);
  UI_Clock_DrawStringAt(hh, s_seg.AX + s_seg.offHH, s_seg.AY, FONT_TIME, COLOR_YELLOW);

  // Colon #1
  {
    tft.setFont(FONT_TIME);
    int16_t cx, cy; uint16_t cw, ch;
    tft.getTextBounds(":", 0, 0, &cx, &cy, &cw, &ch);
    UI_Clock_ClearBox(s_seg.AX + s_seg.offColon1 - CLEAR_MARGIN_X,
                      s_seg.AY, s_seg.wColon + 2*CLEAR_MARGIN_X, s_seg.hLine);
    const int colonY = s_seg.AY + (s_seg.hLine - (int)ch) / 2;
    UI_Clock_DrawStringAt(":", s_seg.AX + s_seg.offColon1, colonY, FONT_TIME, COLOR_YELLOW);
  }

  // MM
  UI_Clock_ClearBox(s_seg.AX + s_seg.offMM - CLEAR_MARGIN_X,
                    s_seg.AY, s_seg.wPair + 2*CLEAR_MARGIN_X, s_seg.hLine);
  UI_Clock_DrawStringAt(mm, s_seg.AX + s_seg.offMM, s_seg.AY, FONT_TIME, COLOR_YELLOW);
}

// Draw SS only (second tick)
void UI_Clock_Draw_SS(const char* ss) {
  if (!ss) return;
  UI_Clock_InitSegLayout();

#if CLOCK_DEBUG_LAYOUT
  {
    tft.setFont(FONT_SECS);
    int16_t dx, dy; uint16_t dw, dh;
    tft.getTextBounds("88", 0, 0, &dx, &dy, &dw, &dh);
    tft.drawRect(s_seg.AX + s_seg.offSS, s_seg.AY, (int)dw, s_seg.hLine, COLOR_RED);
  }
  return;
#endif

  tft.setFont(FONT_SECS);
  int16_t dx, dy; uint16_t dw, dh;
  tft.getTextBounds("88", 0, 0, &dx, &dy, &dw, &dh);
  const int wSecs = (int)dw;

  UI_Clock_ClearBox(s_seg.AX + s_seg.offSS - CLEAR_MARGIN_X,
                    s_seg.AY, wSecs + 2*CLEAR_MARGIN_X, s_seg.hLine);

  int16_t sx, sy; uint16_t sw, sh;
  tft.getTextBounds(ss, 0, 0, &sx, &sy, &sw, &sh);
  const int secsTop  = s_seg.AY;
  const int secsLeft = s_seg.AX + s_seg.offSS + (wSecs - (int)sw) / 2;

  tft.setTextColor(COLOR_YELLOW);
  tft.setCursor(secsLeft - sx, secsTop - sy);
  tft.print(ss);
}

void UI_Clock_ClearSecondsBox() {
  UI_Clock_InitSegLayout();
  tft.setFont(FONT_SECS);
  int16_t dx, dy; uint16_t dw, dh;
  tft.getTextBounds("88", 0, 0, &dx, &dy, &dw, &dh);
  const int wSecs = (int)dw;
  UI_Clock_ClearBox(s_seg.AX + s_seg.offSS - CLEAR_MARGIN_X,
                    s_seg.AY, wSecs + 2*CLEAR_MARGIN_X, s_seg.hLine);
}

void UI_Clock_Draw_AMPM(const char* ap) {
  if (!ap) return;
  UI_Clock_InitSegLayout();

  tft.setFont(FONT_SECS);
  int16_t dx, dy; uint16_t dw, dh;
  tft.getTextBounds("88", 0, 0, &dx, &dy, &dw, &dh);
  const int wSecs = (int)dw;

  UI_Clock_ClearBox(s_seg.AX + s_seg.offSS - CLEAR_MARGIN_X,
                    s_seg.AY, wSecs + 2*CLEAR_MARGIN_X, s_seg.hLine);

  int16_t sx, sy; uint16_t sw, sh;
  tft.getTextBounds(ap, 0, 0, &sx, &sy, &sw, &sh);
  const int top  = s_seg.AY;
  const int left = s_seg.AX + s_seg.offSS + (wSecs - (int)sw) / 2;

  tft.setTextColor(COLOR_YELLOW);
  tft.setCursor(left - sx, top - sy);
  tft.print(ap);
}

static void UI_Clock_TimeRect(int& x, int& y, int& w, int& h) {
  x = L_FreqX() + CLOCK_PAD_L;
  y = L_FreqY() - 2 + CLOCK_PAD_T;

  tft.setFont(FONT_TIME);
  int16_t xb, yb; uint16_t wb, hb;
  tft.getTextBounds("88:88:88", 0, 0, &xb, &yb, &wb, &hb);

  w = (int)wb + CLOCK_W_EXTRA_TOTAL;
  h = (int)hb;
}

void UI_Date_Draw(const char* text) {
  if (!text) return;
  UI_Info_InitLayout();
  const int y  = UI_Info_LineTop(2);
  const int AX = s_info.X;

  tft.setTextWrap(false);
  tft.setFont(FONT_INFO);

  // Clear the full line width to avoid leftover pixels when the new text is shorter
  const int clearW = L_FreqW() - CLOCK_PAD_L;
  tft.fillRect(AX, y, clearW, s_info.lineH, COLOR_BLACK);

  int16_t xd, yd; uint16_t wd, hd;
  tft.getTextBounds(text, 0, 0, &xd, &yd, &wd, &hd);

  tft.setTextColor(COLOR_YELLOW);
  tft.setCursor(AX - xd, y - yd);
  tft.print(text);
}

void UI_Clock_ClearTime() {
  int x, y, w, h;
  UI_Clock_TimeRect(x, y, w, h);
  UI_Clock_ClearBox(x, y, w, h);
}

void UI_Clock_ClearDate() {
  UI_Info_InitLayout();
  const int y = UI_Info_LineTop(2);
  // Use panel width from layout instead of a hardcoded 240px.
  const int clearW = L_FreqW() - CLOCK_PAD_L;
  tft.fillRect(s_info.X, y, clearW, s_info.lineH, COLOR_BLACK);
}

static void UI_ClockPanel_Rect(int& x, int& y, int& w, int& h,
                               int& time_h, int& info_h) {
  // Slice-B region is already injected via layout; do not offset by midScreen here.
  x = L_FreqX();
  y = L_FreqY() - 2;
  w = L_FreqW();

  tft.setFont(FONT_TIME);
  int16_t x1, y1; uint16_t bw, bh;
  tft.getTextBounds("88:88:88", 0, 0, &x1, &y1, &bw, &bh);
  time_h = (int)bh;

  UI_Info_InitLayout();
  info_h = 6 * s_info.lineH;

  h = CLOCK_PAD_T + time_h + CLOCK_V_SP + info_h + 2;
}

void UI_Clock_Clear() {
  int x, y, w, h, th, ih;
  UI_ClockPanel_Rect(x, y, w, h, th, ih);
  tft.fillRect(x, y, w, h, COLOR_BLACK);
  s_segInit  = false;
  s_infoInit = false;
}

// Return true if Slice B is free according to the UI-facing state.
// This keeps UI independent from the underlying radio model.
bool UI_SliceB_Free() {
  // In Keyer-Only mode, the right-hand panel is always considered free.
  if (UIState::isKeyerOnly()) {
    return true;
  }

  // Otherwise, only show the clock if we're connected AND Slice B is not in use.
  return UIState::isConnected() && UIState::isSliceFree(B);
}

// Fallback whole-string draw (e.g., "--:--:--") when time is unknown
void UI_Clock_Draw(const char* text) {
  if (!text) return;
  UI_Clock_InitSegLayout();

#if CLOCK_DEBUG_LAYOUT
  tft.drawRect(s_seg.AX + s_seg.offHH,     s_seg.AY, s_seg.wPair,  s_seg.hLine, COLOR_RED);
  tft.drawRect(s_seg.AX + s_seg.offColon1, s_seg.AY, s_seg.wColon, s_seg.hLine, COLOR_RED);
  tft.drawRect(s_seg.AX + s_seg.offMM,     s_seg.AY, s_seg.wPair,  s_seg.hLine, COLOR_RED);
  {
    tft.setFont(FONT_TIME);
    int16_t cx, cy; uint16_t cw, ch;
    tft.getTextBounds(":", 0, 0, &cx, &cy, &cw, &ch);
    const int colonY = s_seg.AY + (s_seg.hLine - (int)ch) / 2;
    UI_Clock_DrawStringAt(":", s_seg.AX + s_seg.offColon1, colonY, FONT_TIME, COLOR_YELLOW);
  }
  return;
#endif

  tft.setTextColor(COLOR_YELLOW);
  tft.setFont(FONT_TIME);
  tft.setCursor(s_seg.baseX, s_seg.baseY);
  tft.print(text);
}

// ---- Config status line (stored text; drawn on line #6) ----
static char s_cfgStatus[64] = "";

void UI_Info_SetConfigStatus(const char* text) {
  if (!text) { s_cfgStatus[0] = '\0'; return; }
  strncpy(s_cfgStatus, text, sizeof(s_cfgStatus));
  s_cfgStatus[sizeof(s_cfgStatus)-1] = '\0';
}

const char* UI_Info_GetConfigStatusText() {
  return s_cfgStatus;
}

// ---------- Colored label/value helpers ----------
static void UI_Info_DrawLabelValue(int lineIndex,
                                   const char* label,
                                   const char* value,
                                   uint16_t valueColor) {
  if (!label) label = "";
  if (!value) value = "";

  UI_Info_InitLayout();
  const int y = UI_Info_LineTop(lineIndex);

  tft.setTextWrap(false);
  tft.setFont(FONT_INFO);

  int16_t lx, ly; uint16_t lw, lh;
  int16_t vx, vy; uint16_t vw, vh;
  tft.getTextBounds(label, 0, 0, &lx, &ly, &lw, &lh);
  tft.getTextBounds(value, 0, 0, &vx, &vy, &vw, &vh);

  const int clearW = (int)lw + (int)vw + 6;
  tft.fillRect(s_info.X, y, clearW, s_info.lineH, COLOR_BLACK);

  tft.setTextColor(COLOR_CYAN);
  tft.setCursor(s_info.X - lx, y - ly);
  tft.print(label);

  const int valueLeft = s_info.X + (int)lw + 2;
  tft.setTextColor(valueColor);
  tft.setCursor(valueLeft - vx, y - vy);
  tft.print(value);
}

// Line 1: timezone name (yellow; static)
void UI_Info_DrawTimezone() {
  UI_Info_InitLayout();
  const int y  = UI_Info_LineTop(1);

  const char* tz = nullptr;
  static char tz_fallback[16];
  if (CFG_TimeZone.length() > 0) {
    tz = CFG_TimeZone.c_str();
  } else {
    strncpy(tz_fallback, "UTC", sizeof(tz_fallback));
    tz_fallback[sizeof(tz_fallback)-1] = '\0';
    tz = tz_fallback;
  }

  tft.setTextWrap(false);
  tft.setFont(FONT_INFO);

  int16_t bx, by; uint16_t bw, bh;
  tft.getTextBounds(tz, 0, 0, &bx, &by, &bw, &bh);

  const int clearW = L_FreqW() - CLOCK_PAD_L;
  tft.fillRect(s_info.X, y, clearW, s_info.lineH, COLOR_BLACK);

  tft.setTextColor(COLOR_YELLOW);
  tft.setCursor(s_info.X - bx, y - by);
  tft.print(tz);
}

// Line 4: temperature with colored value.
void UI_Info_DrawLine4_Temp(const char* text) {
  if (!text) return;

  const char* colon = strchr(text, ':');
  const char* label = "Temp: ";
  const char* value = (colon && *(colon+1)) ? colon+1 : text;

  while (*value == ' ') ++value;

  float temp = 0.0f;
  bool  isF  = false;
  {
    char buf[24];
    size_t n = 0;
    while (value[n] && n < sizeof(buf)-1 && value[n] != '\n' && value[n] != '\r') { buf[n] = value[n]; ++n; }
    buf[n] = '\0';

    const size_t len = strlen(buf);
    if (len && (buf[len-1] == 'F' || buf[len-1] == 'f')) isF = true;
    temp = strtof(buf, nullptr);
  }

  uint16_t col = COLOR_GREEN;
  if (!isF) {
    if (temp >= 75.0f)      col = COLOR_RED;
    else if (temp >= 65.0f) col = COLOR_YELLOW;
    else if (temp >= 50.0f) col = COLOR_YELLOW;
  } else {
    if (temp >= 167.0f)      col = COLOR_RED;
    else if (temp >= 149.0f) col = COLOR_YELLOW;
    else if (temp >= 122.0f) col = COLOR_YELLOW;
  }

  UI_Info_DrawLabelValue(4, label, value, col);
}

// Line 5: RAM1 free (kB) with only the number colored.
void UI_Info_DrawLine5_RAM1(const char* label, unsigned freeKB, const char* suffix) {
  if (!label)  label  = "RAM1";
  if (!suffix) suffix = " kB free";

  uint16_t valueColor = COLOR_GREEN;
  if      (freeKB < 48) valueColor = COLOR_RED;
  else if (freeKB < 80) valueColor = COLOR_YELLOW;

  UI_Info_InitLayout();
  const int y = UI_Info_LineTop(5);

  tft.setTextWrap(false);
  tft.setFont(FONT_INFO);

  char num[16];
  snprintf(num, sizeof(num), "%u", freeKB);

  int16_t lx, ly; uint16_t lw, lh;
  int16_t nx, ny; uint16_t nw, nh;
  int16_t sx, sy; uint16_t sw, sh;

  char labelBuf[16];
  snprintf(labelBuf, sizeof(labelBuf), "%s: ", label);

  tft.getTextBounds(labelBuf, 0, 0, &lx, &ly, &lw, &lh);
  tft.getTextBounds(num,      0, 0, &nx, &ny, &nw, &nh);
  tft.getTextBounds(suffix,   0, 0, &sx, &sy, &sw, &sh);

  const int gap = 2;
  const int clearW = (int)lw + gap + (int)nw + gap + (int)sw + 2;
  tft.fillRect(s_info.X, y, clearW, s_info.lineH, COLOR_BLACK);

  tft.setTextColor(COLOR_CYAN);
  tft.setCursor(s_info.X - lx, y - ly);
  tft.print(labelBuf);

  const int xNum = s_info.X + (int)lw + gap;
  tft.setTextColor(valueColor);
  tft.setCursor(xNum - nx, y - ny);
  tft.print(num);

  const int xSuf = xNum + (int)nw + gap;
  tft.setTextColor(COLOR_CYAN);
  tft.setCursor(xSuf - sx, y - sy);
  tft.print(suffix);
}

// Line 6: Config status colored (OK=green, ERR=red, else cyan)
void UI_Info_DrawLine6_ConfigColored(const char* text) {
  if (!text) text = "";
  const char* colon = strchr(text, ':');
  const char* label = "Config: ";
  const char* value = (colon && *(colon+1)) ? colon+1 : text;
  while (*value == ' ') ++value;

  uint16_t col = COLOR_CYAN;
  if      (strncmp(value, "OK", 2)  == 0) col = COLOR_GREEN;
  else if (strncmp(value, "ERR", 3) == 0) col = COLOR_RED;

  UI_Info_DrawLabelValue(6, label, value, col);
}

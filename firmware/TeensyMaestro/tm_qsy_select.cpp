// ============================================================================
// tm_qsy_select.cpp
// Modal Band→Mode QSY selector for TeensyMaestro
// ============================================================================

#include "tm_qsy_select.h"
#include "tm_system_utils.h"

extern bool MenuActive;
extern int  SliceActiveVal;
// --- Profile map sources defined elsewhere (MMConfig.ini overrides allowed) ---
extern String CFG_Profile_Map_CW_160M; extern String CFG_Profile_Map_CW_80M;  extern String CFG_Profile_Map_CW_60M;
extern String CFG_Profile_Map_CW_40M;  extern String CFG_Profile_Map_CW_30M;  extern String CFG_Profile_Map_CW_20M;
extern String CFG_Profile_Map_CW_17M;  extern String CFG_Profile_Map_CW_15M;  extern String CFG_Profile_Map_CW_12M;
extern String CFG_Profile_Map_CW_10M;  extern String CFG_Profile_Map_CW_6M;

extern String CFG_Profile_Map_SSB_160M; extern String CFG_Profile_Map_SSB_80M;  extern String CFG_Profile_Map_SSB_60M;
extern String CFG_Profile_Map_SSB_40M;  extern String CFG_Profile_Map_SSB_30M;  extern String CFG_Profile_Map_SSB_20M;
extern String CFG_Profile_Map_SSB_17M;  extern String CFG_Profile_Map_SSB_15M;  extern String CFG_Profile_Map_SSB_12M;
extern String CFG_Profile_Map_SSB_10M;  extern String CFG_Profile_Map_SSB_6M;

extern String CFG_Profile_Map_FM_160M; extern String CFG_Profile_Map_FM_80M;  extern String CFG_Profile_Map_FM_60M;
extern String CFG_Profile_Map_FM_40M;  extern String CFG_Profile_Map_FM_30M;  extern String CFG_Profile_Map_FM_20M;
extern String CFG_Profile_Map_FM_17M;  extern String CFG_Profile_Map_FM_15M;  extern String CFG_Profile_Map_FM_12M;
extern String CFG_Profile_Map_FM_10M;  extern String CFG_Profile_Map_FM_6M;

extern String CFG_Profile_Map_DIGU_160M; extern String CFG_Profile_Map_DIGU_80M;  extern String CFG_Profile_Map_DIGU_60M;
extern String CFG_Profile_Map_DIGU_40M;  extern String CFG_Profile_Map_DIGU_30M;  extern String CFG_Profile_Map_DIGU_20M;
extern String CFG_Profile_Map_DIGU_17M;  extern String CFG_Profile_Map_DIGU_15M;  extern String CFG_Profile_Map_DIGU_12M;
extern String CFG_Profile_Map_DIGU_10M;  extern String CFG_Profile_Map_DIGU_6M;

extern void MenuExit();
extern void ResetTFTScreen();

// --- Lookup tables for profile maps ---
static String* CW_MAP[] = {
  &CFG_Profile_Map_CW_160M, &CFG_Profile_Map_CW_80M, &CFG_Profile_Map_CW_60M, &CFG_Profile_Map_CW_40M, &CFG_Profile_Map_CW_30M,
  &CFG_Profile_Map_CW_20M,  &CFG_Profile_Map_CW_17M, &CFG_Profile_Map_CW_15M, &CFG_Profile_Map_CW_12M, &CFG_Profile_Map_CW_10M, &CFG_Profile_Map_CW_6M
};
static String* SSB_MAP[] = {
  &CFG_Profile_Map_SSB_160M, &CFG_Profile_Map_SSB_80M, &CFG_Profile_Map_SSB_60M, &CFG_Profile_Map_SSB_40M, &CFG_Profile_Map_SSB_30M,
  &CFG_Profile_Map_SSB_20M,  &CFG_Profile_Map_SSB_17M, &CFG_Profile_Map_SSB_15M, &CFG_Profile_Map_SSB_12M, &CFG_Profile_Map_SSB_10M, &CFG_Profile_Map_SSB_6M
};
static String* FM_MAP[] = {
  &CFG_Profile_Map_FM_160M, &CFG_Profile_Map_FM_80M, &CFG_Profile_Map_FM_60M, &CFG_Profile_Map_FM_40M, &CFG_Profile_Map_FM_30M,
  &CFG_Profile_Map_FM_20M,  &CFG_Profile_Map_FM_17M, &CFG_Profile_Map_FM_15M, &CFG_Profile_Map_FM_12M, &CFG_Profile_Map_FM_10M, &CFG_Profile_Map_FM_6M
};
static String* DIGU_MAP[] = {
  &CFG_Profile_Map_DIGU_160M, &CFG_Profile_Map_DIGU_80M, &CFG_Profile_Map_DIGU_60M, &CFG_Profile_Map_DIGU_40M, &CFG_Profile_Map_DIGU_30M,
  &CFG_Profile_Map_DIGU_20M,  &CFG_Profile_Map_DIGU_17M, &CFG_Profile_Map_DIGU_15M, &CFG_Profile_Map_DIGU_12M, &CFG_Profile_Map_DIGU_10M, &CFG_Profile_Map_DIGU_6M
};

// Allowed meter keys and their order in the arrays above
static const int M_KEYS[] = {160, 80, 60, 40, 30, 20, 17, 15, 12, 10, 6};

static int idxForMeters(int m) {
  for (int i = 0; i < (int)(sizeof(M_KEYS)/sizeof(M_KEYS[0])) ; ++i)
    if (M_KEYS[i] == m) return i;
  return 5; // default to 20m if unknown
}

static bool eqMode(const char* a, const char* b) {
  while (*a && *b) {
    char ca = (*a >= 'a' && *a <= 'z') ? (*a - 32) : *a;
    char cb = (*b >= 'a' && *b <= 'z') ? (*b - 32) : *b;
    if (ca != cb) return false;
    ++a; ++b;
  }
  return (*a == 0 && *b == 0);
}

static String* const* mapForMode(const char* mode) {
  if (eqMode(mode, "CW"))   return CW_MAP;
  if (eqMode(mode, "SSB"))  return SSB_MAP;
  if (eqMode(mode, "FM"))   return FM_MAP;
  if (eqMode(mode, "DIGU")) return DIGU_MAP;
  return CW_MAP; // safe default
}

// --------------------------- Internal state ----------------------------------
namespace {

enum class Page : uint8_t { Hidden = 0, Bands, Modes };

struct Rect {
  int16_t x, y, w, h;
  bool contains(int16_t px, int16_t py) const {
    return px >= x && py >= y && px < (x + w) && py < (y + h);
  }
};

struct BandTile {
  const char *label;   // "160", "80", "20", "6"
  uint8_t meters;      // numeric for rules (e.g., 20 for 20m)
  Rect r;
  bool isCancel;
};

struct ModeTile {
  const char *label;   // "SSB","CW","FM","DIGU"
  Rect r;
  bool enabled;
  bool isCancel;
};

static Page s_page = Page::Hidden;
static uint32_t s_lastActivityMs = 0;
static bool     s_dirty          = false;
static char     s_selectedBand[8] = {0};   // e.g. "20"

// Slice mode currently marked by the highlight rectangle, in BANDMODE.
// Set once when the mode page opens (from whatever mode the slice was
// in before the band change), then kept in sync live by
// QsySel::onRadioModeChanged() as the radio reports new modes, without
// a full page redraw: the radio's band stack recall emits several mode
// events right after a band change, and redrawing renderModes() on each
// one (fillScreen plus six tiles) corrupts the display.
static char s_highlightedMode[8];
static BandTile s_bands[12];
static ModeTile s_modes[6];

// Deferred close after mode tap, and a small arm delay to avoid band→mode bounce
static uint32_t s_deferredCloseAtMs = 0;
static uint32_t s_touchArmUntilMs   = 0;   // while now < this, ignore touches in Modes page

// BANDMODE only: time of the last fRig.setBand() call, and a mode chosen
// while still inside the post band change settle window (empty = none).
static uint32_t s_bandAppliedAtMs = 0;
static char     s_pendingMode[8]  = {0};

// A band change makes the radio recall its band stack, which can push
// slice status updates including mode. Applying a mode too soon after
// the band could race with that burst. The RCS QSY path uses the same
// 750 ms quiet window after a profile load (QSY_SETTLE_MS in
// tm_rcs_server.cpp). Not yet verified against a live radio.
static constexpr uint32_t QSYSEL_BAND_SETTLE_MS = 750;

// Basic palette fallbacks (RGB565)
#ifndef COLOR_BLACK
#define COLOR_BLACK 0x0000
#endif
#ifndef COLOR_WHITE
#define COLOR_WHITE 0xFFFF
#endif
#ifndef COLOR_RED
#define COLOR_RED   0xF800
#endif
#ifndef COLOR_GRAY
#define COLOR_GRAY  0x8410
#endif
#ifndef COLOR_BLUE
#define COLOR_BLUE  0x001F
#endif
#ifndef COLOR_GREEN
#define COLOR_GREEN 0x07E0
#endif
#ifndef COLOR_CYAN
#define COLOR_CYAN  0x07FF
#endif
#ifndef COLOR_MAGENTA
#define COLOR_MAGENTA 0xF81F
#endif
#ifndef COLOR_YELLOW
#define COLOR_YELLOW 0xFFE0
#endif

static uint16_t COL_BG       = COLOR_BLACK;
static uint16_t COL_TEXT     = COLOR_WHITE;
static uint16_t COL_BTN      = COLOR_BLUE;
static uint16_t COL_BTN_OUT  = COLOR_WHITE;
static uint16_t COL_BTN_TXT  = COLOR_WHITE;
static uint16_t COL_BTN_DIS  = COLOR_GRAY;
static uint16_t COL_CANCEL   = COLOR_RED;

// Highlight for the tile matching the slice's current mode.
// Must contrast with COL_BTN and with the white tile outline.
// Kept as COLOR_YELLOW rather than a different constant: this file
// already uses COLOR_YELLOW as its one "this tile matters" color, for
// the band and mode tap-confirmation flashes elsewhere in onTouch().
// The visibility problem was the 1px width at a 2px inset next to a
// 1px white outline at inset 0, not the hue; widening it to a 3px band
// (see drawModeHighlight below) keeps the existing color language
// consistent instead of adding a second accent color.
static const uint16_t COL_MODE_HL = COLOR_YELLOW;

// ----------------------- small helpers ---------------------------------------
FLASHMEM static void beep_click() {
  if (BtnClickTone > 0 && BtnClickDur > 0) {
    tone(STPin, BtnClickTone, BtnClickDur);
  }
}

FLASHMEM static void setDefaultFont() {
  // Use large T3 font from Display_Fonts.h for consistent big labels
  tft.setFont(FONT_TITLE);   // e.g. Arial_28_Bold
  tft.setTextSize(1);        // must be 1 when using setFont(...)
  tft.setTextColor(COL_TEXT);
}

// Fill color a mode tile is drawn with, matching the selection logic in
// renderModes(). Factored out so the highlight erase step in
// onRadioModeChanged() draws back the exact color renderModes() used,
// including the disabled and Cancel cases.
static uint16_t modeTileFillColor(const ModeTile &m) {
  if (m.isCancel) return COL_CANCEL;
  if (!m.enabled) return COL_BTN_DIS;
  return COL_BTN;
}

// Draw the current mode marker: a 5px band just inside the tile's
// own outline.
static void drawModeHighlight(const Rect &r, uint16_t colour) {
  for (int i = 2; i <= 6; ++i) {
    tft.drawRect(r.x + i, r.y + i, r.w - 2 * i, r.h - 2 * i, colour);
  }
}

FLASHMEM static void drawButton(const Rect &r, const char *label,
                                uint16_t fill, uint16_t outline, uint16_t txt) {
  tft.fillRect(r.x, r.y, r.w, r.h, fill);
  tft.drawRect(r.x, r.y, r.w, r.h, outline);

  // Ensure correct font is active for bounds calculation
  tft.setFont(FONT_TITLE);
  tft.setTextSize(1);
  tft.setTextColor(txt);

  int16_t x1, y1; uint16_t w, h;
  // Get pixel bounds for the label with current font
  tft.getTextBounds(label, 0, 0, &x1, &y1, &w, &h);

  // Center inside the button rect
  int16_t cx = r.x + (r.w - (int16_t)w) / 2;
  int16_t cy = r.y + (r.h - (int16_t)h) / 2;

  // Baseline position (ILI9341_t3/ST7796_t3 draws from baseline)
  // getTextBounds returns y1 (which is typically negative). We set cursor at (cx, cy - y1)
  tft.setCursor(cx, cy - y1);
  tft.print(label);
}

// -------------------------- layout -------------------------------------------
FLASHMEM static void layoutBands() {
  const int16_t W = tft.width();
  const int16_t H = tft.height();
  const int cols = 3, rows = 4;
  const int gap = 6;
  const int totalGapsX = (cols + 1) * gap;
  const int totalGapsY = (rows + 1) * gap;
  const int cellW = (W - totalGapsX) / cols;
  const int cellH = (H - totalGapsY) / rows;

  const uint8_t listMeters[11] = {160,80,60,40,30,20,17,15,12,10,6};

  for (int i = 0; i < 12; ++i) {
    const int row = i / cols;
    const int col = i % cols;
    Rect r { int16_t(gap + col * (cellW + gap)),
             int16_t(gap + row * (cellH + gap)),
             int16_t(cellW), int16_t(cellH) };
    s_bands[i].r = r;
    s_bands[i].isCancel = (i == 11);

    if (i < 11) {
      static char labels[11][6];
      uint8_t m = listMeters[i];
      snprintf(labels[i], sizeof(labels[i]), "%u", (unsigned)m);
      s_bands[i].label  = labels[i];
      s_bands[i].meters = m;
    } else {
      s_bands[i].label  = "Cancel";
      s_bands[i].meters = 0;
    }
  }
}

FLASHMEM static void layoutModes() {
  const int16_t W = tft.width();
  const int16_t H = tft.height();
  const int cols = 2, rows = 3;     // 2x3
  const int gap = 8;
  const int totalGapsX = (cols + 1) * gap;
  const int totalGapsY = (rows + 1) * gap;
  const int cellW = (W - totalGapsX) / cols;
  const int cellH = (H - totalGapsY) / rows;

  static const char* const kLabelsProfile[6]  = { "SSB", "CW", "FM", "DIGU", "", "Cancel" };
  static const char* const kLabelsBandmode[6] = { "CW", "USB", "LSB", "FM", "DIGU", "Cancel" };
  const char* const* labels =
      (CFG_QsySel_Action == QSYSEL_ACTION_BANDMODE) ? kLabelsBandmode : kLabelsProfile;

  for (int i = 0; i < 6; ++i) {
    const int row = i / cols;
    const int col = i % cols;
    Rect r { int16_t(gap + col * (cellW + gap)),
             int16_t(gap + row * (cellH + gap)),
             int16_t(cellW), int16_t(cellH) };
    s_modes[i].r = r;
    s_modes[i].label = labels[i];
    s_modes[i].isCancel = (i == 5);
    s_modes[i].enabled = (labels[i][0] != '\0');
  }
}

FLASHMEM static void computeModeEnables() {
  uint8_t meters = (uint8_t)atoi(s_selectedBand);

  if (CFG_QsySel_Action == QSYSEL_ACTION_BANDMODE) {
    for (int i = 0; i < 6; ++i) {
      if (s_modes[i].isCancel) { s_modes[i].enabled = true; continue; }

      const char* lab = s_modes[i].label;
      bool en = true;

      // FM is disabled on the lower bands because wide modes there are
      // discouraged by band plan recommendations. This is guidance, not a
      // legal restriction, so the rule is a convenience default rather
      // than an enforcement.
      if (strcmp(lab, "FM") == 0) {
        en = (meters <= 10);
      } else if (strcmp(lab, "USB") == 0) {
        // 30 m is a CW and digital only band, no phone.
        en = (meters != 30);
      } else if (strcmp(lab, "LSB") == 0) {
        // 30 m is a CW and digital only band, no phone. On 60 m only USB is
        // used, matching the 60 m exception already present in
        // FlexRig::toggleCW_SSB().
        en = (meters != 30 && meters != 60);
      }
      // CW and DIGU match neither rule above, so they stay enabled.
      s_modes[i].enabled = en;
    }
    return;
  }

  // FM disabled below 10m → only 10m and 6m enabled
  // SSB disabled on 30m
  for (int i = 0; i < 6; ++i) {
    if (s_modes[i].label[0] == '\0') { s_modes[i].enabled = false; continue; }
    if (s_modes[i].isCancel) { s_modes[i].enabled = true; continue; }

    const char* lab = s_modes[i].label;
    bool en = true;

    // FM is disabled on the lower bands because wide modes there are
    // discouraged by band plan recommendations. This is guidance, not a
    // legal restriction, so the rule is a convenience default rather
    // than an enforcement.
    if (strcmp(lab, "FM") == 0) {
      en = (meters <= 10);
    } else if (strcmp(lab, "SSB") == 0) {
      en = (meters != 30);
    }
    s_modes[i].enabled = en;
  }
}

// -------------------------- rendering -----------------------------------------
FLASHMEM static void renderBands() {
  tft.fillScreen(COL_BG);    // full black
  setDefaultFont();
  // no banner
  for (int i = 0; i < 12; ++i) {
    const bool isCancel = s_bands[i].isCancel;
    uint16_t fill = isCancel ? COL_CANCEL : COL_BTN;
    uint16_t out  = COL_BTN_OUT;
    uint16_t txt  = COL_BTN_TXT;
    drawButton(s_bands[i].r, s_bands[i].label, fill, out, txt);
  }
}

FLASHMEM static void renderModes() {
  tft.fillScreen(COL_BG);    // full black
  setDefaultFont();

  // In BANDMODE, highlight the tile that matches s_highlightedMode.
  // QsySel::onRadioModeChanged() keeps that in sync as the radio
  // reports new modes; renderModes() just draws whatever is current.
  const bool showActiveMode = (s_highlightedMode[0] != '\0');

  // Uniform blue buttons for all modes; red for Cancel; gray for disabled
  for (int i = 0; i < 6; ++i) {
    const ModeTile& m = s_modes[i];
    const uint16_t fill = modeTileFillColor(m);
    const uint16_t out  = COL_BTN_OUT;
    const uint16_t txt  = (!m.enabled && !m.isCancel) ? COLOR_BLACK : COL_BTN_TXT;

    drawButton(m.r, m.label, fill, out, txt);

    if (showActiveMode && !m.isCancel && strcmp(s_highlightedMode, m.label) == 0) {
      drawModeHighlight(m.r, COL_MODE_HL);
    }
  }
}

// -------------------------- state & guards ------------------------------------
FLASHMEM static void switchPage(Page p) { s_page = p; s_dirty = true; }
FLASHMEM static bool canOpen() {
  if (!fRig.connected) return false;
  if (FlexIsHeadless()) return false;
  if (fRig.interlock.isInTransmit) return false;
  return true;
}
static void resetTimeout() { s_lastActivityMs = millis(); }

static void applyProfileAndClose(const char* bandMeters, const char* mode) {
  // Parse meters, choose map, then pick the correct String by index
  const int m = atoi(bandMeters);
  const int idx = idxForMeters(m);
  String* const* arr = mapForMode(mode);
  const String& profName = *(arr[idx]);

  // Load the explicit profile name from configuration mappings
  fRig.loadGlobalProfile(profName);

  // Properly close the selector (toggles MenuActive=false and redraws UI)
  QsySel::close();
}

static void applyBandModeAndArmClose(const char* mode) {
  const uint32_t now = millis();

  if ((now - s_bandAppliedAtMs) >= QSYSEL_BAND_SETTLE_MS) {
    if ((SliceActiveVal >= 0) && (SliceActiveVal < fRig.nMaxSlice)) {
      fRig.setMode(SliceActiveVal, mode);
    }
    s_deferredCloseAtMs = now + (uint32_t)CFG_Profile_Selector_Close_Delay_Ms;
  } else {
    // Still inside the post band change settle window. Defer the mode
    // change to tick() so it cannot race the radio's band stack recall.
    strncpy(s_pendingMode, mode, sizeof(s_pendingMode) - 1);
    s_pendingMode[sizeof(s_pendingMode) - 1] = '\0';
    // s_deferredCloseAtMs is armed later, by tick(), once the pending
    // mode has actually been sent.
  }
}

static void openBandsPage() {
  s_selectedBand[0] = '\0';
  s_deferredCloseAtMs = 0;
  s_bandAppliedAtMs = 0;
  s_pendingMode[0]  = '\0';
  s_highlightedMode[0] = '\0';
  layoutBands();
  switchPage(Page::Bands);
  resetTimeout();
  s_dirty = true;
  beep_click();
}

static void openModesPage() {
  s_highlightedMode[0] = '\0';
  if (CFG_QsySel_Action == QSYSEL_ACTION_BANDMODE &&
      SliceActiveVal >= 0 && SliceActiveVal < fRig.nMaxSlice) {
    const String m = fRig.slice[SliceActiveVal].mode;
    strncpy(s_highlightedMode, m.c_str(),
            sizeof(s_highlightedMode) - 1);
    s_highlightedMode[sizeof(s_highlightedMode) - 1] = '\0';
  }

  layoutModes();
  computeModeEnables();
  switchPage(Page::Modes);
  resetTimeout();

  // Arm modes page: ignore touches for a short time to avoid band→mode bounce
  s_touchArmUntilMs = millis() + 200;  // ~200 ms guard

  s_dirty = true;
  beep_click();
}


} // namespace

// ------------------------------- Public API -----------------------------------
namespace QsySel {

FLASHMEM String profileNameForBandMode(int meters, const char* mode) {
  if (mode == nullptr || mode[0] == '\0') {
    return String("");
  }

  // Strict band lookup. idxForMeters() falls back to 20m for unknown input,
  // which is fine for the touchscreen but wrong for a programmatic caller.
  int idx = -1;
  for (int i = 0; i < (int)(sizeof(M_KEYS) / sizeof(M_KEYS[0])); ++i) {
    if (M_KEYS[i] == meters) {
      idx = i;
      break;
    }
  }
  if (idx < 0) {
    return String("");
  }

  // Strict mode lookup. mapForMode() falls back to CW_MAP for unknown input,
  // which would silently select a CW profile for a non CW request.
  String* const* arr = nullptr;
  if (eqMode(mode, "CW")) {
    arr = CW_MAP;
  } else if (eqMode(mode, "SSB")) {
    arr = SSB_MAP;
  } else if (eqMode(mode, "FM")) {
    arr = FM_MAP;
  } else if (eqMode(mode, "DIGU")) {
    arr = DIGU_MAP;
  }
  if (arr == nullptr) {
    return String("");
  }

  return *(arr[idx]);
}

FLASHMEM void open() {
  if (!canOpen()) return;
  MenuActive = true;
#if DEBUG_TOUCH == 1
  Serial.println(F("[QsySel] open → MenuActive=1"));
#endif
  openBandsPage();
}

FLASHMEM void close() {
  switchPage(Page::Hidden);
  MenuActive = false;
#if DEBUG_TOUCH == 1
  Serial.println(F("[QsySel] close → MenuActive=0"));
#endif
  // Force a clean redraw of the base UI to avoid remnants/overdraw
  //ResetTFTScreen();
  MenuExit();
}

FLASHMEM bool isVisible() {
  return s_page != Page::Hidden;
}

void tick() {
  if (s_page == Page::Hidden) return;

  const uint32_t now = millis();

  // BANDMODE: fire a mode change that was deferred because it landed
  // inside the post band change settle window. Runs before the deferred
  // close and idle timeout checks so a pending setMode is never dropped.
  if (s_pendingMode[0] != '\0' &&
      (now - s_bandAppliedAtMs) >= QSYSEL_BAND_SETTLE_MS) {
    if ((SliceActiveVal >= 0) && (SliceActiveVal < fRig.nMaxSlice)) {
      fRig.setMode(SliceActiveVal, s_pendingMode);
    }
    s_pendingMode[0] = '\0';
    s_deferredCloseAtMs = now + (uint32_t)CFG_Profile_Selector_Close_Delay_Ms;
  }

  // Perform deferred close, then exit
  if (s_deferredCloseAtMs && now >= s_deferredCloseAtMs) {
    s_deferredCloseAtMs = 0;
    close();            // <-- actually close; do NOT just clear the timestamp
    return;
  }

  // Idle timeout is suppressed while a BANDMODE mode change is still
  // pending, so the selector cannot close before it is sent.
  if (s_pendingMode[0] == '\0' &&
      (now - s_lastActivityMs) >= (uint32_t)CFG_Profile_Selector_Timeout_Ms) {
    close();
    return;
  }

  // Redraw if needed
  if (s_dirty) {
    if (s_page == Page::Bands) renderBands();
    else if (s_page == Page::Modes) renderModes();
    s_dirty = false;
  }
}

// Request a redraw on the next tick(). Safe to call from event callbacks:
// it never touches the display itself.
void markDirty() {
  if (s_page != Page::Hidden) s_dirty = true;
}

// Update the BANDMODE mode highlight in place. Called from
// onSlice_mode() when the radio reports a new mode. Redraws only
// the two affected highlight rectangles, never the whole page,
// because a full redraw from an event callback corrupts the
// display.
void onRadioModeChanged() {
  if (s_page != Page::Modes) return;
  if (CFG_QsySel_Action != QSYSEL_ACTION_BANDMODE) return;
  if (SliceActiveVal < 0 || SliceActiveVal >= fRig.nMaxSlice) return;

  char newMode[8];
  const String m = fRig.slice[SliceActiveVal].mode;
  strncpy(newMode, m.c_str(), sizeof(newMode) - 1);
  newMode[sizeof(newMode) - 1] = '\0';

  if (strcmp(newMode, s_highlightedMode) == 0) return;

  // Erase the highlight from whichever tile currently carries it, using
  // the same fill color renderModes() would draw for that tile.
  for (int i = 0; i < 6; ++i) {
    const ModeTile &tile = s_modes[i];
    if (!tile.isCancel && strcmp(s_highlightedMode, tile.label) == 0) {
      drawModeHighlight(tile.r, modeTileFillColor(tile));
      break;
    }
  }

  strncpy(s_highlightedMode, newMode, sizeof(s_highlightedMode) - 1);
  s_highlightedMode[sizeof(s_highlightedMode) - 1] = '\0';

  // Draw the highlight on the tile matching the new mode, if any.
  for (int i = 0; i < 6; ++i) {
    const ModeTile &tile = s_modes[i];
    if (!tile.isCancel && strcmp(s_highlightedMode, tile.label) == 0) {
      drawModeHighlight(tile.r, COL_MODE_HL);
      break;
    }
  }
}

FLASHMEM void onTouch(int16_t x, int16_t y, bool isRelease)
{
  // Ignore if selector is not visible
  if (s_page == Page::Hidden) return;

  // We only act on press events; release is handled by caller
  if (isRelease) return;

  resetTimeout();

  // --- Bands page: 12 tiles + Cancel ---
  if (s_page == Page::Bands) {
    for (int i = 0; i < 12; ++i) {
      const BandTile &b = s_bands[i];
      if (!b.r.contains(x, y)) continue;

      if (b.isCancel) {
        beep_click();
        close();            // MenuActive = false inside close()
        return;
      }

      // Visual confirmation of the band hit
      tft.drawRect(b.r.x + 2, b.r.y + 2, b.r.w - 4, b.r.h - 4, COLOR_YELLOW);
      delay(60);

      // Store selected band and move to Modes page
      snprintf(s_selectedBand, sizeof(s_selectedBand), "%u", (unsigned)b.meters);

      if (CFG_QsySel_Action == QSYSEL_ACTION_BANDMODE) {
        // Same sequence as Process_Buttons.ino case BtnMenuSetBand:, but
        // using a local instead of the shared ActivePan global, which this
        // code has no reason to modify.
        if ((SliceActiveVal >= 0) && (SliceActiveVal < fRig.nMaxSlice)) {
          const uint32_t panHandle = (uint32_t)fRig.slice[SliceActiveVal].pan;
          if (panHandle > 0) {
            const int idx = TMU_HandleToPanIndexSafe(
                panHandle, TMU_ArrayLen(fRig.panadapter));
            if (idx >= 0) fRig.setBand(idx, b.meters);
          }
        }
        s_bandAppliedAtMs = millis();
      }

      openModesPage();      // sets s_page to Modes and renders, also arms s_touchArmUntilMs
      return;
    }
    // Tap outside any tile: ignore
    return;
  }

  // --- Modes page: up to 6 tiles + Cancel ---
  if (s_page == Page::Modes) {
    // Guard: ignore touches until arm delay has passed to avoid accidental mode selection
    if (s_touchArmUntilMs) {
      int32_t delta = (int32_t)(millis() - s_touchArmUntilMs);
      if (delta < 0) {
        // Still inside arm window, ignore this touch
        return;
      }
      // Arm window is over, clear it
      s_touchArmUntilMs = 0;
    }

    for (int i = 0; i < 6; ++i) {
      const ModeTile &m = s_modes[i];
      if (!m.r.contains(x, y)) continue;

      if (m.isCancel) {
        beep_click();
        if (s_pendingMode[0] != '\0') {
          // A mode was already chosen and was only waiting out the post
          // band change settle window. The user made that choice; Cancel
          // closes the selector, it does not undo a completed selection.
          if ((SliceActiveVal >= 0) && (SliceActiveVal < fRig.nMaxSlice)) {
            fRig.setMode(SliceActiveVal, s_pendingMode);
          }
          s_pendingMode[0] = '\0';
        }
        close();            // MenuActive = false inside close()
        return;
      }

      if (!m.enabled) {
        // Disabled tile: ignore cleanly
        return;
      }

      // Visual confirmation of the mode hit
      tft.drawRect(m.r.x + 2, m.r.y + 2, m.r.w - 4, m.r.h - 4, COLOR_YELLOW);
      delay(60);

      beep_click();
      if (CFG_QsySel_Action == QSYSEL_ACTION_BANDMODE) {
        applyBandModeAndArmClose(m.label);
      } else {
        // Apply the selected band+mode profile and close after a short delay
        s_deferredCloseAtMs = millis() + (uint32_t)CFG_Profile_Selector_Close_Delay_Ms;
        applyProfileAndClose(s_selectedBand, m.label);
      }
      return;
    }
    // Tap outside any tile: ignore
    return;
  }

  // Hidden or unknown page: do nothing
}

} // namespace QsySel

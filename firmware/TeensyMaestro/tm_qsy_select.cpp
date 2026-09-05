// ============================================================================
// tm_qsy_select.cpp
// Modal Band→Mode QSY selector for TeensyMaestro
// ============================================================================

#include "tm_qsy_select.h"

extern bool MenuActive;
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
static BandTile s_bands[12];
static ModeTile s_modes[6];

// Deferred close after mode tap, and a small arm delay to avoid band→mode bounce
static uint32_t s_deferredCloseAtMs = 0;
static uint32_t s_touchArmUntilMs   = 0;   // while now < this, ignore touches in Modes page

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

  const char* labels[6] = { "SSB", "CW", "FM", "DIGU", "", "Cancel" };

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
  // FM disabled below 10m → only 10m and 6m enabled
  // SSB disabled on 30m
  uint8_t meters = (uint8_t)atoi(s_selectedBand);

  for (int i = 0; i < 6; ++i) {
    if (s_modes[i].label[0] == '\0') { s_modes[i].enabled = false; continue; }
    if (s_modes[i].isCancel) { s_modes[i].enabled = true; continue; }

    const char* lab = s_modes[i].label;
    bool en = true;
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
  // Uniform blue buttons for all modes; red for Cancel; gray for disabled
  for (int i = 0; i < 6; ++i) {
    const ModeTile& m = s_modes[i];
    uint16_t fill = m.isCancel ? COL_CANCEL : COL_BTN;
    uint16_t out  = COL_BTN_OUT;
    uint16_t txt  = COL_BTN_TXT;

    if (!m.enabled && !m.isCancel) {
      fill = COL_BTN_DIS;
      txt  = COLOR_BLACK;
    }
    drawButton(m.r, m.label, fill, out, txt);
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

static void openBandsPage() {
  s_selectedBand[0] = '\0';
  s_deferredCloseAtMs = 0;
  layoutBands();
  switchPage(Page::Bands);
  resetTimeout();
  s_dirty = true;
  beep_click();
}

static void openModesPage() {
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

  // Perform deferred close, then exit
  if (s_deferredCloseAtMs && now >= s_deferredCloseAtMs) {
    s_deferredCloseAtMs = 0;
    close();            // <-- actually close; do NOT just clear the timestamp
    return;
  }

  // Idle timeout
  if ((now - s_lastActivityMs) >= (uint32_t)CFG_Profile_Selector_Timeout_Ms) {
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

void renderIfDirty() { s_dirty = true; }

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

      // Apply the selected band+mode profile and close after a short delay
      beep_click();
      s_deferredCloseAtMs = millis() + (uint32_t)CFG_Profile_Selector_Close_Delay_Ms;
      applyProfileAndClose(s_selectedBand, m.label);
      return;
    }
    // Tap outside any tile: ignore
    return;
  }

  // Hidden or unknown page: do nothing
}

} // namespace QsySel

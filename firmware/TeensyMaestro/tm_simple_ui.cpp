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

/*
  tm_simple_ui defines layout primitives and printing helpers, so boot, info screens,
  and widgets can render consistently.
*/

#include "tm_simple_ui.h"
// [PATCH:TMUI:INCLUDE_PGM]
#include <avr/pgmspace.h>  // for strncpy_P on Teensy


namespace TMUI {

  constexpr int16_t kPanelW = 480;
  constexpr int16_t kPanelH = 320;

  constexpr int16_t BOOT_W = kPanelW;
  constexpr int16_t BOOT_H = 26;

  constexpr int16_t SYS_W  = kPanelW;
  constexpr int16_t SYS_H  = kPanelH;

  constexpr int16_t CW_W   = kPanelW;
  constexpr int16_t CW_H   = kPanelH;

  constexpr int16_t BANNER_W = kPanelW;
  constexpr int16_t BANNER_H = kPanelH;

  // We tried DMAMEM before but it caused crashes on Teensy 4.1 during boot, immediately after a flash.
  static BootScreen_t sBootProgress("boot-progress", 0, kPanelH - BOOT_H, BOOT_W, BOOT_H);
  static InfoScreen_t sSystemInfo  ("system-info",   0, 0,                 SYS_W,  SYS_H);
  static CwScreen_t   sCwKeyerOnly ("cw-keyer-only", 0, 0,                 CW_W,   CW_H);
  static BannerScreen_t sBanner    ("initial-banner",  0, 0,           BANNER_W, BANNER_H);

  static bool sLayoutInited = false;

  static FLASHMEM void setScreenDefaults() {

    Paint pBoot; pBoot.bgColor = BootTheme::Background; pBoot.textColor = BootTheme::PrimaryText; pBoot.font = &Arial_14;
    Paint pInfo; pInfo.bgColor = BootTheme::Background; pInfo.textColor = BootTheme::PrimaryText; pInfo.font = &Arial_14;
    Paint pCw;   pCw.bgColor   = BootTheme::Background; pCw.textColor   = BootTheme::PrimaryText; pCw.font   = &Arial_14;

    sBootProgress.setPaint(pBoot);
    sSystemInfo.setPaint(pInfo);
    sCwKeyerOnly.setPaint(pCw);
    sBanner.setPaint(pBoot);

  }

  static FLASHMEM void addAreasOnce() {
    sBootProgress.addArea(SimpleArea("status",
      0, 0, BOOT_W, BOOT_H,
      0, 8,
      LayoutMode::SingleLine));

    // [SYSINFO_LAYOUT] --- begin
    // Deterministic geometry for System Info (title, 3+4+2 rows with headers).
    // Note: header areas (16 px) and KV-rows (18 px) are separated with at least 4 px.

    sSystemInfo.addArea(SimpleArea("main-title", 14,   6, SYS_W - 28, 24, 0,   0,   LayoutMode::KeyValue)); // left-aligned title area

    // --- Device block ---
    sSystemInfo.addArea(SimpleArea("hdr-device", 14,  48, SYS_W - 28, 16, 0,   0,   LayoutMode::SingleLine)); // header
    sSystemInfo.addArea(SimpleArea("line1",      14,  68, SYS_W - 28, 18, 0, 124,   LayoutMode::KeyValue));
    sSystemInfo.addArea(SimpleArea("line2",      14,  88, SYS_W - 28, 18, 0, 124,   LayoutMode::KeyValue));
    sSystemInfo.addArea(SimpleArea("line3",      14, 108, SYS_W - 28, 18, 0, 124,   LayoutMode::KeyValue));

    // --- Flex block ---
    sSystemInfo.addArea(SimpleArea("hdr-flex",   14, 130, SYS_W - 28, 16, 0,   0,   LayoutMode::SingleLine)); // header
    sSystemInfo.addArea(SimpleArea("line4",      14, 150, SYS_W - 28, 18, 0, 124,   LayoutMode::KeyValue));
    sSystemInfo.addArea(SimpleArea("line5",      14, 170, SYS_W - 28, 18, 0, 124,   LayoutMode::KeyValue));
    sSystemInfo.addArea(SimpleArea("line6",      14, 190, SYS_W - 28, 18, 0, 124,   LayoutMode::KeyValue));
    sSystemInfo.addArea(SimpleArea("line7",      14, 210, SYS_W - 28, 18, 0, 124,   LayoutMode::KeyValue));

    // --- Client block ---
    sSystemInfo.addArea(SimpleArea("hdr-client", 14, 232, SYS_W - 28, 16, 0,   0,   LayoutMode::SingleLine)); // header
    sSystemInfo.addArea(SimpleArea("line8",      14, 252, SYS_W - 28, 18, 0, 124,   LayoutMode::KeyValue));
    sSystemInfo.addArea(SimpleArea("line9",      14, 272, SYS_W - 28, 18, 0, 124,   LayoutMode::KeyValue));

    // [SYSINFO_LAYOUT] --- end


    sCwKeyerOnly.addArea(SimpleArea("wpm",       14,  40, CW_W - 28,  24, 0, 64,  LayoutMode::KeyValue));
    sBanner.addArea(SimpleArea("banner",
      0, 0, BANNER_W, BANNER_H,
      0, 0,
      LayoutMode::SingleLine));
  }

  FLASHMEM bool InitSimpleLayout() {
    if (sLayoutInited) return true;
    setScreenDefaults();
    addAreasOnce();
    sLayoutInited = true;
    return true;
  }

  FLASHMEM BootScreen_t& BootProgress() { return sBootProgress; }
  FLASHMEM InfoScreen_t& SystemInfo()   { return sSystemInfo; }
  FLASHMEM CwScreen_t&   CwKeyerOnly()  { return sCwKeyerOnly; }
  FLASHMEM BannerScreen_t& Banner()     { return sBanner; }

  // [TMUI_SYSINFO_IMPL] --- begin
  void SysInfo_Frame(InfoScreen_t& sys, TFT_Driver& tft, const char* title)
  {
    sys.clearAll(tft);

    const SimpleArea* titleArea = sys.findArea("main-title");
    if (!titleArea) return;
    const Rect r = titleArea->absoluteRect(sys);

    // Left-aligned title (area already starts at x=14). Vertically center text.
    tft.setTextWrap(false);
    tft.setFont(Arial_20_Bold);
    tft.setTextColor(BootTheme::PrimaryText);
    const char* text = title ? title : "System Info";
    const int16_t cursorY = TMUI::vCenterCursorY_c(tft, text, r.y, r.h);
    tft.setCursor(r.x, cursorY);
    tft.print(text);

    // Accent separator: draw exactly below the title area with full area width.
    const int16_t sepY = r.y + r.h + 10;
    tft.fillRect(r.x, sepY, r.w, 2, BootTheme::Accent);
  }

  // [PATCH:TMUI:SysInfo_Header] -- replace whole function
  void SysInfo_Header(InfoScreen_t& sys, TFT_Driver& tft,
                      const char* headerAreaName, const char* sectionTitle)
  {
    if (!headerAreaName || !sectionTitle || !*sectionTitle) return;
    const SimpleArea* a = sys.findArea(headerAreaName);
    if (!a) return;

    const Rect r = a->absoluteRect(sys);
    tft.setTextWrap(false);
    tft.setFont(Arial_14_Bold);
    tft.setTextColor(BootTheme::PrimaryText);
    const int16_t y = TMUI::vCenterCursorY_c(tft, sectionTitle, r.y, r.h);
    tft.setCursor(r.x, y);
    tft.print(sectionTitle);
  }

  void SysInfo_SectionBefore(InfoScreen_t& sys, TFT_Driver& tft,
                              const char* beforeAreaName, const char* sectionTitle)
  {
    if (!sectionTitle || !*sectionTitle) return;

    // Deterministic anchors (no overlap with KV rows):
    // - "Device": 5 px under separator (which is 5 px under title bottom)
    // - "Flex":   22 px above line4
    // - "Client": 22 px above line6
    int16_t y = 0;

    if (strcmp(beforeAreaName, "line1") == 0) {
      const SimpleArea* titleArea = sys.findArea("main-title");
      if (!titleArea) return;
      const Rect tr = titleArea->absoluteRect(sys);
      const int16_t sepY = tr.y + tr.h + 5;
      y = sepY + 5;   // Device (line1 group)
    } else if (strcmp(beforeAreaName, "line4") == 0) {
      const SimpleArea* a = sys.findArea("line4");
      if (!a) return;
      const Rect ar = a->absoluteRect(sys);
      y = ar.y - 22;  // Flex above Connected/Model rows
    } else if (strcmp(beforeAreaName, "line6") == 0) {
      const SimpleArea* a = sys.findArea("line6");
      if (!a) return;
      const Rect ar = a->absoluteRect(sys);
      y = ar.y - 22;  // Client above Station/Client S/W rows
    } else {
      // Fallback: place 22 px above referenced line
      const SimpleArea* a = sys.findArea(beforeAreaName);
      if (!a) return;
      const Rect ar = a->absoluteRect(sys);
      y = ar.y - 22;
    }

    const int16_t x = sys.x() + 14;
    tft.setFont(Arial_14_Bold);
    tft.setTextColor(BootTheme::Accent);
    if (y < sys.y()) y = sys.y();
    tft.setCursor(x, y);
    tft.print(sectionTitle);
  }

  // [TMUI_SYSINFO_IMPL] SysInfo_KV
  void SysInfo_KV(InfoScreen_t& sys, TFT_Driver& tft,
                  const char* areaName, InfoKey key, const char* valueC)
  {
    SimpleArea* a = const_cast<SimpleArea*>(sys.findArea(areaName));
    if (!a) return;

    // --- Key label (secondary grey) ---
    const Paint base = sys.paint();
    Paint keyPaint = base;
    keyPaint.textColor = BootTheme::SecondaryText;
    a->setPaintOverride(keyPaint);

    // Resolve label from PROGMEM and build "- <label>" on stack
    const __FlashStringHelper* fsh = InfoLabel(key);  // PROGMEM string
    char labelBuf[48];
    if (fsh) {
      // Copy from flash to RAM buffer safely
      strncpy_P(labelBuf, reinterpret_cast<const char*>(fsh), sizeof(labelBuf) - 1);
      labelBuf[sizeof(labelBuf) - 1] = '\0';
    } else {
      labelBuf[0] = '\0';
    }

    char keyBuf[64];
    snprintf(keyBuf, sizeof(keyBuf), "- %s", labelBuf);

    a->printTitleC(sys, tft, keyBuf, Truncation::None);
    a->clearPaintOverride();

    // --- Value (primary), with "N/A" fallback ---
    const char* val = (valueC && valueC[0]) ? valueC : "N/A";
    a->printValueC(sys, tft, val, Truncation::Ellipsis);
  }
  // [TMUI_SYSINFO_IMPL] --- end


} // namespace TMUI

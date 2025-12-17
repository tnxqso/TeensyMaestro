/*
  TeensyMaestro — Community Edition (CE)
  SPDX-License-Identifier: CC-BY-NC-SA-3.0
  SPDX-FileCopyrightText: 2025 TNX QSO
*/

#include "ui_boot.h"
#include "tm_config.h"
#include "tm_simple_ui.h"
#include "tm_system_utils.h"
#include "Display_Driver.h"
#include "Display_Colors.h"
#include "Display_Fonts.h"

static TFT_Driver* s_tft = nullptr;

void UI_Boot::attachTFT(void* tftPtr) {
  s_tft = reinterpret_cast<TFT_Driver*>(tftPtr);
}

static inline bool haveTFT() {
  return s_tft != nullptr;
}

FLASHMEM void UI_Boot::showInitialBanner(uint16_t bgColor /* = 0xFFFF */, const char* byline /* = nullptr */)
{
  if (!haveTFT()) return;

  if (bgColor == 0xFFFF)
    bgColor = BootTheme::Background;

  // Try SimpleUI banner first
  if (TMUI::InitSimpleLayout()) {
    auto& banner = TMUI::Banner();
    const TMUI::SimpleArea* area = banner.findArea("banner");
    if (area) {
      TMUI::RenderInitialBanner(banner, *s_tft, *area, TM_SHORT_NAME, byline /*nullable*/, bgColor);
      return;
    }
  }

}

// [UI_BOOT_SHOWINFO] --- begin
FLASHMEM void UI_Boot::showInfo()
{
  if (!haveTFT()) return;

  // Collect boot/info snapshot
  BootInfo info = BuildBootInfo();

  // Initialize layout and get the System Info screen instance
  if (!TMUI::InitSimpleLayout()) return;
  auto& sys = TMUI::SystemInfo();

  // ----- Title + separator -----
  TMUI::SysInfo_Frame(sys, *s_tft, "System Info");

  // ----- Section headers strictly inside hdr-* areas (no overlap) -----
  TMUI::SysInfo_Header(sys, *s_tft, "hdr-device", "Device");
  TMUI::SysInfo_Header(sys, *s_tft, "hdr-flex",   "Flex");
  if (info.rigConnected) {
    TMUI::SysInfo_Header(sys, *s_tft, "hdr-client", "Client");
  }

  // ============================
  // Device block (lines 1–3)
  // ============================

  char verLine[96];
  if (info.version.length() > 0) {
    strncpy(verLine, info.version.c_str(), sizeof(verLine) - 1);
    verLine[sizeof(verLine) - 1] = '\0';
  } else {
    verLine[0] = '\0';
  }

  #if DEBUG == 1
  if (info.isDebugBuild) {
    const size_t curLen = strlen(verLine);
    if (curLen + 4 < sizeof(verLine)) {
      strncat(verLine, " (D)", sizeof(verLine) - 1 - curLen);
    }
  }
  #endif

  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
          info.mac[0], info.mac[1], info.mac[2],
          info.mac[3], info.mac[4], info.mac[5]);

  TMUI::SysInfo_KV(sys, *s_tft, "line1", TMUI::InfoKey::Version, verLine);
  TMUI::SysInfo_KV(sys, *s_tft, "line2", TMUI::InfoKey::MAC,     macStr);
  TMUI::SysInfo_KV(sys, *s_tft, "line3", TMUI::InfoKey::IP,      info.ipStr.c_str());

  // ============================
  // Flex block lines
  // ============================
  TMUI::SysInfo_KV(sys, *s_tft, "line4", TMUI::InfoKey::Connected, info.rigConnected ? "Yes" : "No");
  if (info.rigConnected) {
    TMUI::SysInfo_KV(sys, *s_tft, "line5", TMUI::InfoKey::Model,   info.rigModel.c_str());
    TMUI::SysInfo_KV(sys, *s_tft, "line6", TMUI::InfoKey::Serial,  info.rigSerial ? "Yes" : "No");
    TMUI::SysInfo_KV(sys, *s_tft, "line7", TMUI::InfoKey::FW,      info.rigVersion.c_str());

    // ============================
    // Client block lines
    // ============================
    TMUI::SysInfo_KV(sys, *s_tft, "line8", TMUI::InfoKey::Station,  info.clientStation.c_str());
    TMUI::SysInfo_KV(sys, *s_tft, "line9", TMUI::InfoKey::ClientSW, info.clientProgram.c_str());
  }

}

FLASHMEM void UI_Boot::showProgress(BootStage stage, const char* note) {
  if (!haveTFT()) return;

  TMUI::InitSimpleLayout();

  const char* label = "";
  switch (stage) {
    case BootStage::Start:        label = "Starting...";       break;
    case BootStage::InitDisplay:  label = "Init display...";   break;
    case BootStage::InitNetwork:  label = "Init network...";   break;
    case BootStage::InitStorage:  label = "Init storage...";   break;
    case BootStage::DiscoverFlex: label = "Discover Flex...";  break;
    case BootStage::ConnectFlex:  label = "Connect Flex...";   break;
    case BootStage::LoadConfig:   label = "Load config...";    break;
    case BootStage::Done:         label = "Done!";           break;
  }
  String line = label;
  if (note && *note) {
    line += " — ";
    line += note;
  }

  auto& boot = TMUI::BootProgress();
  const TMUI::SimpleArea* status = boot.findArea("status");
  if (!status) return;

  status->clearAll(boot, *s_tft);
    // [UI_BOOT_PROGRESS_CSTR] use no-heap C-string path
  status->printFullC(boot, *s_tft, line.c_str(), TMUI::Truncation::Ellipsis, /*sideGuardPx*/ 4);

}

FLASHMEM void UI_Boot::Prog(BootStage st, const char* s) {
  UI_Boot::showProgress(st, s ? s : "");
}

// [UI_BOOT_PROG_PSTR] --- begin
FLASHMEM void UI_Boot::Prog(BootStage st, const __FlashStringHelper* s) {
  char buf[128];
  if (s) {
    strncpy_P(buf, (PGM_P)s, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
  } else {
    buf[0] = '\0';
  }
  UI_Boot::showProgress(st, buf);
}
// [UI_BOOT_PROG_PSTR] --- end

FLASHMEM void UI_Boot::Progf(BootStage st, const char* fmt, ...) {
  char buf[160];
  va_list ap; 
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  UI_Boot::showProgress(st, buf);
}

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
/*
  tm_simple_ui.h
  Minimal, fixed-layout UI scaffolding for TeensyMaestro.

  Goals:
  - Static screens with fixed pixel geometry (no runtime math gymnastics)
  - Named areas per screen, each with a title-slot and a value-slot
  - Simple APIs: clear area/title/value; print title/value/full
  - Colors/fonts default at screen level; areas can optionally override
  - Easy to reason about: "what you set is what you get"
  
  Dependencies:
  - Display_Driver.h  (TFT_Driver)
  - Display_Colors.h  (BootTheme or your palette)
  - Display_Fonts.h   (ILI9341_t3_font_t fonts)
*/

#include <Arduino.h>
#include <stdint.h>
#include <string.h>
#include <array>
#include "Display_Driver.h"
#include "Display_Colors.h"
#include "Display_Fonts.h"

namespace TMUI {

inline static uint16_t tmui_width_of_cstr(TFT_Driver& tft, const char* s);
inline static void     tmui_fit_text_to_width_c(TFT_Driver& tft,
                                                const char* src,
                                                char* dst, size_t dstLen,
                                                uint16_t maxW,
                                                bool withEllipsis);
inline static int16_t  vCenterCursorY_c(TFT_Driver& tft, const char* text,
                                        int16_t areaY, int16_t areaH);

// Forward declaration (templated)
template<size_t kMaxAreas>
class SimpleScreen;

struct Paint {
  uint16_t bgColor     = BootTheme::Background;
  uint16_t textColor   = BootTheme::PrimaryText;
  const ILI9341_t3_font_t* font = &Arial_14;
};

struct Rect {
  int16_t x = 0;
  int16_t y = 0;
  int16_t w = 0;
  int16_t h = 0;

  inline bool contains(int16_t px, int16_t py) const {
    return (px >= x) && (py >= y) && (px < x + w) && (py < y + h);
  }
};

enum class Truncation : uint8_t {
  None = 0,
  Ellipsis = 1
};

enum class LayoutMode : uint8_t {
  KeyValue = 0,
  SingleLine = 1
};

class SimpleArea {
public:
  SimpleArea() = default;

  SimpleArea(const char* name,
             int16_t relX, int16_t relY, int16_t width, int16_t height,
             int16_t titleX, int16_t valueX,
             LayoutMode mode = LayoutMode::KeyValue)
  : _relX(relX), _relY(relY), _w(width), _h(height),
    _titleX(titleX), _valueX(valueX), _mode(mode) {
    setName(name);
  }

  const char* name() const { return _name; }
  void setName(const char* n) {
    if (!n) { _name[0] = '\0'; return; }
    strncpy(_name, n, sizeof(_name) - 1);
    _name[sizeof(_name) - 1] = '\0';
  }

  Rect relativeRect() const { return Rect{_relX, _relY, _w, _h}; }

  template<size_t N> Rect   absoluteRect(const SimpleScreen<N>& owner) const;
  template<size_t N> int16_t titleAbsX(const SimpleScreen<N>& owner) const;
  template<size_t N> int16_t valueAbsX(const SimpleScreen<N>& owner) const;
  template<size_t N> int16_t absYTop(const SimpleScreen<N>& owner) const { return owner.y() + _relY; }
  template<size_t N> int16_t absYBottom(const SimpleScreen<N>& owner) const { return owner.y() + _relY + _h; }

  template<size_t N> void clearAll(const SimpleScreen<N>& owner, TFT_Driver& tft) const;
  template<size_t N> void clearTitle(const SimpleScreen<N>& owner, TFT_Driver& tft) const;
  template<size_t N> void clearValue(const SimpleScreen<N>& owner, TFT_Driver& tft) const;
 
  // [TMUI_C_AND_PROGMEM_OVERLOADS] --- begin
  // C-string variants (no heap allocations)
  template<size_t N>
  void printTitleC(const SimpleScreen<N>& owner, TFT_Driver& tft, const char* text,
                   Truncation trunc = Truncation::None, int16_t rightGuardPx = 2) const
  {
    const auto& p = effPaint(owner);
    const Rect r  = absoluteRect(owner);
    tft.setFont(*p.font);
    tft.setTextColor(p.textColor);

    char buf[192]; // Enough for typical lines on a 480px display with 14pt font
    if (trunc == Truncation::Ellipsis) {
      const uint16_t maxW = (uint16_t)max(0, (int)(_valueX - _titleX - rightGuardPx));
      tmui_fit_text_to_width_c(tft, text ? text : "", buf, sizeof(buf), maxW, true);
    } else {
      strncpy(buf, text ? text : "", sizeof(buf) - 1);
      buf[sizeof(buf) - 1] = '\0';
    }

    const int16_t cursorY = vCenterCursorY_c(tft, buf, r.y, r.h);
    tft.setCursor(owner.x() + _relX + _titleX, cursorY);
    tft.print(buf);
  }

  template<size_t N>
  void printValueC(const SimpleScreen<N>& owner, TFT_Driver& tft, const char* text,
                   Truncation trunc = Truncation::None, int16_t rightGuardPx = 2) const
  {
    const auto& p = effPaint(owner);
    const Rect r  = absoluteRect(owner);
    tft.setFont(*p.font);
    tft.setTextColor(p.textColor);

    const int16_t leftX  = owner.x() + _relX + _valueX;
    const int16_t rightX = r.x + r.w - rightGuardPx;
    const uint16_t maxW  = (rightX > leftX) ? (uint16_t)(rightX - leftX) : 0;

    char buf[192];
    if (trunc == Truncation::Ellipsis) {
      tmui_fit_text_to_width_c(tft, text ? text : "", buf, sizeof(buf), maxW, true);
    } else {
      strncpy(buf, text ? text : "", sizeof(buf) - 1);
      buf[sizeof(buf) - 1] = '\0';
    }

    const int16_t cursorY = vCenterCursorY_c(tft, buf, r.y, r.h);
    tft.setCursor(leftX, cursorY);
    tft.print(buf);
  }

  template<size_t N>
  void printFullC(const SimpleScreen<N>& owner, TFT_Driver& tft, const char* text,
                  Truncation trunc = Truncation::Ellipsis, int16_t sideGuardPx = 2) const
  {
    const auto& p = effPaint(owner);
    const Rect r  = absoluteRect(owner);
    tft.setFont(*p.font);
    tft.setTextColor(p.textColor);

    char buf[192];
    if (trunc == Truncation::Ellipsis) {
      const uint16_t maxW = (uint16_t)max(0, (int)(r.w - 2 * sideGuardPx));
      tmui_fit_text_to_width_c(tft, text ? text : "", buf, sizeof(buf), maxW, true);
    } else {
      strncpy(buf, text ? text : "", sizeof(buf) - 1);
      buf[sizeof(buf) - 1] = '\0';
    }

    int16_t bx, by; uint16_t bw, bh;
    tft.getTextBounds((char*)buf, 0, 0, &bx, &by, &bw, &bh);
    const int16_t centerX = r.x + (int16_t)(r.w / 2);
    int16_t startX = (int16_t)(centerX - (int16_t)(bw / 2));
    if (startX < r.x + sideGuardPx) startX = (int16_t)(r.x + sideGuardPx);
    if (startX + (int16_t)bw > r.x + r.w - sideGuardPx)
      startX = (int16_t)((r.x + r.w - sideGuardPx) - (int16_t)bw);

    const int16_t cursorY = vCenterCursorY_c(tft, buf, r.y, r.h);
    tft.setCursor(startX, cursorY);
    tft.print(buf);
  }

  // PROGMEM variants: copy from PSTR/F("..") to stack buffer, then use C-string path
  template<size_t N>
  void printTitle_P(const SimpleScreen<N>& owner, TFT_Driver& tft, const __FlashStringHelper* textP,
                    Truncation trunc = Truncation::None, int16_t rightGuardPx = 2) const
  {
    char tmp[192];
    if (textP) {
      strncpy_P(tmp, (PGM_P)textP, sizeof(tmp) - 1);
      tmp[sizeof(tmp) - 1] = '\0';
    } else {
      tmp[0] = '\0';
    }
    printTitleC(owner, tft, tmp, trunc, rightGuardPx);
  }

  template<size_t N>
  void printValue_P(const SimpleScreen<N>& owner, TFT_Driver& tft, const __FlashStringHelper* textP,
                    Truncation trunc = Truncation::None, int16_t rightGuardPx = 2) const
  {
    char tmp[192];
    if (textP) {
      strncpy_P(tmp, (PGM_P)textP, sizeof(tmp) - 1);
      tmp[sizeof(tmp) - 1] = '\0';
    } else {
      tmp[0] = '\0';
    }
    printValueC(owner, tft, tmp, trunc, rightGuardPx);
  }

  template<size_t N>
  void printFull_P(const SimpleScreen<N>& owner, TFT_Driver& tft, const __FlashStringHelper* textP,
                   Truncation trunc = Truncation::Ellipsis, int16_t sideGuardPx = 2) const
  {
    char tmp[192];
    if (textP) {
      strncpy_P(tmp, (PGM_P)textP, sizeof(tmp) - 1);
      tmp[sizeof(tmp) - 1] = '\0';
    } else {
      tmp[0] = '\0';
    }
    printFullC(owner, tft, tmp, trunc, sideGuardPx);
  }
  // [TMUI_C_AND_PROGMEM_OVERLOADS] --- end

  template<size_t N>
  void printTitle(const SimpleScreen<N>& owner, TFT_Driver& tft, const String& text,
                  Truncation trunc = Truncation::None, int16_t rightGuardPx = 2) const;

  template<size_t N>
  void printValue(const SimpleScreen<N>& owner, TFT_Driver& tft, const String& text,
                  Truncation trunc = Truncation::None, int16_t rightGuardPx = 2) const;

  template<size_t N>
  void printFull(const SimpleScreen<N>& owner, TFT_Driver& tft, const String& text,
                 Truncation trunc = Truncation::Ellipsis, int16_t sideGuardPx = 2) const;

  void setTitleColumnX(int16_t relTitleX) { _titleX = relTitleX; }
  void setValueColumnX(int16_t relValueX) { _valueX = relValueX; }

  LayoutMode layoutMode() const { return _mode; }
  void setLayoutMode(LayoutMode m) { _mode = m; }

  bool   hasPaintOverride() const { return _paintOverrideEnabled; }
  void   setPaintOverride(const Paint& p) { _paintOverride = p; _paintOverrideEnabled = true; }
  void   clearPaintOverride() { _paintOverrideEnabled = false; }

private:
  char    _name[24]      = {0};
  int16_t _relX          = 0;
  int16_t _relY          = 0;
  int16_t _w             = 0;
  int16_t _h             = 0;
  int16_t _titleX        = 0;
  int16_t _valueX        = 40;

  bool    _paintOverrideEnabled = false;
  Paint   _paintOverride;

  LayoutMode _mode = LayoutMode::KeyValue;

  template<size_t N> const Paint& effPaint(const SimpleScreen<N>& owner) const;
};

template<size_t kMaxAreas>
class SimpleScreen {
public:
  SimpleScreen() = default;

  SimpleScreen(const char* name, int16_t x, int16_t y, int16_t w, int16_t h)
  : _x(x), _y(y), _w(w), _h(h) {
    setName(name);
  }

  const char* name() const { return _name; }
  void setName(const char* n) {
    if (!n) { _name[0] = '\0'; return; }
    strncpy(_name, n, sizeof(_name) - 1);
    _name[sizeof(_name) - 1] = '\0';
  }

  int16_t x() const { return _x; }
  int16_t y() const { return _y; }
  int16_t w() const { return _w; }
  int16_t h() const { return _h; }
  Rect rect()  const { return Rect{_x, _y, _w, _h}; }

  const Paint& paint() const { return _paint; }
  void setPaint(const Paint& p) { _paint = p; }

  bool addArea(const SimpleArea& a) {
    if (_count >= kMaxAreas) return false;
    _areas[_count++] = a;
    return true;
  }

  const SimpleArea* findArea(const char* areaName) const {
    if (!areaName) return nullptr;
    for (size_t i = 0; i < _count; ++i) {
      if (strcmp(_areas[i].name(), areaName) == 0) return &_areas[i];
    }
    return nullptr;
  }

  size_t areaCount() const { return _count; }
  const SimpleArea& areaAt(size_t idx) const { return _areas[idx]; }

  void clearAll(TFT_Driver& tft) const {
    tft.fillRect(_x, _y, _w, _h, _paint.bgColor);
  }

private:
  friend class SimpleArea;

  char    _name[16] = {0};
  int16_t _x        = 0;
  int16_t _y        = 0;
  int16_t _w        = 0;
  int16_t _h        = 0;

  Paint   _paint;

  std::array<SimpleArea, kMaxAreas> _areas{};
  size_t _count = 0;
};

// Helpers for C-string rendering to avoid heap allocations from Arduino String.

inline static uint16_t tmui_width_of_cstr(TFT_Driver& tft, const char* s) {
  if (!s) return 0;
  int16_t x1, y1; uint16_t w, h;
  tft.getTextBounds((char*)s, 0, 0, &x1, &y1, &w, &h);
  return w;
}

// [TMUI_FIT_TEXT_WIDTH_C] --- begin
inline static void tmui_fit_text_to_width_c(TFT_Driver& tft,
                                            const char* src,
                                            char* dst, size_t dstLen,
                                            uint16_t maxW,
                                            bool withEllipsis)
{
  if (!src || !dst || dstLen < 2) return;

  // Defensive bound check to silence GCC warnings
  size_t len = 0;
  for (; len < dstLen - 1 && src[len]; ++len) { /* safe manual strlen */ }
  if (len >= dstLen) len = dstLen - 1;

  strncpy(dst, src, dstLen - 1);
  dst[dstLen - 1] = '\0';

  // Temporary variables for text bounds
  int16_t x1, y1;
  uint16_t w, h;

  // Shrink string until it fits
  while (len > 1) {
    tft.getTextBounds(dst, 0, 0, &x1, &y1, &w, &h);
    if (w <= maxW) break;

    if (withEllipsis && len > 4) {
      strcpy(&dst[len - 4], "...");
      len -= 1;
    } else {
      dst[--len] = '\0';
    }
  }
}
// [TMUI_FIT_TEXT_WIDTH_C] --- end

inline static int16_t vCenterCursorY_c(TFT_Driver& tft, const char* text, int16_t areaY, int16_t areaH) {
  if (!text) text = "";
  int16_t bx, by; uint16_t bw, bh;
  tft.getTextBounds((char*)text, 0, 0, &bx, &by, &bw, &bh);
  const int16_t centerY = areaY + (areaH / 2);
  int16_t cursorY       = centerY - (by + (int16_t)(bh / 2));
  const int16_t minY    = areaY + 1;
  const int16_t maxY    = areaY + areaH - 1;
  if (cursorY < minY) cursorY = minY;
  if (cursorY > maxY) cursorY = maxY;
  return cursorY;
}


// ===== SimpleArea inline implementations =====

template<size_t N>
inline const Paint& SimpleArea::effPaint(const SimpleScreen<N>& owner) const {
  return _paintOverrideEnabled ? _paintOverride : owner._paint;
}

template<size_t N>
inline Rect SimpleArea::absoluteRect(const SimpleScreen<N>& owner) const {
  return Rect{
    (int16_t)(owner.x() + _relX),
    (int16_t)(owner.y() + _relY),
    _w,
    _h
  };
}

template<size_t N>
inline int16_t SimpleArea::titleAbsX(const SimpleScreen<N>& owner) const {
  return (int16_t)(owner.x() + _relX + _titleX);
}

template<size_t N>
inline int16_t SimpleArea::valueAbsX(const SimpleScreen<N>& owner) const {
  return (int16_t)(owner.x() + _relX + _valueX);
}

template<size_t N>
inline void SimpleArea::clearAll(const SimpleScreen<N>& owner, TFT_Driver& tft) const {
  const Rect r = absoluteRect(owner);
  const auto& p = effPaint(owner);
  tft.fillRect(r.x, r.y, r.w, r.h, p.bgColor);
}

template<size_t N>
inline void SimpleArea::clearTitle(const SimpleScreen<N>& owner, TFT_Driver& tft) const {
  const auto& p = effPaint(owner);
  const Rect r = absoluteRect(owner);
  const int16_t x = owner.x() + _relX;
  const int16_t w = (_valueX > 0 ? _valueX : r.w);
  tft.fillRect(x, r.y, w, r.h, p.bgColor);
}

template<size_t N>
inline void SimpleArea::clearValue(const SimpleScreen<N>& owner, TFT_Driver& tft) const {
  const auto& p = effPaint(owner);
  const Rect r = absoluteRect(owner);
  const int16_t x = owner.x() + _relX + _valueX;
  const int16_t w = r.w - _valueX;
  if (w > 0) tft.fillRect(x, r.y, w, r.h, p.bgColor);
}

inline static int16_t vCenterCursorY(TFT_Driver& tft, const String& text, int16_t areaY, int16_t areaH) {
  int16_t bx, by; uint16_t bw, bh;
  tft.getTextBounds((char*)text.c_str(), 0, 0, &bx, &by, &bw, &bh);
  const int16_t centerY     = areaY + (areaH / 2);
  int16_t cursorY           = centerY - (by + (int16_t)(bh / 2));
  const int16_t minY        = areaY + 1;
  const int16_t maxY        = areaY + areaH - 1;
  if (cursorY < minY) cursorY = minY;
  if (cursorY > maxY) cursorY = maxY;
  return cursorY;
}

inline static String tmui_fit_text_to_width(TFT_Driver& tft, const String& src, uint16_t maxW, bool withEllipsis) {
  auto widthOf = [&](const String& s) -> uint16_t {
    int16_t x1, y1; uint16_t w, h;
    tft.getTextBounds((char*)s.c_str(), 0, 0, &x1, &y1, &w, &h);
    return w;
  };

  if (!withEllipsis) {
    return src;
  }

  if (widthOf(src) <= maxW) return src;

  const String dots = "...";
  const uint16_t dotsW = widthOf(dots);

  String clipped = src;
  while (clipped.length() > 0) {
    clipped.remove(clipped.length() - 1);
    if (widthOf(clipped) + dotsW <= maxW) { clipped += dots; break; }
  }
  return clipped;
}

template<size_t N>
inline void SimpleArea::printTitle(const SimpleScreen<N>& owner, TFT_Driver& tft, const String& text,
                                   Truncation trunc, int16_t rightGuardPx) const {
  const auto& p = effPaint(owner);
  const Rect r  = absoluteRect(owner);
  tft.setFont(*p.font);
  tft.setTextColor(p.textColor);

  String line = text;
  if (trunc == Truncation::Ellipsis) {
    const uint16_t maxW = (uint16_t)max(0, (int)(_valueX - _titleX - rightGuardPx));
    line = tmui_fit_text_to_width(tft, line, maxW, true);
  }

  const int16_t cursorY = vCenterCursorY(tft, line, r.y, r.h);
  tft.setCursor(owner.x() + _relX + _titleX, cursorY);
  tft.print(line);
}

template<size_t N>
inline void SimpleArea::printValue(const SimpleScreen<N>& owner, TFT_Driver& tft, const String& text,
                                   Truncation trunc, int16_t rightGuardPx) const {
  const auto& p = effPaint(owner);
  const Rect r  = absoluteRect(owner);
  tft.setFont(*p.font);
  tft.setTextColor(p.textColor);

  String line = text;
  if (trunc == Truncation::Ellipsis) {
    const int16_t leftX  = owner.x() + _relX + _valueX;
    const int16_t rightX = r.x + r.w - rightGuardPx;
    const uint16_t maxW  = (rightX > leftX) ? (uint16_t)(rightX - leftX) : 0;
    line = tmui_fit_text_to_width(tft, line, maxW, true);
  }

  const int16_t cursorY = vCenterCursorY(tft, line, r.y, r.h);
  tft.setCursor(owner.x() + _relX + _valueX, cursorY);
  tft.print(line);
}

template<size_t N>
inline void SimpleArea::printFull(const SimpleScreen<N>& owner, TFT_Driver& tft, const String& text,
                                  Truncation trunc, int16_t sideGuardPx) const {
  const auto& p = effPaint(owner);
  const Rect r  = absoluteRect(owner);
  tft.setFont(*p.font);
  tft.setTextColor(p.textColor);

  String line = text;
  if (trunc == Truncation::Ellipsis) {
    const uint16_t maxW = (uint16_t)max(0, (int)(r.w - 2 * sideGuardPx));
    line = tmui_fit_text_to_width(tft, line, maxW, true);
  }

  int16_t bx, by; uint16_t bw, bh;
  tft.getTextBounds((char*)line.c_str(), 0, 0, &bx, &by, &bw, &bh);
  const int16_t centerX = r.x + (int16_t)(r.w / 2);
  int16_t startX = (int16_t)(centerX - (int16_t)(bw / 2));
  if (startX < r.x + sideGuardPx) startX = (int16_t)(r.x + sideGuardPx);
  if (startX + (int16_t)bw > r.x + r.w - sideGuardPx)
    startX = (int16_t)((r.x + r.w - sideGuardPx) - (int16_t)bw);

  const int16_t cursorY = vCenterCursorY(tft, line, r.y, r.h);
  tft.setCursor(startX, cursorY);
  tft.print(line);
}

bool InitSimpleLayout();

// [TMUI_SYSINFO_HELPERS_BLOCK] --- begin
// Standard labels for System Info screen.
enum class InfoKey : uint8_t {
  Version, MAC, IP, Connected, Model, Serial, FW, Station, ClientSW, FlexHost
};

// Return default PROGMEM label for a given InfoKey.
inline const __FlashStringHelper* InfoLabel(InfoKey k) {
  switch (k) {
    case InfoKey::Version:   return F("Version:");
    case InfoKey::MAC:       return F("MAC:");
    case InfoKey::IP:        return F("IP:");
    case InfoKey::Connected: return F("Connected:");
    case InfoKey::Model:     return F("Model:");
    case InfoKey::Serial:    return F("Serial:");
    case InfoKey::FW:        return F("FW:");
    case InfoKey::Station:   return F("Station:");
    case InfoKey::ClientSW:  return F("Client S/W:");
    case InfoKey::FlexHost:  return F("Flex Host:");
  }
  return F("");
}

// Keep capacity close to actual usage to reduce RAM1.
using BootScreen_t   = SimpleScreen<2>;   // uses 1 area
using InfoScreen_t   = SimpleScreen<16>;  // ~13 used; keep some headroom
using CwScreen_t     = SimpleScreen<2>;   // uses 1 area
using BannerScreen_t = SimpleScreen<1>;   // uses 1 area


BootScreen_t&  BootProgress();
InfoScreen_t&  SystemInfo();
CwScreen_t&    CwKeyerOnly();
BannerScreen_t& Banner();

// [TMUI_SYSINFO_API_DECL] --- begin
void SysInfo_Frame(InfoScreen_t& sys, TFT_Driver& tft, const char* title);
void SysInfo_Header(InfoScreen_t& sys, TFT_Driver& tft,
                    const char* headerAreaName, const char* sectionTitle);
void SysInfo_SectionBefore(InfoScreen_t& sys, TFT_Driver& tft,
                           const char* beforeAreaName, const char* sectionTitle);
void SysInfo_KV(InfoScreen_t& sys, TFT_Driver& tft,
                const char* areaName, InfoKey key, const char* valueC);
// [TMUI_SYSINFO_API_DECL] --- end

inline const char* ResolveByline(const char* byline) {
  if (byline && *byline) return byline;

  static char s_bylineBuf[96];
  static const __FlashStringHelper* kDefaultBylineP = F("Originally created by Len, KD0RC");
  strncpy_P(s_bylineBuf, (PGM_P)kDefaultBylineP, sizeof(s_bylineBuf) - 1);
  s_bylineBuf[sizeof(s_bylineBuf) - 1] = '\0';
  return s_bylineBuf;
}

// ===== Initial Banner helper (template, header-only) =====
// Draw a centered title, a thin accent line below, and a centered byline
// within the given SimpleArea's rectangle on 'owner'.
template<size_t N>
inline void RenderInitialBanner(const SimpleScreen<N>& owner,
                                TFT_Driver& tft,
                                const SimpleArea& area,
                                const char* title,
                                const char* byline,
                                uint16_t bgColor)
{
  const Rect r = area.absoluteRect(owner);

  // Clear area background
  tft.fillRect(r.x, r.y, r.w, r.h, bgColor);

  // Title (centered)
  tft.setFont(Arial_28_Bold);
  tft.setTextColor(BootTheme::PrimaryText);
  int16_t bx, by; uint16_t bw, bh;

  const char* safeTitle  = title ? title : "";
  const char* safeByline = ResolveByline(byline);

  tft.getTextBounds((char*)safeTitle, 0, 0, &bx, &by, &bw, &bh);
  const int16_t titleY = r.y + 62; // a pleasant offset from top; same as legacy
  int16_t titleX = r.x + (int16_t)((r.w - (int16_t)bw) / 2);
  if (titleX < r.x + 2) titleX = r.x + 2;

  tft.setCursor(titleX, titleY);
  tft.print(safeTitle);

  // Accent line below title
  const int16_t xMargin = 48;
  const int16_t lineY   = titleY + 28 + 6;
  int16_t lineW = r.w - 2 * xMargin;
  if (lineW < 10) lineW = max<int16_t>(10, r.w - 4);
  int16_t lineX = r.x + (int16_t)((r.w - lineW) / 2);
  if (lineX < r.x) lineX = r.x;

  tft.fillRect(lineX, lineY, lineW, 2, BootTheme::Accent);

  // Byline (centered)
  tft.setFont(Arial_14);
  tft.setTextColor(BootTheme::SecondaryText);
  tft.getTextBounds((char*)safeByline, 0, 0, &bx, &by, &bw, &bh);
  const int16_t bylineY = lineY + 12 + 10;
  int16_t bylineX = r.x + (int16_t)((r.w - (int16_t)bw) / 2);
  if (bylineX < r.x + 2) bylineX = r.x + 2;

  tft.setCursor(bylineX, bylineY);
  tft.print(safeByline);
}

} // namespace TMUI

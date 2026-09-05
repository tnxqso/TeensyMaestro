// ============================================================================
// tm_qsy_select.h
// Modal Band→Mode QSY selector for TeensyMaestro
// ============================================================================

#pragma once
#include <Arduino.h>
#include "Display_Driver.h"
#include "Display_Colors.h"
#include "Display_Fonts.h"
#include "FlexRigTeensy.h"
#include "tm_rig_alias.h"

// Externals provided by TeensyMaestro.ino
extern const byte STPin;
extern const int  BtnClickDur;
extern const int  BtnClickTone;

// QSY Selector action, set by the "Selector Action" key in MMConfig.ini.
//   QSYSEL_ACTION_PROFILE  - compose a SmartSDR global profile name from
//                            the selected band and mode, and load it.
//   QSYSEL_ACTION_BANDMODE - apply the band directly to the panadapter
//                            owning the active slice, then optionally
//                            apply a mode to that slice.
enum QsySelAction : uint8_t {
  QSYSEL_ACTION_PROFILE  = 0,
  QSYSEL_ACTION_BANDMODE = 1
};
extern QsySelAction CFG_QsySel_Action;

// Names kept as-is: they mirror the MMConfig.ini keys
// "Profile Selector Timeout Ms" and "Profile Selector Close Delay
// Ms" one to one. Renaming them would break that mapping.
extern uint16_t CFG_Profile_Selector_Timeout_Ms;
extern uint16_t CFG_Profile_Selector_Close_Delay_Ms;

bool FlexIsHeadless();

namespace QsySel {

// API
void open();                         // Open if connected, not headless, not TX
void close();                        // Force close
void tick();                         // Call periodically (e.g., from loop())
bool isVisible();                    // Selector visible?
// Request a redraw on the next tick(). Safe to call from event callbacks:
// it never touches the display itself.
void markDirty();
// Update the BANDMODE mode highlight in place. Called from
// onSlice_mode() when the radio reports a new mode. Redraws only
// the two affected highlight rectangles, never the whole page,
// because a full redraw from an event callback corrupts the
// display.
void onRadioModeChanged();
void onTouch(int16_t x, int16_t y, bool isRelease); // Route touches while visible

// Look up the configured global profile name for a band and mode bucket.
// meters must be one of 160, 80, 60, 40, 30, 20, 17, 15, 12, 10, 6.
// mode must be one of "CW", "SSB", "FM", "DIGU", compared case insensitively.
// Returns an empty String when the band or the mode is not recognised.
// Unlike the touchscreen path this performs no silent fallback, so callers
// can distinguish a real mapping from a bad request.
String profileNameForBandMode(int meters, const char* mode);

} // namespace QsySel

// ============================================================================
// tm_profile_select.h
// Modal Band→Mode profile selector for TeensyMaestro
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

extern uint16_t CFG_Profile_Selector_Timeout_Ms;
extern uint16_t CFG_Profile_Selector_Close_Delay_Ms; 

bool FlexIsHeadless();

namespace ProfileSel {

// API
void open();                         // Open if connected, not headless, not TX
void close();                        // Force close
void tick();                         // Call periodically (e.g., from loop())
bool isVisible();                    // Selector visible?
void onTouch(int16_t x, int16_t y, bool isRelease); // Route touches while visible

void renderIfDirty();                // Optional explicit redraw

// Look up the configured global profile name for a band and mode bucket.
// meters must be one of 160, 80, 60, 40, 30, 20, 17, 15, 12, 10, 6.
// mode must be one of "CW", "SSB", "FM", "DIGU", compared case insensitively.
// Returns an empty String when the band or the mode is not recognised.
// Unlike the touchscreen path this performs no silent fallback, so callers
// can distinguish a real mapping from a bad request.
String profileNameForBandMode(int meters, const char* mode);

} // namespace ProfileSel

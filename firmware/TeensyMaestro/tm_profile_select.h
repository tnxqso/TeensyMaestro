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

} // namespace ProfileSel

/*
  TM_Macro_Engine.h

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
#include <Arduino.h>
#include "TM_Keyer_Engine.h"

// Macro token handling for $C (Callsign), $N (Serial), $S (Speed), etc.
class TM_Macro_Engine {
public:
    TM_Macro_Engine(TM_Keyer_Engine& engine);

    // Play a macro string (non-blocking, queues events)
    void play(const String& macro);

    // Stop current macro playback immediately
    void stop();

    // Configuration setters
    void setCallsign(const String& callsign) { _myCall = callsign; }
    void setSerialNumber(int serial) { _serialNum = serial; }
    int  getSerialNumber() const { return _serialNum; }
    
    // Call this if user changes serial manually
    void incrementSerial() { _serialNum++; }
    void decrementSerial() { if (_serialNum > 1) _serialNum--; }

private:
    TM_Keyer_Engine& _engine;
    String _myCall;
    int    _serialNum = 1;

    // Internal parsing helper
    void parseAndQueue(const String& msg);
    void queueNumber(int n);
};

// Global instance declaration (defined in TM_Keyer_Wrapper.cpp)
extern TM_Macro_Engine g_macroEngine;
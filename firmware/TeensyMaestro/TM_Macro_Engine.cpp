/*
  TM_Macro_Engine.cpp

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

#include "TM_Macro_Engine.h"
#include <ctype.h>

TM_Macro_Engine::TM_Macro_Engine(TM_Keyer_Engine& engine) : _engine(engine) {
}

void TM_Macro_Engine::stop() {
    _engine.abortNow();
}

void TM_Macro_Engine::queueNumber(int n) {
    String s = String(n);
    for (unsigned int i = 0; i < s.length(); i++) {
        _engine.enqueueChar(s[i]);
    }
}

void TM_Macro_Engine::play(const String& macro) {
    // If macro is empty, do nothing
    if (macro.length() == 0) return;

    // Parse loop
    for (unsigned int i = 0; i < macro.length(); i++) {
        char c = macro[i];

        // 1. Check for Command Token '$'
        if (c == '$') {
            i++;
            if (i >= macro.length()) break; // End of string
            char cmd = toupper(macro[i]);

            // $C = Callsign
            if (cmd == 'C') {
                for (unsigned int k = 0; k < _myCall.length(); k++) {
                    _engine.enqueueChar(_myCall[k]);
                }
            }
            // $N = Serial Number
            else if (cmd == 'N') {
                // Check modifiers: $N+, $N-, $NR (Repeat)
                if (i + 1 < macro.length()) {
                    char next = toupper(macro[i + 1]);
                    if (next == '+') {
                        _serialNum++;
                        i++; 
                        continue; // Done, don't send
                    }
                    if (next == '-') {
                        if (_serialNum > 1) _serialNum--;
                        i++;
                        continue; // Done
                    }
                    if (next == 'R') {
                        // $NR: Send PREVIOUS number (Current - 1)
                        int prev = (_serialNum > 1) ? _serialNum - 1 : 1;
                        queueNumber(prev);
                        i++;
                        continue;
                    }
                }
                
                // Standard $N: Send current and increment
                queueNumber(_serialNum);
                _serialNum++;
            }
            // $S = Speed ($S25, $S30)
            else if (cmd == 'S') {
                // Parse number until non-digit
                String numStr = "";
                while (i + 1 < macro.length() && isDigit(macro[i + 1])) {
                    numStr += macro[++i];
                }
                if (numStr.length() > 0) {
                    int s = numStr.toInt();
                    _engine.enqueueWpm((uint8_t)s);
                } else {
                    // Plain $S (Restore Speed? Not fully supported in this simplified parser yet)
                    // Could store 'Base WPM' and restore it here.
                }
            }
            // $P = Pause ($P500)
            else if (cmd == 'P') {
                String numStr = "";
                while (i + 1 < macro.length() && isDigit(macro[i + 1])) {
                    numStr += macro[++i];
                }
                if (numStr.length() > 0) {
                    int ms = numStr.toInt();
                    _engine.enqueueWait((uint16_t)ms);
                }
            }
        }
        // 2. Prosigns (*AR, *SK, *HH)
        else if (c == '*') {
            // Map common prosigns or just send raw if engine handles '*'
            // The engine treats characters sequentially. 
            // WinKeyer uses Merge (<1B>) for prosigns.
            // For now, let's just queue the characters following '*'. 
            // Ideally we should use KeyerEventType::PROSIGN_START/END here.
            
            _engine.enqueue({KeyerEventType::PROSIGN_START, 0});
            // Look ahead for 2 chars
            if (i + 1 < macro.length()) _engine.enqueueChar(macro[++i]);
            if (i + 1 < macro.length() && isAlpha(macro[i+1])) _engine.enqueueChar(macro[++i]);
            _engine.enqueue({KeyerEventType::PROSIGN_END, 0});
        }
        // 3. Normal Character
        else {
            _engine.enqueueChar(c);
        }
    }
}
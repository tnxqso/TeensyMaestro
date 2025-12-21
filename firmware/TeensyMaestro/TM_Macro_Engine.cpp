/*
  TM_Macro_Engine.cpp
  TeensyMaestro — Community Edition (CE)
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
    if (macro.length() == 0) return;

    // Save current WPM as baseline for restoration
    uint8_t restoreWpm = _engine.getWpm();

    for (unsigned int i = 0; i < macro.length(); i++) {
        char c = macro[i];

        if (c == '$') {
            i++;
            if (i >= macro.length()) break; 
            char cmd = toupper(macro[i]);

            if (cmd == 'C') {
                for (unsigned int k = 0; k < _myCall.length(); k++) {
                    _engine.enqueueChar(_myCall[k]);
                }
            }
            else if (cmd == 'N') {
                if (i + 1 < macro.length()) {
                    char next = toupper(macro[i + 1]);
                    if (next == '+') { _serialNum++; i++; continue; }
                    if (next == '-') { if (_serialNum > 1) _serialNum--; i++; continue; }
                    if (next == 'R') {
                        int prev = (_serialNum > 1) ? _serialNum - 1 : 1;
                        queueNumber(prev); i++; continue;
                    }
                }
                queueNumber(_serialNum);
                _serialNum++;
            }
            else if (cmd == 'S') {
                // Peek ahead for digits
                String numStr = "";
                while (i + 1 < macro.length() && isDigit(macro[i + 1])) {
                    numStr += macro[++i];
                }
                
                if (numStr.length() > 0) {
                    // $S25 -> Set speed
                    int s = numStr.toInt();
                    _engine.enqueueWpm((uint8_t)s);
                } else {
                    // $S -> Restore speed
                    _engine.enqueueWpm(restoreWpm);
                }
            }
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
        else if (c == '*') {
            _engine.enqueue({KeyerEventType::PROSIGN_START, 0});
            // Simplified handling: Queue next 2 valid chars
            int count = 0;
            while(count < 2 && i + 1 < macro.length()) {
                char next = macro[++i];
                if (isAlphaNumeric(next)) {
                    _engine.enqueueChar(next);
                    count++;
                }
            }
            _engine.enqueue({KeyerEventType::PROSIGN_END, 0});
        }
        else {
            _engine.enqueueChar(c);
        }
    }
}
#pragma once
// ============================================================================
// TeensyMaestro logging macros
// Centralized debug macros that can be used both in the main sketch
// and in libraries like FlexRigTeensy.
// ============================================================================

// If DEBUG is not defined before including this header, default to 0.
#ifndef DEBUG
  #define DEBUG 0
#endif

// Debug enabled
#if DEBUG
  #ifndef debug
    #define debug(x)        Serial.print(x)
  #endif
  #ifndef debugln
    #define debugln(x)      Serial.println(x)
  #endif
  #ifndef debugf
    #define debugf(...)     Serial.printf(__VA_ARGS__)
  #endif

// Debug disabled
#else
  #ifndef debug
    #define debug(x)
  #endif
  #ifndef debugln
    #define debugln(x)
  #endif
  #ifndef debugf
    #define debugf(...)     do{}while(0)
  #endif
#endif

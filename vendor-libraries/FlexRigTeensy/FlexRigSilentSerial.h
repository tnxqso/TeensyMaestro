#pragma once
#include "FlexRigTeensy.h"

#if !DEBUG
// Type is declared in FlexRigTeensy.h
extern __fr_serial_null__ __fr_serial_sink__;  // <-- only declaration here
#undef  Serial
#define Serial __fr_serial_sink__
#endif

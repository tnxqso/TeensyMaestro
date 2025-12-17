// tm_printf_stub.cpp
// Global override for the C stdio printf used by third-party libraries (mbedTLS, etc.).
// This prevents newlib's heavy printf implementation from being linked in.

#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>

extern "C" int printf(const char *format, ...) {
#if DEBUG
    // In debug builds: format into a small local buffer and send to Serial.
    // This keeps all printf() output visible without pulling in full stdio.
    char buf[160];

    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);

    Serial.print(buf);
    // We do not bother returning the exact number of characters written.
    return 0;
#else
    // In non-debug builds: swallow all printf calls to minimize code size.
    (void)format;
    return 0;
#endif
}

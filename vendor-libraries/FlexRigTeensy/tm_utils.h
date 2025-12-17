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
#include <Arduino.h>
#include <stdint.h>

/* --------------------------------------------------------------------------
   TeensyMaestro Utility Helpers
   - tm_print_hex32() : RAM1-safe hexadecimal print (no Print base formatter)
   - tm_parse_hex32() : RAM1-safe parser for "0x40000000" or "1073741824"
   -------------------------------------------------------------------------- */

// Prints 32-bit value as 0xHHHHHHHH safely (no division or heavy libs)
static inline void tm_print_hex32(uint32_t v)
{
    char buf[11]; // "0x" + 8 hex + '\0'
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 0; i < 8; ++i) {
        uint8_t nib = (v >> ((7 - i) * 4)) & 0xF;
        buf[2 + i] = (nib < 10) ? (char)('0' + nib) : (char)('A' + (nib - 10));
    }
    buf[10] = '\0';
    Serial.println(buf);
}

// Parses both "0x..." (hex) and "12345" (decimal) safely.
// No strtoul, no malloc, no libc dependencies.
static inline uint32_t tm_parse_hex32(const char* s)
{
    while (*s == ' ' || *s == '\t') { ++s; }    // skip spaces

    bool is_hex = (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'));
    if (is_hex) s += 2;

    uint32_t val = 0u;
    if (is_hex) {
        for (;; ++s) {
            char c = *s;
            uint8_t d;
            if      (c >= '0' && c <= '9') d = (uint8_t)(c - '0');
            else if (c >= 'a' && c <= 'f') d = (uint8_t)(10 + c - 'a');
            else if (c >= 'A' && c <= 'F') d = (uint8_t)(10 + c - 'A');
            else break;
            val = (val << 4) | d;
        }
    } else {
        for (;; ++s) {
            char c = *s;
            if (c < '0' || c > '9') break;
            val = val * 10u + (uint32_t)(c - '0');
        }
    }
    return val;
}

// Convert 32-bit value to "0xHHHHHHHH" C-string (no division, no libc).
static inline void tm_hex32_to_cstr(uint32_t v, char out[11]) {
    out[0] = '0'; out[1] = 'x';
    for (int i = 0; i < 8; ++i) {
        uint8_t nib = (v >> ((7 - i) * 4)) & 0xF;
        out[2 + i] = (nib < 10) ? (char)('0' + nib) : (char)('A' + (nib - 10));
    }
    out[10] = '\0';
}

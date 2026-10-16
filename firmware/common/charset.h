// Single flap table shared by master and unit (fixes the upstream master/unit table mismatch).
// Plain C++ with no STL so it builds for AVR, ESP32 and the native test env.
#pragma once

#include <stddef.h>
#include <stdint.h>

#define FLAP_COUNT 45

// Index = flap position on the drum. Must match the printed flaps.
// Positions 27-29 are '$' '&' '#' (the upstream master default).
static const char FLAP_GLYPHS[FLAP_COUNT + 1] =
    " ABCDEFGHIJKLMNOPQRSTUVWXYZ$&#0123456789:.-?!";

// Returns the flap index for an ASCII char (lowercase allowed), or -1 if no flap shows it.
inline int8_t flapIndex(char c) {
  if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
  for (int8_t i = 0; i < FLAP_COUNT; i++) {
    if (FLAP_GLYPHS[i] == c) return i;
  }
  return -1;
}

// Folds a UTF-8 string to displayable flap characters, one output char per input code point:
// lowercase -> uppercase, Latin-1 accents -> base letter (ã->A, ç->C, é->E), anything else -> blank.
// Writes at most outSize-1 chars plus a terminator. Returns the number of chars written.
size_t foldToFlaps(const char* in, char* out, size_t outSize);

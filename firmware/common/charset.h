// Single flap table shared by master and unit (fixes the upstream master/unit table mismatch).
// Plain C++ with no STL so it builds for AVR, ESP32 and the native test env.
#pragma once

#include <stddef.h>
#include <stdint.h>

#define FLAP_COUNT 45

// Flap glyphs are single bytes: ASCII, Latin-1 for the accented letters, control codes for logos.
#define FLAP_INSTAGRAM '\x01'
#define FLAP_FACEBOOK '\x02'
#define FLAP_TIKTOK '\x03'

// Index = flap position on the drum. Must match the printed flaps (hardware/stl/README.md):
// blank A-Z Ã Ç É 0-9 Á . [TikTok] [Instagram] [Facebook]
static const char FLAP_GLYPHS[FLAP_COUNT + 1] =
    " ABCDEFGHIJKLMNOPQRSTUVWXYZ\xC3\xC7\xC9" "0123456789\xC1.\x03\x01\x02";

// Returns the flap index for a flap byte (ASCII or Latin-1, lowercase allowed), or -1 if no flap
// shows it.
inline int8_t flapIndex(char ch) {
  uint8_t c = (uint8_t)ch;
  if (c >= 'a' && c <= 'z') c = (uint8_t)(c - 'a' + 'A');
  else if (c >= 0xE0 && c <= 0xFE && c != 0xF7) c = (uint8_t)(c - 0x20);  // Latin-1 lowercase
  for (int8_t i = 0; i < FLAP_COUNT; i++) {
    if ((uint8_t)FLAP_GLYPHS[i] == c) return i;
  }
  return -1;
}

// Folds a UTF-8 string to flap bytes, one output char per input code point: lowercase ->
// uppercase, Ã Ç É Á (any case) -> their own flaps (Latin-1 bytes), other Latin-1 accents -> base
// letter (õ->O, ê->E), anything else (including control codes) -> blank.
// Writes at most outSize-1 chars plus a terminator. Returns the number of chars written.
size_t foldToFlaps(const char* in, char* out, size_t outSize);

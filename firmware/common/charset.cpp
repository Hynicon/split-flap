#include "charset.h"

// Base letters for U+00C0..U+00DF and U+00E0..U+00FF; ' ' where there is no sensible letter (×, ÷, Þ, þ).
static const char LATIN1_UPPER[33] = "AAAAAAACEEEEIIIIDNOOOOO OUUUUY S";
static const char LATIN1_LOWER[33] = "AAAAAAACEEEEIIIIDNOOOOO OUUUUY Y";

static char foldCodePoint(uint32_t cp) {
  if (cp < 0x20) return ' ';  // control codes, so text can't reach the logo flaps
  if (cp < 0x80 || (cp >= 0xC0 && cp <= 0xFF)) {
    int8_t i = flapIndex((char)cp);
    if (i >= 0) return FLAP_GLYPHS[i];
  }
  if (cp >= 0xC0 && cp <= 0xDF) return LATIN1_UPPER[cp - 0xC0];
  if (cp >= 0xE0 && cp <= 0xFF) return LATIN1_LOWER[cp - 0xE0];
  return ' ';
}

size_t foldToFlaps(const char* in, char* out, size_t outSize) {
  if (outSize == 0) return 0;
  size_t n = 0;
  const uint8_t* p = (const uint8_t*)in;
  while (*p && n + 1 < outSize) {
    uint32_t cp;
    uint8_t b = *p;
    int extra;
    if (b < 0x80) { cp = b; extra = 0; }
    else if ((b & 0xE0) == 0xC0) { cp = b & 0x1F; extra = 1; }
    else if ((b & 0xF0) == 0xE0) { cp = b & 0x0F; extra = 2; }
    else if ((b & 0xF8) == 0xF0) { cp = b & 0x07; extra = 3; }
    else { cp = 0xFFFD; extra = 0; }  // stray continuation / invalid lead byte
    p++;
    for (int i = 0; i < extra; i++) {
      if ((*p & 0xC0) != 0x80) { cp = 0xFFFD; break; }  // truncated sequence: don't eat the next char
      cp = (cp << 6) | (*p & 0x3F);
      p++;
    }
    out[n++] = foldCodePoint(cp);
  }
  out[n] = '\0';
  return n;
}

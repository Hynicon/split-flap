#include "display/layout.h"

#include <algorithm>

#include "charset.h"

const char* platformId(Platform p) {
  switch (p) {
    case Platform::TikTok: return "tiktok";
    case Platform::Instagram: return "instagram";
    case Platform::Facebook: return "facebook";
  }
  return "";
}

std::string platformLabel(Platform p, uint8_t labelWidth, bool logos) {
  static const char* const LABELS[PLATFORM_COUNT] = {"TT", "IG", "FB"};
  static const char LOGOS[PLATFORM_COUNT] = {FLAP_TIKTOK, FLAP_INSTAGRAM, FLAP_FACEBOOK};
  if (logos) return std::string(1, LOGOS[(uint8_t)p]) + std::string(labelWidth - 1, ' ');
  std::string label = LABELS[(uint8_t)p];
  if (labelWidth == 1) return label.substr(0, 1);  // T / I / F
  label.resize(labelWidth, ' ');
  return label;
}

static std::string padLeft(const std::string& s, uint8_t width) {
  return s.size() >= width ? s : std::string(width - s.size(), ' ') + s;
}

std::string formatCount(uint64_t n, uint8_t width) {
  std::string digits = std::to_string(n);
  if (digits.size() <= width) return padLeft(digits, width);

  // Largest suffix that leaves a non-zero integer part reads best ("12.34M" rather than "12345K").
  static const struct { uint64_t div; char suffix; } UNITS[] = {
      {1000000000ULL, 'B'}, {1000000ULL, 'M'}, {1000ULL, 'K'}};
  for (const auto& u : UNITS) {
    if (n < u.div) continue;
    uint64_t whole = n / u.div;
    std::string wholeStr = std::to_string(whole);
    int room = (int)width - (int)wholeStr.size() - 1;  // after integer part and suffix
    if (room < 0) break;  // smaller suffixes have even longer integer parts
    int decimals = room >= 2 ? std::min(room - 1, 2) : 0;  // '.' + up to 2 digits
    std::string out = wholeStr;
    if (decimals > 0) {
      uint64_t scale = decimals == 1 ? 10 : 100;
      uint64_t frac = (n % u.div) / (u.div / scale);  // integer division == round down
      std::string fracStr = std::to_string(frac);
      out += "." + std::string(decimals - fracStr.size(), '0') + fracStr;
    }
    out += u.suffix;
    return padLeft(out, width);
  }
  return std::string(width, ' ');
}

std::string composeFrame(Platform p, std::optional<uint64_t> count, const FrameConfig& c) {
  std::string label = platformLabel(p, c.labelWidth, c.labelLogos);
  uint8_t digitWidth = c.units > label.size() ? c.units - label.size() : 0;
  std::string body = count ? formatCount(*count, digitWidth) : std::string(digitWidth, ' ');
  std::string frame = label + body;
  frame.resize(c.units, ' ');
  return frame;
}

std::string composeText(const std::string& utf8, uint8_t units, Align align) {
  std::string folded(utf8.size() + 1, '\0');
  size_t n = foldToFlaps(utf8.c_str(), &folded[0], folded.size());
  folded.resize(n);
  // trim surrounding blanks so alignment is meaningful
  size_t first = folded.find_first_not_of(' ');
  if (first == std::string::npos) return std::string(units, ' ');
  folded = folded.substr(first, folded.find_last_not_of(' ') - first + 1);
  if (folded.size() > units) folded.resize(units);

  size_t pad = units - folded.size();
  switch (align) {
    case Align::Left: return folded + std::string(pad, ' ');
    case Align::Right: return std::string(pad, ' ') + folded;
    case Align::Center: return std::string(pad / 2, ' ') + folded + std::string(pad - pad / 2, ' ');
  }
  return folded;
}

// The first candidate that fits, centred; blanks if none does.
static std::string firstThatFits(const std::string* candidates, size_t count, uint8_t units) {
  for (size_t i = 0; i < count; i++) {
    if (candidates[i].size() <= units) return composeText(candidates[i], units, Align::Center);
  }
  return std::string(units, ' ');
}

std::string composeClock(uint8_t hour, uint8_t minute, bool h24, uint8_t units) {
  if (hour > 23 || minute > 59) return std::string(units, ' ');
  char mm[3] = {(char)('0' + minute / 10), (char)('0' + minute % 10), '\0'};
  if (h24) {
    char hh[3] = {(char)('0' + hour / 10), (char)('0' + hour % 10), '\0'};
    const std::string forms[] = {std::string(hh) + " " + mm, std::string(hh) + mm};
    return firstThatFits(forms, 2, units);
  }
  std::string h = std::to_string(hour % 12 == 0 ? 12 : hour % 12);
  const std::string forms[] = {h + " " + mm + (hour < 12 ? " AM" : " PM"), h + " " + mm, h + mm};
  return firstThatFits(forms, 3, units);
}

std::string composeDate(uint8_t day, uint8_t month, DateLang lang, uint8_t units) {
  static const char* const EN[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
  static const char* const PT[12] = {"JAN", "FEV", "MAR", "ABR", "MAI", "JUN", "JUL", "AGO", "SET", "OUT", "NOV", "DEZ"};
  if (day < 1 || day > 31 || month < 1 || month > 12) return std::string(units, ' ');
  std::string d = std::to_string(day), m = (lang == DateLang::Pt ? PT : EN)[month - 1];
  const std::string forms[] = {d + " " + m, d + m, m};
  return firstThatFits(forms, 3, units);
}

std::string frameToUtf8(const std::string& frame) {
  std::string out;
  for (char ch : frame) {
    uint8_t c = (uint8_t)ch;
    if (c == (uint8_t)FLAP_INSTAGRAM) out += "\xEE\x80\x80";       // U+E000
    else if (c == (uint8_t)FLAP_FACEBOOK) out += "\xEE\x80\x81";   // U+E001
    else if (c == (uint8_t)FLAP_TIKTOK) out += "\xEE\x80\x82";     // U+E002
    else if (c < 0x80) out += ch;
    else {
      out += (char)(0xC0 | (c >> 6));
      out += (char)(0x80 | (c & 0x3F));
    }
  }
  return out;
}

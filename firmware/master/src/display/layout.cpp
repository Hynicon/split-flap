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

std::string platformLabel(Platform p, uint8_t labelWidth) {
  static const char* const LABELS[PLATFORM_COUNT] = {"TT", "IG", "FB"};
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
  return std::string(width, '-');
}

std::string composeFrame(Platform p, std::optional<uint64_t> count, const FrameConfig& c) {
  std::string label = platformLabel(p, c.labelWidth);
  uint8_t digitWidth = c.units > label.size() ? c.units - label.size() : 0;
  std::string body = count ? formatCount(*count, digitWidth) : std::string(digitWidth, '-');
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

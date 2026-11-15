#include "display/layout.h"

#include <stdio.h>

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

// Like firstThatFits, for candidates that are already flap bytes (folded), which must not be folded again.
static std::string firstFlapsThatFit(const std::string* candidates, size_t count, uint8_t units) {
  for (size_t i = 0; i < count; i++) {
    const std::string& f = candidates[i];
    if (f.empty() || f.size() > units) continue;
    size_t pad = units - f.size();
    return std::string(pad / 2, ' ') + f + std::string(pad - pad / 2, ' ');
  }
  return std::string(units, ' ');
}

static const char* weatherWord(int code, DateLang lang) {
  // WMO codes: 0 clear, 1 mainly clear, 2-3 cloudy, 45/48 fog, 51-67 drizzle and rain,
  // 71-77 snow, 80-82 showers, 85-86 snow showers, 95-99 thunderstorm.
  enum { Clear, Cloud, Fog, Rain, Snow, Storm, None } w = None;
  if (code == 0 || code == 1) w = Clear;
  else if (code == 2 || code == 3) w = Cloud;
  else if (code == 45 || code == 48) w = Fog;
  else if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) w = Rain;
  else if ((code >= 71 && code <= 77) || code == 85 || code == 86) w = Snow;
  else if (code >= 95 && code <= 99) w = Storm;
  static const char* const EN[] = {"CLEAR", "CLOUD", "FOG", "RAIN", "SNOW", "STORM"};
  static const char* const PT[] = {"LIMPO", "NUVEM", "NEVOA", "CHUVA", "NEVE", "TROVOADA"};
  return w == None ? nullptr : (lang == DateLang::Pt ? PT : EN)[w];
}

std::string composeWeather(int32_t tempTenths, int code, bool fahrenheit, DateLang lang, uint8_t units) {
  int32_t deg = tempTenths >= 0 ? (tempTenths + 5) / 10 : -((-tempTenths + 5) / 10);
  std::string unit = fahrenheit ? "F" : "C";
  std::string number = std::to_string(deg < 0 ? -deg : deg) + unit;
  if (deg < 0) {
    const std::string forms[] = {std::string(lang == DateLang::Pt ? "MENOS " : "MINUS ") + number, "M" + number};
    return firstThatFits(forms, 2, units);
  }
  const char* word = weatherWord(code, lang);
  const std::string forms[] = {word ? std::string(word) + " " + number : number, number};
  return firstThatFits(forms, 2, units);
}

std::string composeCountdown(const std::string& labelUtf8, int64_t secondsLeft, uint8_t units) {
  if (secondsLeft <= 0) return std::string(units, ' ');
  std::string full, brief;
  if (secondsLeft >= 2 * 86400) {
    int64_t d = secondsLeft / 86400, h = (secondsLeft % 86400) / 3600;
    full = std::to_string(d) + "D" + (h ? std::to_string(h) + "H" : "");
    brief = std::to_string(d) + "D";
  } else if (secondsLeft >= 3600) {
    int64_t h = secondsLeft / 3600, m = (secondsLeft % 3600) / 60;
    char mm[4];
    snprintf(mm, sizeof mm, "%02d", (int)m);
    full = std::to_string(h) + "H" + mm + "M";
    brief = std::to_string(h) + "H";
  } else {
    full = brief = std::to_string((secondsLeft + 59) / 60) + "M";
  }
  std::string label(labelUtf8.size() + 1, '\0');
  label.resize(foldToFlaps(labelUtf8.c_str(), &label[0], label.size()));
  size_t a = label.find_first_not_of(' ');
  label = a == std::string::npos ? "" : label.substr(a, label.find_last_not_of(' ') - a + 1);
  // a brief time with the label beats a full time without it: the label says what is coming
  std::string forms[] = {label + " " + full, label + " " + brief, full, brief};
  if (label.empty()) forms[0] = forms[1] = "";
  return firstFlapsThatFit(forms, 4, units);
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

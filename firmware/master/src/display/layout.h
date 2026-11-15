// Pure formatting logic for display frames. No Arduino dependencies: unit-tested in env:native.
#pragma once

#include <stdint.h>

#include <optional>
#include <string>

enum class Platform : uint8_t { TikTok = 0, Instagram = 1, Facebook = 2 };
constexpr uint8_t PLATFORM_COUNT = 3;

enum class Align : uint8_t { Left, Center, Right };
enum class DateLang : uint8_t { En, Pt };

struct FrameConfig {
  uint8_t units = 9;       // total flap units
  uint8_t labelWidth = 2;  // 1: T/I/F, 2: TT/IG/FB
  bool labelLogos = true;  // the label is the platform's logo flap instead of letters
};

const char* platformId(Platform p);  // "tiktok", "instagram", "facebook"
// Label padded to labelWidth: letters, or with `logos` the platform's logo flap.
std::string platformLabel(Platform p, uint8_t labelWidth, bool logos = false);

// Right-aligned, blank-padded count, exactly `width` chars.
// If the digits don't fit, abbreviates with K/M/B and up to 2 decimals (e.g. "12.34M", "987K"),
// always rounding DOWN so the display never overstates. Returns `width` blanks if even that can't fit
// (there is no dash flap).
std::string formatCount(uint64_t n, uint8_t width);

// Full frame for all units: label + count, or label + blanks if there has never been a good value.
std::string composeFrame(Platform p, std::optional<uint64_t> count, const FrameConfig& c);

// Manual text: folded to flap glyphs (accents stripped, uppercased), truncated, aligned to `units`.
std::string composeText(const std::string& utf8, uint8_t units, Align align);

// The time, centred: "14 32" or, with h24 = false, "2 32 PM". There is no colon flap. Shorter
// forms are used when the display is narrow ("2 32", "1432"); blanks if nothing fits.
std::string composeClock(uint8_t hour, uint8_t minute, bool h24, uint8_t units);

// The date, centred: "13 NOV" (month 1..12; Portuguese: "13 FEV"). "13NOV" on a 5-unit display.
std::string composeDate(uint8_t day, uint8_t month, DateLang lang, uint8_t units);

// The weather, centred: "CLEAR 18C", falling back to "18C" when the word doesn't fit (Portuguese words
// with DateLang::Pt). tempTenths is in tenths of a degree, rounded to whole degrees. There is no minus
// or degree flap, so below zero it reads "MINUS 3C" ("MENOS 3C"), or "M3C" on a narrow display.
// code is a WMO weather code as Open-Meteo reports it; unknown codes show no word.
std::string composeWeather(int32_t tempTenths, int code, bool fahrenheit, DateLang lang, uint8_t units);

// A countdown, centred: the label, a blank and the time left ("XMAS 12D", "CLOSE 2H05M", "CLOSE 25M").
// Days from 2 days, hours from 1 hour, minutes below that (rounded up, so it never reads 0M while
// time remains). Shorter forms when the label doesn't fit, then the time alone. secondsLeft > 0.
std::string composeCountdown(const std::string& labelUtf8, int64_t secondsLeft, uint8_t units);

// A frame (flap bytes) as UTF-8 for JSON and logs: accented flaps as themselves, the Instagram,
// Facebook and TikTok logos as U+E000, U+E001 and U+E002 (private use; the web UI draws them).
std::string frameToUtf8(const std::string& frame);

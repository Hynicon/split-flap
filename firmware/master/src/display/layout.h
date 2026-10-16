// Pure formatting logic for display frames. No Arduino dependencies: unit-tested in env:native.
#pragma once

#include <stdint.h>

#include <optional>
#include <string>

enum class Platform : uint8_t { TikTok = 0, Instagram = 1, Facebook = 2 };
constexpr uint8_t PLATFORM_COUNT = 3;

enum class Align : uint8_t { Left, Center, Right };

struct FrameConfig {
  uint8_t units = 9;       // total flap units
  uint8_t labelWidth = 2;  // 1: T/I/F, 2: TT/IG/FB
};

const char* platformId(Platform p);  // "tiktok", "instagram", "facebook"
std::string platformLabel(Platform p, uint8_t labelWidth);

// Right-aligned, blank-padded count, exactly `width` chars.
// If the digits don't fit, abbreviates with K/M/B and up to 2 decimals (e.g. "12.34M", "987K"),
// always rounding DOWN so the display never overstates. Returns `width` dashes if even that can't fit.
std::string formatCount(uint64_t n, uint8_t width);

// Full frame for all units: label + count, or label + dashes if there has never been a good value.
std::string composeFrame(Platform p, std::optional<uint64_t> count, const FrameConfig& c);

// Manual text: folded to flap glyphs (accents stripped, uppercased), truncated, aligned to `units`.
std::string composeText(const std::string& utf8, uint8_t units, Align align);

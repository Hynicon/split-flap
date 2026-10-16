// Device settings and their JSON form (NVS storage and /api/config share it).
// Pure: no Arduino dependencies, unit-tested in env:native.
#pragma once

#include <stdint.h>

#include <string>

#include "display/layout.h"

enum class DisplayMode : uint8_t { Cycle, Single, Text, Off };
enum class SourceKind : uint8_t { Api, Manual };

struct Config {
  uint8_t enabledMask = 0b111;  // bit i = Platform i
  SourceKind source[PLATFORM_COUNT] = {SourceKind::Api, SourceKind::Api, SourceKind::Api};
  uint64_t manualCount[PLATFORM_COUNT] = {0, 0, 0};
  uint16_t pollMinutes = 15;
  uint16_t cycleSeconds = 60;
  FrameConfig frame;  // 9 units, label width 2
  uint8_t speedRpm = 10;
  DisplayMode mode = DisplayMode::Cycle;
  Platform single = Platform::Instagram;
  std::string text;
  Align textAlign = Align::Center;
  uint16_t quietStart = 0;  // minutes after midnight; start == end -> no quiet hours
  uint16_t quietEnd = 0;
  std::string timezone = "UTC0";  // POSIX TZ, e.g. "WET0WEST,M3.5.0/1,M10.5.0" (Lisbon)
  bool celebrate = false;
  uint32_t milestoneStep = 1000;
  std::string adminKey;  // required on every POST; never returned by GET /api/config
};

constexpr uint16_t MIN_CYCLE_SECONDS = 20;  // full revolution ~5 s @12 rpm + 2 s overheat timeout
constexpr size_t MAX_TEXT_BYTES = 64;

std::string configToJson(const Config& c, bool includeSecrets);

// Partial update: only keys present in `json` change. On any invalid value nothing changes and
// the returned string describes the problem; returns "" on success.
std::string applyConfigJson(Config& c, const std::string& json);

// Body of POST /api/tokens (from tools/auth or the web UI).
struct TokenUpdate {
  Platform platform = Platform::Instagram;
  bool clear = false;
  std::string accessToken, refreshToken, pageId, clientKey, clientSecret;
  uint64_t issuedAt = 0, expiresAt = 0, refreshExpiresAt = 0;
};
std::string parseTokenUpdate(const std::string& json, TokenUpdate& out);

bool parsePlatform(const std::string& id, Platform& out);

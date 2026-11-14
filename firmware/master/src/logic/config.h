// Device settings and their JSON form (NVS storage and /api/config share it).
// Pure: no Arduino dependencies, unit-tested in env:native.
#pragma once

#include <stdint.h>

#include <string>
#include <vector>

#include "display/layout.h"

// Cycle: each enabled platform in turn. Random: a word from `words`. Mix: the `mix` list in order.
enum class DisplayMode : uint8_t { Cycle, Single, Text, Off, Random, Clock, Date, Mix };
constexpr uint8_t DISPLAY_MODE_COUNT = 8;
enum class MixKind : uint8_t { Followers, Random, Text, Clock, Date };
constexpr uint8_t MIX_KIND_COUNT = 5;

// One entry of the mixed rotation. A Followers entry shows every enabled platform for `seconds` each.
struct MixItem {
  MixKind kind = MixKind::Followers;
  uint16_t seconds = 60;
  bool operator==(const MixItem& o) const { return kind == o.kind && seconds == o.seconds; }
};
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
  std::vector<std::string> words;  // random mode picks from these
  uint16_t randomSeconds = 60;
  std::vector<MixItem> mix;
  bool clock24h = true;
  DateLang dateLang = DateLang::En;
  std::vector<std::string> presets;  // saved messages for the app's quick-send buttons
  std::string remoteUrl;    // relay server, e.g. "https://flap.example.com"; empty = no relay
  std::string remoteToken;  // device token for the relay; never returned by GET /api/config
  std::string adminKey;  // required on every POST; never returned by GET /api/config
};

constexpr uint16_t MIN_CYCLE_SECONDS = 20;  // full revolution ~5 s @12 rpm + 2 s overheat timeout
constexpr size_t MAX_TEXT_BYTES = 64;
constexpr size_t MAX_WORDS = 100;
constexpr size_t MAX_WORD_BYTES = 32;
constexpr size_t MAX_MIX = 8;
constexpr size_t MAX_PRESETS = 12;

// includeLists = false leaves out `words` and `presets`: NVS stores them under their own keys,
// because one NVS string holds at most 4000 bytes.
std::string configToJson(const Config& c, bool includeSecrets, bool includeLists = true);
std::string wordsToJson(const Config& c);    // {"words":[...]}
std::string presetsToJson(const Config& c);  // {"presets":[...]}

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

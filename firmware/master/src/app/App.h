// The scheduler: polls sources, maintains tokens, picks what the display shows.
// loop() runs on the Arduino loop task; the public API below is also called from web handlers
// (AsyncTCP task), so shared state is guarded by a mutex. Network calls never hold the lock.
#pragma once

#include <Arduino.h>

#include <mutex>
#include <optional>
#include <string>

#include "display/FlapBus.h"
#include "logic/config.h"
#include "logic/rotation.h"
#include "sources/Source.h"
#include "store/TokenStore.h"

class App {
 public:
  void begin();
  void loop();

  // ---- thread-safe API for the web server ----
  std::string statusJson();
  std::string configJson();
  // Returns "" on success, else an error message. Persists on success.
  std::string updateConfig(const std::string& json);
  std::string updateTokens(const std::string& json);
  std::string showText(const std::string& text, const std::string& align);
  void requestRehome();
  // Temporary message that is not saved to settings (e.g. "SETUP" while the Wi-Fi portal is open).
  void setOverlay(const std::string& text);
  bool checkAdminKey(const String& key);
  bool adminKeySet();
  void setAdminKey(const std::string& key);  // from the setup portal
  // ---- for the relay client (net/Remote) ----
  // The relay server's address and this device's token; false if no relay is configured.
  bool remoteSettings(std::string& url, std::string& token);
  // True once after every settings change, so the relay client knows to report the settings again.
  bool takeConfigChanged();
  FlapBus& bus() { return bus_; }

 private:
  struct PlatformState {
    std::optional<uint64_t> count;
    uint64_t lastOkEpoch = 0;
    FetchStatus status = FetchStatus::Error;
    String error = "not fetched yet";
    uint8_t failures = 0;
    uint32_t nextFetchMs = 0;
    uint32_t lastAttemptMs = 0;
  };

  struct WeatherState {
    bool have = false;
    int32_t tempTenths = 0;
    int code = -1;
    bool fahrenheit = false;     // the unit of this reading
    double lat = 0, lon = 0;     // where this reading is for
    uint64_t lastOkEpoch = 0;
    String error = "not fetched yet";
    uint8_t failures = 0;
    uint32_t nextFetchMs = 0;
  };

  Source* sourceFor(Platform p);
  bool pollSources();  // true if it used the network this time
  void pollWeather();
  bool weatherWanted() const;
  void updateDisplay();
  std::string desiredFrame(uint32_t now);
  static uint64_t epochNow();
  bool quietNow();
  std::string clockFrame(bool date);
  std::string weatherFrame();
  std::string countdownFrame();

  std::mutex mu_;
  Config cfg_;
  TokenStore tokens_;
  FlapBus bus_;
  InstagramSource instagram_{tokens_};
  FacebookSource facebook_{tokens_};
  TikTokSource tiktok_{tokens_};
  ManualSource manual_[PLATFORM_COUNT] = {{Platform::TikTok, cfg_.manualCount[0]},
                                          {Platform::Instagram, cfg_.manualCount[1]},
                                          {Platform::Facebook, cfg_.manualCount[2]}};
  PlatformState state_[PLATFORM_COUNT];
  WeatherState weather_;

  Rotation rotation_{[]() -> uint32_t { return esp_random(); }};  // which platform, word or mix entry is on show
  bool configChanged_ = true;  // report the settings to the relay after boot
  std::string requested_;  // last frame handed to the bus
  bool rehomeRequested_ = false;
  bool unitsChanged_ = false;
  bool forceRefresh_ = false;  // re-show even in quiet hours (explicit user action)
  int celebrate_ = -1;         // platform whose milestone is being celebrated
  bool celebrateBlankShown_ = false;
  std::string tzApplied_;
  std::string overlay_;
};

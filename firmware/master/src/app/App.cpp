#include "app/App.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <time.h>

#include "logic/policy.h"
#include "sources/Https.h"
#include "store/ConfigStore.h"

using Lock = std::lock_guard<std::mutex>;

void App::begin() {
  cfg_ = ConfigStore::load();
  for (uint8_t i = 0; i < PLATFORM_COUNT; i++) {
    uint64_t count, at;
    if (ConfigStore::loadCount((Platform)i, count, at)) {
      state_[i].count = count;
      state_[i].lastOkEpoch = at;
      state_[i].error = "cached value from before restart";
    }
  }
  bus_.begin(cfg_.frame.units);  // also scans the bus
  Serial.printf("[app] %u units, label width %u, mode %s\n", cfg_.frame.units, cfg_.frame.labelWidth,
                cfg_.mode == DisplayMode::Cycle ? "cycle" : "other");
}

uint64_t App::epochNow() {
  time_t t = time(nullptr);
  return t > 1700000000 ? (uint64_t)t : 0;  // 0 until NTP has synced
}

bool App::quietNow() {
  if (!epochNow()) return false;
  time_t t = time(nullptr);
  struct tm local;
  localtime_r(&t, &local);
  return inQuietHours(local.tm_hour * 60 + local.tm_min, cfg_.quietStart, cfg_.quietEnd);
}

Source* App::sourceFor(Platform p) {
  switch (p) {
    case Platform::TikTok: return &tiktok_;
    case Platform::Instagram: return &instagram_;
    case Platform::Facebook: return &facebook_;
  }
  return nullptr;
}

void App::loop() {
  {
    Lock lock(mu_);
    if (tzApplied_ != cfg_.timezone) {
      configTzTime(cfg_.timezone.c_str(), "pool.ntp.org", "time.google.com");
      tzApplied_ = cfg_.timezone;
    }
  }
  if (!pollSources()) pollWeather();  // at most one network call per loop
  updateDisplay();
}

bool App::pollSources() {
  uint32_t now = millis();
  for (uint8_t i = 0; i < PLATFORM_COUNT; i++) {
    Platform p = (Platform)i;
    Source* src;
    {
      Lock lock(mu_);
      if (!((cfg_.enabledMask >> i) & 1)) continue;
      PlatformState& s = state_[i];
      if (cfg_.source[i] == SourceKind::Manual) {
        FetchResult r = manual_[i].fetch();
        if (!s.count || *s.count != r.followers || s.status != FetchStatus::Ok) {
          s.count = r.followers;
          s.status = FetchStatus::Ok;
          s.error = "manual";
          s.lastOkEpoch = epochNow();
        }
        continue;
      }
      if ((int32_t)(now - s.nextFetchMs) < 0 || WiFi.status() != WL_CONNECTED) continue;
      s.lastAttemptMs = now;
      src = sourceFor(p);
    }

    // Network: no lock held. One API fetch per loop() so the display keeps moving.
    uint64_t epoch = epochNow();
    FetchResult maint = src->maintainToken(epoch);
    if (!maint.ok()) Serial.printf("[%s] token maintenance: %s\n", src->id(), maint.error.c_str());
    FetchResult r = src->fetch();
    Serial.printf("[%s] %s %llu %s\n", src->id(), fetchStatusName(r.status), r.followers, r.error.c_str());

    Lock lock(mu_);
    PlatformState& s = state_[i];
    if (r.ok()) {
      uint64_t previous = s.count.value_or(0);
      bool changed = !s.count || previous != r.followers;
      s.count = r.followers;
      s.status = FetchStatus::Ok;
      s.error = maint.ok() ? "" : maint.error;  // fetch worked, but warn that refresh is failing
      s.failures = 0;
      s.lastOkEpoch = epoch;
      s.nextFetchMs = millis() + backoffMinutes(0, cfg_.pollMinutes) * 60000UL;
      if (changed) ConfigStore::saveCount(p, r.followers, epoch);
      // Only in the follower modes: a milestone must not interrupt a message, the clock or the mix.
      bool showing = cfg_.mode == DisplayMode::Cycle || (cfg_.mode == DisplayMode::Single && cfg_.single == p);
      if (cfg_.celebrate && showing && crossedMilestone(previous, r.followers, cfg_.milestoneStep)) {
        Serial.printf("[%s] milestone! %llu\n", src->id(), r.followers);
        celebrate_ = i;
        celebrateBlankShown_ = false;
      }
    } else {
      // Keep the last good count on screen; only the status changes.
      s.status = maint.authExpired() ? maint.status : r.status;
      s.error = maint.authExpired() ? maint.error : r.error;
      if (s.failures < 255) s.failures++;
      s.nextFetchMs = millis() + backoffMinutes(s.failures, cfg_.pollMinutes, r.retryAfterSec) * 60000UL;
    }
    return true;
  }
  return false;
}

// Only while the weather is on show (its mode, or an entry in the mix) and a location is set.
bool App::weatherWanted() const {
  if (!cfg_.weatherSet) return false;
  if (cfg_.mode == DisplayMode::Weather) return true;
  if (cfg_.mode != DisplayMode::Mix) return false;
  for (const MixItem& m : cfg_.mix) {
    if (m.kind == MixKind::Weather) return true;
  }
  return false;
}

void App::pollWeather() {
  static const uint16_t WEATHER_MINUTES = 15;
  char url[200];
  {
    Lock lock(mu_);
    if (!weatherWanted() || WiFi.status() != WL_CONNECTED) return;
    WeatherState& w = weather_;
    // A new place or unit makes the reading on hand stale: fetch now.
    bool moved = w.lat != cfg_.weatherLat || w.lon != cfg_.weatherLon || w.fahrenheit != cfg_.weatherFahrenheit;
    if (moved) {
      w.have = false;
      w.failures = 0;
      w.lat = cfg_.weatherLat;
      w.lon = cfg_.weatherLon;
      w.fahrenheit = cfg_.weatherFahrenheit;
      w.nextFetchMs = millis();
    }
    if ((int32_t)(millis() - w.nextFetchMs) < 0) return;
    snprintf(url, sizeof url,
             "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
             "&current=temperature_2m,weather_code&temperature_unit=%s",
             w.lat, w.lon, w.fahrenheit ? "fahrenheit" : "celsius");
  }
  HttpResponse http = httpsGet(url);  // no lock held during the network call
  WeatherResult r = parseOpenMeteo(http.code, http.body);
  Serial.printf("[weather] %s %ld %d %s\n", fetchStatusName(r.status), (long)r.tempTenths, r.code, r.error.c_str());

  Lock lock(mu_);
  WeatherState& w = weather_;
  if (r.status == FetchStatus::Ok) {
    w.have = true;
    w.tempTenths = r.tempTenths;
    w.code = r.code;
    w.lastOkEpoch = epochNow();
    w.error = "";
    w.failures = 0;
    w.nextFetchMs = millis() + WEATHER_MINUTES * 60000UL;
  } else {
    w.error = r.error.c_str();
    if (w.failures < 255) w.failures++;
    w.nextFetchMs = millis() + backoffMinutes(w.failures, WEATHER_MINUTES, http.retryAfterSec) * 60000UL;
  }
}

std::string App::desiredFrame(uint32_t now) {
  const uint8_t units = cfg_.frame.units;
  const std::string blank(units, ' ');

  if (!overlay_.empty()) return composeText(overlay_, units, Align::Center);
  if (celebrate_ >= 0) {
    if (!celebrateBlankShown_) {
      if (bus_.shown() == blank && !bus_.busy()) {
        celebrateBlankShown_ = true;
      } else {
        return blank;  // flip everything to blank first...
      }
    }
    Platform p = (Platform)celebrate_;  // ...then flip straight to the new number
    rotation_.jumpTo(p, now);
    celebrate_ = -1;
    return composeFrame(p, state_[(uint8_t)p].count, cfg_.frame);
  }

  Scene scene = rotation_.update(cfg_, now);
  switch (scene.kind) {
    case SceneKind::Blank: break;
    case SceneKind::Followers: return composeFrame(scene.platform, state_[(uint8_t)scene.platform].count, cfg_.frame);
    case SceneKind::Word: return composeText(scene.word, units, cfg_.textAlign);
    case SceneKind::Text: return composeText(cfg_.text, units, cfg_.textAlign);
    case SceneKind::Clock: return clockFrame(false);
    case SceneKind::Date: return clockFrame(true);
    case SceneKind::Weather: return weatherFrame();
    case SceneKind::Countdown: return countdownFrame();
  }
  return blank;
}

// The local time or date; blank until the clock has synced.
std::string App::clockFrame(bool date) {
  const uint8_t units = cfg_.frame.units;
  if (!epochNow()) return std::string(units, ' ');
  time_t t = time(nullptr);
  struct tm local;
  localtime_r(&t, &local);
  if (date) return composeDate((uint8_t)local.tm_mday, (uint8_t)(local.tm_mon + 1), cfg_.dateLang, units);
  return composeClock((uint8_t)local.tm_hour, (uint8_t)local.tm_min, cfg_.clock24h, units);
}

// The latest weather reading; blank until there is one.
std::string App::weatherFrame() {
  const uint8_t units = cfg_.frame.units;
  if (!weather_.have) return std::string(units, ' ');
  return composeWeather(weather_.tempTenths, weather_.code, weather_.fahrenheit, cfg_.dateLang, units);
}

// Time left to countdown_at; blank until the clock has synced. Once a one-off countdown is over it
// shows countdown_done (or the label).
std::string App::countdownFrame() {
  const uint8_t units = cfg_.frame.units;
  CountdownTarget target;
  if (!epochNow() || !parseCountdownAt(cfg_.countdownAt, target)) return std::string(units, ' ');
  time_t t = time(nullptr);
  struct tm l;
  localtime_r(&t, &l);
  int64_t left = secondsUntil(target, (uint16_t)(l.tm_year + 1900), (uint8_t)(l.tm_mon + 1), (uint8_t)l.tm_mday,
                              (uint8_t)l.tm_hour, (uint8_t)l.tm_min, (uint8_t)l.tm_sec);
  if (left <= 0) {
    return composeText(cfg_.countdownDone.empty() ? cfg_.countdownLabel : cfg_.countdownDone, units, Align::Center);
  }
  return composeCountdown(cfg_.countdownLabel, left, units);
}

void App::updateDisplay() {
  Lock lock(mu_);
  if (unitsChanged_) {
    bus_.setUnits(cfg_.frame.units);
    requested_.clear();
    unitsChanged_ = false;
  }
  if (rehomeRequested_) {
    bus_.rehomeAll();
    rehomeRequested_ = false;
  }
  std::string frame = desiredFrame(millis());
  if (frame != requested_ && (forceRefresh_ || !quietNow())) {
    bus_.show(frame, cfg_.speedRpm);
    requested_ = frame;
    forceRefresh_ = false;
  }
  bus_.update();  // under the lock: statusJson() reads bus state from the web task (I2C polls take ~ms)
}

// ---------------- web API ----------------

static const char* resetReasonName(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON: return "power_on";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "interrupt_watchdog";
    case ESP_RST_TASK_WDT: return "task_watchdog";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_DEEPSLEEP: return "deep_sleep";
    case ESP_RST_EXT: return "external";
    default: return "other";
  }
}

std::string App::statusJson() {
  JsonDocument doc;
  uint32_t now = millis();
  doc["uptime_s"] = now / 1000;
  doc["reset_reason"] = resetReasonName(esp_reset_reason());
  doc["free_heap"] = ESP.getFreeHeap();
  doc["time"] = epochNow();
  doc["wifi"]["ssid"] = WiFi.SSID();
  doc["wifi"]["rssi"] = WiFi.RSSI();
  doc["wifi"]["ip"] = WiFi.localIP().toString();

  Lock lock(mu_);
  doc["display"]["frame"] = frameToUtf8(requested_);
  doc["display"]["quiet"] = quietNow();
  doc["display"]["busy"] = bus_.busy();
  doc["bus"]["units"] = bus_.units();
  JsonArray present = doc["bus"]["present"].to<JsonArray>();
  for (uint8_t u = 0; u < bus_.units(); u++) present.add((bus_.presentMask() >> u) & 1 ? true : false);
  doc["bus"]["error"] = bus_.lastError();

  for (uint8_t i = 0; i < PLATFORM_COUNT; i++) {
    Platform p = (Platform)i;
    const PlatformState& s = state_[i];
    JsonObject o = doc["platforms"][platformId(p)].to<JsonObject>();
    o["enabled"] = ((cfg_.enabledMask >> i) & 1) != 0;
    o["source"] = cfg_.source[i] == SourceKind::Manual ? "manual" : "api";
    if (s.count) o["count"] = *s.count;
    else o["count"] = nullptr;
    o["last_ok"] = s.lastOkEpoch;
    o["status"] = fetchStatusName(s.status);
    o["error"] = s.error;
    o["failures"] = s.failures;
    o["next_fetch_in_s"] = (int32_t)(s.nextFetchMs - now) > 0 ? (s.nextFetchMs - now) / 1000 : 0;
    PlatformTokens t = tokens_.load(p);
    JsonObject tok = o["token"].to<JsonObject>();
    tok["access"] = maskSecret(t.accessToken.c_str());
    tok["refresh"] = maskSecret(t.refreshToken.c_str());
    tok["expires_at"] = t.expiresAt;
    tok["refresh_expires_at"] = t.refreshExpiresAt;
    if (t.pageId.length()) tok["page_id"] = t.pageId;
    tok["needs_reauth"] = s.status == FetchStatus::AuthExpired;
  }
  JsonObject w = doc["weather"].to<JsonObject>();
  w["wanted"] = weatherWanted();
  if (weather_.have) {
    w["temp"] = weather_.tempTenths / 10.0;
    w["code"] = weather_.code;
  } else {
    w["temp"] = nullptr;
  }
  w["unit"] = weather_.fahrenheit ? "f" : "c";
  w["last_ok"] = weather_.lastOkEpoch;
  w["error"] = weather_.error;
  std::string out;
  serializeJson(doc, out);
  return out;
}

std::string App::configJson() {
  Lock lock(mu_);
  return configToJson(cfg_, false);
}

std::string App::updateConfig(const std::string& json) {
  Lock lock(mu_);
  Config before = cfg_;
  std::string err = applyConfigJson(cfg_, json);
  if (!err.empty()) return err;
  ConfigStore::save(cfg_, &before);
  configChanged_ = true;
  if (cfg_.words != before.words) rotation_.wordsChanged();
  if (cfg_.mix != before.mix) rotation_.restart();
  if (cfg_.frame.units != before.frame.units) unitsChanged_ = true;
  for (uint8_t i = 0; i < PLATFORM_COUNT; i++) {
    bool wasOn = (before.enabledMask >> i) & 1, isOn = (cfg_.enabledMask >> i) & 1;
    if ((isOn && !wasOn) || cfg_.source[i] != before.source[i] || cfg_.pollMinutes != before.pollMinutes) {
      state_[i].nextFetchMs = millis();
      state_[i].failures = 0;
    }
  }
  forceRefresh_ = true;
  return "";
}

std::string App::updateTokens(const std::string& json) {
  TokenUpdate u;
  std::string err = parseTokenUpdate(json, u);
  if (!err.empty()) return err;
  Lock lock(mu_);
  if (u.clear) {
    tokens_.clear(u.platform);
  } else {
    PlatformTokens t = tokens_.load(u.platform);  // merge: keep fields the request doesn't set
    if (!u.accessToken.empty()) t.accessToken = u.accessToken.c_str();
    if (!u.refreshToken.empty()) t.refreshToken = u.refreshToken.c_str();
    if (!u.pageId.empty()) t.pageId = u.pageId.c_str();
    if (!u.clientKey.empty()) t.clientKey = u.clientKey.c_str();
    if (!u.clientSecret.empty()) t.clientSecret = u.clientSecret.c_str();
    t.issuedAt = u.issuedAt ? u.issuedAt : epochNow();
    t.expiresAt = u.expiresAt;
    if (u.refreshExpiresAt) t.refreshExpiresAt = u.refreshExpiresAt;
    tokens_.save(u.platform, t);
    Serial.printf("[%s] tokens updated (access %s)\n", platformId(u.platform), maskSecret(u.accessToken).c_str());
  }
  PlatformState& s = state_[(uint8_t)u.platform];
  s.failures = 0;
  s.nextFetchMs = millis();
  s.error = u.clear ? "tokens cleared" : "tokens updated, fetching";
  return "";
}

std::string App::showText(const std::string& text, const std::string& align) {
  JsonDocument doc;
  doc["mode"] = "text";
  doc["text"] = text;
  if (!align.empty()) doc["text_align"] = align;
  std::string json;
  serializeJson(doc, json);
  return updateConfig(json);
}

void App::setOverlay(const std::string& text) {
  Lock lock(mu_);
  overlay_ = text;
  forceRefresh_ = true;
}

void App::requestRehome() {
  Lock lock(mu_);
  rehomeRequested_ = true;
}

bool App::adminKeySet() {
  Lock lock(mu_);
  return !cfg_.adminKey.empty();
}

bool App::checkAdminKey(const String& key) {
  Lock lock(mu_);
  const std::string& expected = cfg_.adminKey;
  if (expected.empty() || key.length() != expected.size()) return false;
  uint8_t diff = 0;  // constant-time compare
  for (size_t i = 0; i < expected.size(); i++) diff |= (uint8_t)(expected[i] ^ key[i]);
  return diff == 0;
}

void App::setAdminKey(const std::string& key) {
  Lock lock(mu_);
  cfg_.adminKey = key;
  ConfigStore::save(cfg_);
  configChanged_ = true;
}

bool App::remoteSettings(std::string& url, std::string& token) {
  Lock lock(mu_);
  url = cfg_.remoteUrl;
  token = cfg_.remoteToken;
  return !url.empty() && !token.empty();
}

bool App::takeConfigChanged() {
  Lock lock(mu_);
  bool changed = configChanged_;
  configChanged_ = false;
  return changed;
}

#include "app/App.h"

#include <ArduinoJson.h>
#include <WiFi.h>
#include <time.h>

#include "logic/policy.h"
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
  pollSources();
  updateDisplay();
}

void App::pollSources() {
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
    return;
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
    current_ = celebrate_;
    cycleStartMs_ = now;
    celebrate_ = -1;
    return composeFrame(p, state_[(uint8_t)p].count, cfg_.frame);
  }

  switch (cfg_.mode) {
    case DisplayMode::Off:
      return blank;
    case DisplayMode::Text:
      return composeText(cfg_.text, units, cfg_.textAlign);
    case DisplayMode::Single:
      return composeFrame(cfg_.single, state_[(uint8_t)cfg_.single].count, cfg_.frame);
    case DisplayMode::Cycle:
      break;
  }
  bool currentEnabled = current_ >= 0 && ((cfg_.enabledMask >> current_) & 1);
  if (!currentEnabled || now - cycleStartMs_ >= cfg_.cycleSeconds * 1000UL) {
    current_ = nextPlatform(current_, cfg_.enabledMask, PLATFORM_COUNT);
    cycleStartMs_ = now;
  }
  if (current_ < 0) return blank;
  return composeFrame((Platform)current_, state_[current_].count, cfg_.frame);
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
  doc["display"]["frame"] = requested_;
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
  ConfigStore::save(cfg_);
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
}

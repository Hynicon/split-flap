#include "logic/config.h"

#include <ArduinoJson.h>
#include <stdio.h>

static const char* const MODE_NAMES[] = {"cycle", "single", "text", "off"};
static const char* const ALIGN_NAMES[] = {"left", "center", "right"};

bool parsePlatform(const std::string& id, Platform& out) {
  for (uint8_t i = 0; i < PLATFORM_COUNT; i++) {
    if (id == platformId((Platform)i)) {
      out = (Platform)i;
      return true;
    }
  }
  return false;
}

static std::string hhmm(uint16_t minutes) {
  char buf[6];
  snprintf(buf, sizeof buf, "%02u:%02u", (unsigned)(minutes / 60), (unsigned)(minutes % 60));
  return buf;
}

static bool parseHhmm(const char* s, uint16_t& out) {
  unsigned h, m;
  char extra;
  if (!s || sscanf(s, "%u:%u%c", &h, &m, &extra) != 2 || h > 23 || m > 59) return false;
  out = (uint16_t)(h * 60 + m);
  return true;
}

std::string configToJson(const Config& c, bool includeSecrets) {
  JsonDocument doc;
  for (uint8_t i = 0; i < PLATFORM_COUNT; i++) {
    JsonObject p = doc["platforms"][platformId((Platform)i)].to<JsonObject>();
    p["enabled"] = ((c.enabledMask >> i) & 1) != 0;
    p["source"] = c.source[i] == SourceKind::Manual ? "manual" : "api";
    p["manual_count"] = c.manualCount[i];
  }
  doc["poll_minutes"] = c.pollMinutes;
  doc["cycle_seconds"] = c.cycleSeconds;
  doc["units"] = c.frame.units;
  doc["label_width"] = c.frame.labelWidth;
  doc["speed"] = c.speedRpm;
  doc["mode"] = MODE_NAMES[(uint8_t)c.mode];
  doc["single"] = platformId(c.single);
  doc["text"] = c.text;
  doc["text_align"] = ALIGN_NAMES[(uint8_t)c.textAlign];
  doc["quiet_start"] = hhmm(c.quietStart);
  doc["quiet_end"] = hhmm(c.quietEnd);
  doc["timezone"] = c.timezone;
  doc["celebrate"] = c.celebrate;
  doc["milestone_step"] = c.milestoneStep;
  if (includeSecrets) doc["admin_key"] = c.adminKey;
  else doc["admin_key_set"] = !c.adminKey.empty();
  std::string out;
  serializeJson(doc, out);
  return out;
}

template <typename T>
static bool readInt(JsonVariantConst v, long min, long max, T& out, std::string& err, const char* name) {
  if (v.isNull()) return true;
  if (!v.is<long>() || v.as<long>() < min || v.as<long>() > max) {
    err = std::string(name) + " must be an integer " + std::to_string(min) + ".." + std::to_string(max);
    return false;
  }
  out = (T)v.as<long>();
  return true;
}

std::string applyConfigJson(Config& live, const std::string& json) {
  JsonDocument doc;
  if (DeserializationError e = deserializeJson(doc, json)) return std::string("invalid json: ") + e.c_str();
  if (!doc.is<JsonObject>()) return "expected a JSON object";

  Config c = live;  // validate on a copy, commit only if everything is valid
  std::string err;

  JsonObjectConst platforms = doc["platforms"];
  for (JsonPairConst kv : platforms) {
    Platform p;
    if (!parsePlatform(kv.key().c_str(), p)) return std::string("unknown platform ") + kv.key().c_str();
    uint8_t i = (uint8_t)p;
    JsonObjectConst o = kv.value();
    if (!o["enabled"].isNull()) {
      if (!o["enabled"].is<bool>()) return "enabled must be true/false";
      c.enabledMask = o["enabled"].as<bool>() ? (c.enabledMask | (1u << i)) : (c.enabledMask & ~(1u << i));
    }
    if (!o["source"].isNull()) {
      std::string s = o["source"] | "";
      if (s == "api") c.source[i] = SourceKind::Api;
      else if (s == "manual") c.source[i] = SourceKind::Manual;
      else return "source must be api or manual";
    }
    if (!o["manual_count"].isNull()) {
      if (!o["manual_count"].is<uint64_t>()) return "manual_count must be a non-negative integer";
      c.manualCount[i] = o["manual_count"].as<uint64_t>();
    }
  }

  if (!readInt(doc["poll_minutes"], 5, 1440, c.pollMinutes, err, "poll_minutes")) return err;
  if (!readInt(doc["cycle_seconds"], MIN_CYCLE_SECONDS, 3600, c.cycleSeconds, err, "cycle_seconds")) return err;
  if (!readInt(doc["units"], 1, 16, c.frame.units, err, "units")) return err;
  if (!readInt(doc["label_width"], 1, 2, c.frame.labelWidth, err, "label_width")) return err;
  if (c.frame.labelWidth >= c.frame.units) return "label_width must be smaller than units";
  if (!readInt(doc["speed"], 1, 12, c.speedRpm, err, "speed")) return err;
  if (!readInt(doc["milestone_step"], 1, 1000000000L, c.milestoneStep, err, "milestone_step")) return err;

  if (!doc["mode"].isNull()) {
    std::string m = doc["mode"] | "";
    size_t colon = m.find(':');  // PLAN.md form "single:<platform>"
    if (colon != std::string::npos) {
      if (!parsePlatform(m.substr(colon + 1), c.single)) return "unknown platform in mode";
      m = m.substr(0, colon);
    }
    bool found = false;
    for (uint8_t i = 0; i < 4; i++) {
      if (m == MODE_NAMES[i]) {
        c.mode = (DisplayMode)i;
        found = true;
      }
    }
    if (!found) return "mode must be cycle, single, text or off";
  }
  if (!doc["single"].isNull() && !parsePlatform(doc["single"] | "", c.single)) return "unknown platform in single";
  if (!doc["text"].isNull()) {
    std::string t = doc["text"] | "";
    if (t.size() > MAX_TEXT_BYTES) return "text too long";
    c.text = t;
  }
  if (!doc["text_align"].isNull()) {
    std::string a = doc["text_align"] | "";
    if (a == "left") c.textAlign = Align::Left;
    else if (a == "center") c.textAlign = Align::Center;
    else if (a == "right") c.textAlign = Align::Right;
    else return "text_align must be left, center or right";
  }
  if (!doc["quiet_start"].isNull() && !parseHhmm(doc["quiet_start"], c.quietStart)) return "quiet_start must be HH:MM";
  if (!doc["quiet_end"].isNull() && !parseHhmm(doc["quiet_end"], c.quietEnd)) return "quiet_end must be HH:MM";
  if (!doc["timezone"].isNull()) {
    std::string tz = doc["timezone"] | "";
    if (tz.empty() || tz.size() > 63) return "timezone must be a POSIX TZ string";
    c.timezone = tz;
  }
  if (!doc["celebrate"].isNull()) {
    if (!doc["celebrate"].is<bool>()) return "celebrate must be true/false";
    c.celebrate = doc["celebrate"];
  }
  if (!doc["admin_key"].isNull()) {
    std::string k = doc["admin_key"] | "";
    if (k.size() < 8 || k.size() > 64) return "admin_key must be 8..64 characters";
    c.adminKey = k;
  }

  live = c;
  return "";
}

std::string parseTokenUpdate(const std::string& json, TokenUpdate& out) {
  JsonDocument doc;
  if (DeserializationError e = deserializeJson(doc, json)) return std::string("invalid json: ") + e.c_str();
  TokenUpdate t;
  if (!parsePlatform(doc["platform"] | "", t.platform)) return "platform must be tiktok, instagram or facebook";
  t.clear = doc["clear"] | false;
  if (!t.clear) {
    t.accessToken = doc["access_token"] | "";
    t.refreshToken = doc["refresh_token"] | "";
    t.pageId = doc["page_id"] | "";
    t.clientKey = doc["client_key"] | "";
    t.clientSecret = doc["client_secret"] | "";
    t.issuedAt = doc["issued_at"] | (uint64_t)0;
    t.expiresAt = doc["expires_at"] | (uint64_t)0;
    t.refreshExpiresAt = doc["refresh_expires_at"] | (uint64_t)0;
    if (t.accessToken.empty() && t.refreshToken.empty()) return "access_token or refresh_token required";
    if (t.platform == Platform::Facebook && t.pageId.empty()) return "page_id required for facebook";
    if (t.platform == Platform::TikTok && !t.refreshToken.empty() && (t.clientKey.empty() || t.clientSecret.empty())) {
      return "client_key and client_secret required with a tiktok refresh_token";
    }
  }
  out = t;
  return "";
}

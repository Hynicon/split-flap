#include "logic/config.h"

#include <ArduinoJson.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

static const char* const MODE_NAMES[DISPLAY_MODE_COUNT] = {"cycle", "single", "text", "off", "random", "clock", "date", "mix", "weather", "countdown"};
static const char* const MIX_NAMES[MIX_KIND_COUNT] = {"followers", "random", "text", "clock", "date", "weather", "countdown"};
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

static void addList(JsonDocument& doc, const char* key, const std::vector<std::string>& list) {
  JsonArray a = doc[key].to<JsonArray>();
  for (const std::string& s : list) a.add(s);
}

std::string wordsToJson(const Config& c) {
  JsonDocument doc;
  addList(doc, "words", c.words);
  std::string out;
  serializeJson(doc, out);
  return out;
}

std::string presetsToJson(const Config& c) {
  JsonDocument doc;
  addList(doc, "presets", c.presets);
  std::string out;
  serializeJson(doc, out);
  return out;
}

std::string configToJson(const Config& c, bool includeSecrets, bool includeLists) {
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
  doc["label_logos"] = c.frame.labelLogos;
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
  doc["random_seconds"] = c.randomSeconds;
  JsonArray mix = doc["mix"].to<JsonArray>();
  for (const MixItem& m : c.mix) {
    JsonObject o = mix.add<JsonObject>();
    o["kind"] = MIX_NAMES[(uint8_t)m.kind];
    o["seconds"] = m.seconds;
  }
  doc["clock_24h"] = c.clock24h;
  doc["date_lang"] = c.dateLang == DateLang::Pt ? "pt" : "en";
  if (c.weatherSet) {
    doc["weather_lat"] = c.weatherLat;
    doc["weather_lon"] = c.weatherLon;
  } else {
    doc["weather_lat"] = "";
    doc["weather_lon"] = "";
  }
  doc["weather_unit"] = c.weatherFahrenheit ? "f" : "c";
  doc["countdown_label"] = c.countdownLabel;
  doc["countdown_at"] = c.countdownAt;
  doc["countdown_done"] = c.countdownDone;
  if (includeLists) {
    addList(doc, "words", c.words);
    addList(doc, "presets", c.presets);
  }
  doc["remote_url"] = c.remoteUrl;
  if (includeSecrets) {
    doc["remote_token"] = c.remoteToken;
    doc["admin_key"] = c.adminKey;
  } else {
    doc["remote_token_set"] = !c.remoteToken.empty();
    doc["admin_key_set"] = !c.adminKey.empty();
  }
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

// A JSON array of strings, each at most maxBytes long. Blank entries are dropped.
static bool readList(JsonVariantConst v, size_t maxItems, size_t maxBytes, std::vector<std::string>& out,
                     std::string& err, const char* name) {
  if (v.isNull()) return true;
  if (!v.is<JsonArrayConst>()) {
    err = std::string(name) + " must be a list";
    return false;
  }
  std::vector<std::string> list;
  for (JsonVariantConst item : v.as<JsonArrayConst>()) {
    if (!item.is<const char*>()) {
      err = std::string(name) + " must contain only text";
      return false;
    }
    std::string s = item.as<const char*>();
    if (s.find_first_not_of(' ') == std::string::npos) continue;
    if (s.size() > maxBytes) {
      err = std::string(name) + ": an entry is longer than " + std::to_string(maxBytes) + " bytes";
      return false;
    }
    list.push_back(s);
  }
  if (list.size() > maxItems) {
    err = std::string(name) + " can hold at most " + std::to_string(maxItems) + " entries";
    return false;
  }
  out = list;
  return true;
}

// True for a URL remainder ("host[:port][/path]") whose host is on the local network:
// localhost, a .local name, or a private, loopback or link-local IPv4 address.
static bool isLocalHost(const std::string& rest) {
  std::string host = rest.substr(0, rest.find_first_of("/?#"));
  if (host.find('@') != std::string::npos) return false;  // user@host could hide the real host
  host = host.substr(0, host.find(':'));
  for (char& ch : host) ch = (char)tolower((unsigned char)ch);
  auto endsWith = [&](const char* s) {
    size_t n = strlen(s);
    return host.size() > n && host.compare(host.size() - n, n, s) == 0;
  };
  if (host == "localhost" || endsWith(".local")) return true;
  unsigned a, b, c, d;
  char tail;
  if (sscanf(host.c_str(), "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) != 4 || a > 255 || b > 255 || c > 255 || d > 255)
    return false;
  return a == 10 || a == 127 || (a == 192 && b == 168) || (a == 172 && b >= 16 && b <= 31) || (a == 169 && b == 254);
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
    for (uint8_t i = 0; i < DISPLAY_MODE_COUNT; i++) {
      if (m == MODE_NAMES[i]) {
        c.mode = (DisplayMode)i;
        found = true;
      }
    }
    if (!found) return "mode must be cycle, single, text, off, random, clock, date, mix, weather or countdown";
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
  if (!doc["label_logos"].isNull()) {
    if (!doc["label_logos"].is<bool>()) return "label_logos must be true/false";
    c.frame.labelLogos = doc["label_logos"];
  }
  if (!doc["celebrate"].isNull()) {
    if (!doc["celebrate"].is<bool>()) return "celebrate must be true/false";
    c.celebrate = doc["celebrate"];
  }
  if (!readInt(doc["random_seconds"], MIN_CYCLE_SECONDS, 3600, c.randomSeconds, err, "random_seconds")) return err;
  if (!readList(doc["words"], MAX_WORDS, MAX_WORD_BYTES, c.words, err, "words")) return err;
  if (!readList(doc["presets"], MAX_PRESETS, MAX_TEXT_BYTES, c.presets, err, "presets")) return err;
  if (!doc["mix"].isNull()) {
    if (!doc["mix"].is<JsonArrayConst>()) return "mix must be a list";
    std::vector<MixItem> mix;
    for (JsonVariantConst v : doc["mix"].as<JsonArrayConst>()) {
      MixItem item;
      std::string kind = v["kind"] | "";
      bool found = false;
      for (uint8_t i = 0; i < MIX_KIND_COUNT; i++) {
        if (kind == MIX_NAMES[i]) {
          item.kind = (MixKind)i;
          found = true;
        }
      }
      if (!found) return "mix kind must be followers, random, text, clock, date, weather or countdown";
      if (!readInt(v["seconds"], MIN_CYCLE_SECONDS, 3600, item.seconds, err, "mix seconds")) return err;
      mix.push_back(item);
    }
    if (mix.size() > MAX_MIX) return "mix can hold at most " + std::to_string(MAX_MIX) + " entries";
    c.mix = mix;
  }
  if (!doc["clock_24h"].isNull()) {
    if (!doc["clock_24h"].is<bool>()) return "clock_24h must be true/false";
    c.clock24h = doc["clock_24h"];
  }
  if (!doc["date_lang"].isNull()) {
    std::string l = doc["date_lang"] | "";
    if (l == "en") c.dateLang = DateLang::En;
    else if (l == "pt") c.dateLang = DateLang::Pt;
    else return "date_lang must be en or pt";
  }
  // Weather location: two numbers, or "" in both to switch the weather off.
  if (!doc["weather_lat"].isNull() || !doc["weather_lon"].isNull()) {
    JsonVariantConst la = doc["weather_lat"], lo = doc["weather_lon"];
    bool clearLat = la.is<const char*>() && !*la.as<const char*>();
    bool clearLon = lo.is<const char*>() && !*lo.as<const char*>();
    if (clearLat && clearLon) {
      c.weatherSet = false;
    } else {
      if (!la.is<double>() || !lo.is<double>()) return "weather_lat and weather_lon must both be numbers, or both empty";
      double lat = la.as<double>(), lon = lo.as<double>();
      if (lat < -90 || lat > 90 || lon < -180 || lon > 180) return "weather_lat must be -90..90 and weather_lon -180..180";
      c.weatherSet = true;
      c.weatherLat = lat;
      c.weatherLon = lon;
    }
  }
  if (!doc["weather_unit"].isNull()) {
    std::string u = doc["weather_unit"] | "";
    if (u == "c") c.weatherFahrenheit = false;
    else if (u == "f") c.weatherFahrenheit = true;
    else return "weather_unit must be c or f";
  }
  if (!doc["countdown_label"].isNull()) {
    std::string t = doc["countdown_label"] | "";
    if (t.size() > MAX_WORD_BYTES) return "countdown_label too long";
    c.countdownLabel = t;
  }
  if (!doc["countdown_done"].isNull()) {
    std::string t = doc["countdown_done"] | "";
    if (t.size() > MAX_TEXT_BYTES) return "countdown_done too long";
    c.countdownDone = t;
  }
  if (!doc["countdown_at"].isNull()) {
    std::string t = doc["countdown_at"] | "";
    CountdownTarget target;
    if (!t.empty() && !parseCountdownAt(t, target)) return "countdown_at must be YYYY-MM-DDTHH:MM or HH:MM";
    c.countdownAt = t;
  }
  if (!doc["remote_url"].isNull()) {
    std::string u = doc["remote_url"] | "";
    while (!u.empty() && u.back() == '/') u.pop_back();
    bool https = u.rfind("https://", 0) == 0, http = u.rfind("http://", 0) == 0;
    if (u.size() > 200 || (!u.empty() && !https && !http)) return "remote_url must start with https://";
    // The device token travels in every request, so plain http is only for a test server on the local network.
    if (http && !isLocalHost(u.substr(7))) return "remote_url must use https:// unless the server is on the local network";
    c.remoteUrl = u;
  }
  if (!doc["remote_token"].isNull()) {
    std::string t = doc["remote_token"] | "";
    if (t.size() > 128) return "remote_token is too long";
    c.remoteToken = t;
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

bool parseCountdownAt(const std::string& s, CountdownTarget& out) {
  unsigned y, mo, d, h, mi;
  char extra;
  CountdownTarget t;
  if (sscanf(s.c_str(), "%4u-%2u-%2uT%2u:%2u%c", &y, &mo, &d, &h, &mi, &extra) == 5 && s.size() == 16) {
    static const uint8_t DAYS[] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (y < 2000 || y > 2099 || mo < 1 || mo > 12 || d < 1 || d > DAYS[mo - 1] || h > 23 || mi > 59) return false;
    bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    if (mo == 2 && d == 29 && !leap) return false;
    t.daily = false;
    t.year = (uint16_t)y;
    t.month = (uint8_t)mo;
    t.day = (uint8_t)d;
  } else if (sscanf(s.c_str(), "%2u:%2u%c", &h, &mi, &extra) == 2 && s.size() == 5) {
    if (h > 23 || mi > 59) return false;
    t.daily = true;
  } else {
    return false;
  }
  t.hour = (uint8_t)h;
  t.minute = (uint8_t)mi;
  out = t;
  return true;
}

// Days from 1970-01-01 to a civil date (Howard Hinnant's algorithm).
static int64_t daysFromCivil(int64_t y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int64_t)doe - 719468;
}

int64_t secondsUntil(const CountdownTarget& t, uint16_t year, uint8_t month, uint8_t day, uint8_t hour,
                     uint8_t minute, uint8_t second) {
  int64_t now = daysFromCivil(year, month, day) * 86400 + hour * 3600 + minute * 60 + second;
  if (t.daily) {
    int64_t today = daysFromCivil(year, month, day) * 86400 + t.hour * 3600 + t.minute * 60;
    if (today <= now) today += 86400;  // already passed today: tomorrow's
    return today - now;
  }
  int64_t target = daysFromCivil(t.year, t.month, t.day) * 86400 + t.hour * 3600 + t.minute * 60;
  return target - now;
}

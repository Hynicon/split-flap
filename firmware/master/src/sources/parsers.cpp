#include "sources/parsers.h"

#include <ArduinoJson.h>

const char* fetchStatusName(FetchStatus s) {
  switch (s) {
    case FetchStatus::Ok: return "ok";
    case FetchStatus::AuthExpired: return "auth_expired";
    case FetchStatus::RateLimited: return "rate_limited";
    case FetchStatus::Error: return "error";
  }
  return "error";
}

std::string maskSecret(const std::string& secret) {
  if (secret.empty()) return "";
  if (secret.size() <= 8) return "****";
  return "****" + secret.substr(secret.size() - 4);
}

static std::string truncate(const char* s, size_t max = 80) {
  std::string out = s ? s : "";
  if (out.size() > max) out = out.substr(0, max) + "...";
  return out;
}

// ---------- Meta (Instagram + Facebook Graph) ----------
// Error shape: {"error":{"message":"...","type":"OAuthException","code":190,"error_subcode":463}}
static void applyMetaError(JsonObjectConst err, int httpCode, FetchStatus& status, std::string& error) {
  int code = err["code"] | 0;
  error = "meta error " + std::to_string(code) + ": " + truncate(err["message"] | "");
  if (code == 190 || code == 102 || code == 10 || (code >= 200 && code <= 299)) {
    status = FetchStatus::AuthExpired;  // 190 invalid/expired token, 102 session, 10/2xx permission
  } else if (code == 4 || code == 17 || code == 32 || code == 613 || code == 80001 || httpCode == 429) {
    status = FetchStatus::RateLimited;
  } else {
    status = FetchStatus::Error;
  }
}

static ParseResult parseMetaFollowers(int httpCode, const std::string& body) {
  ParseResult r;
  JsonDocument filter;
  filter["followers_count"] = true;
  filter["error"]["code"] = true;
  filter["error"]["message"] = true;
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, body, DeserializationOption::Filter(filter));
  if (e) {
    r.status = httpCode == 429 ? FetchStatus::RateLimited : FetchStatus::Error;
    r.error = "http " + std::to_string(httpCode) + ", bad json: " + e.c_str();
    return r;
  }
  if (!doc["error"].isNull()) {
    applyMetaError(doc["error"], httpCode, r.status, r.error);
    return r;
  }
  if (httpCode == 429) {
    r.status = FetchStatus::RateLimited;
    r.error = "http 429";
    return r;
  }
  if (httpCode != 200 || !doc["followers_count"].is<uint64_t>()) {
    r.error = "http " + std::to_string(httpCode) + ", no followers_count";
    return r;
  }
  r.status = FetchStatus::Ok;
  r.followers = doc["followers_count"].as<uint64_t>();
  return r;
}

ParseResult parseInstagram(int httpCode, const std::string& body) { return parseMetaFollowers(httpCode, body); }
ParseResult parseFacebook(int httpCode, const std::string& body) { return parseMetaFollowers(httpCode, body); }

TokenResult parseInstagramRefresh(int httpCode, const std::string& body) {
  TokenResult r;
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, body);
  if (e) {
    r.error = "http " + std::to_string(httpCode) + ", bad json: " + e.c_str();
    return r;
  }
  if (!doc["error"].isNull()) {
    applyMetaError(doc["error"], httpCode, r.status, r.error);
    return r;
  }
  const char* token = doc["access_token"] | "";
  if (httpCode != 200 || !*token) {
    r.error = "http " + std::to_string(httpCode) + ", no access_token";
    return r;
  }
  r.status = FetchStatus::Ok;
  r.accessToken = token;
  r.expiresIn = doc["expires_in"] | 0;
  return r;
}

// ---------- TikTok ----------
// Shape: {"data":{"user":{"follower_count":N}},"error":{"code":"ok","message":"","log_id":"..."}}
static FetchStatus tiktokStatus(const std::string& code, int httpCode) {
  if (code == "access_token_invalid" || code == "scope_not_authorized" || code == "scope_permission_missed" ||
      code == "token_expired" || httpCode == 401) {
    return FetchStatus::AuthExpired;
  }
  if (code == "rate_limit_exceeded" || httpCode == 429) return FetchStatus::RateLimited;
  return FetchStatus::Error;
}

ParseResult parseTikTok(int httpCode, const std::string& body) {
  ParseResult r;
  JsonDocument filter;
  filter["data"]["user"]["follower_count"] = true;
  filter["error"]["code"] = true;
  filter["error"]["message"] = true;
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, body, DeserializationOption::Filter(filter));
  if (e) {
    r.status = httpCode == 429 ? FetchStatus::RateLimited : FetchStatus::Error;
    r.error = "http " + std::to_string(httpCode) + ", bad json: " + e.c_str();
    return r;
  }
  std::string code = doc["error"]["code"] | "";
  if (code != "ok" || httpCode != 200) {
    r.status = tiktokStatus(code, httpCode);
    r.error = "tiktok " + (code.empty() ? "http " + std::to_string(httpCode) : code) + ": " +
              truncate(doc["error"]["message"] | "");
    return r;
  }
  JsonVariantConst count = doc["data"]["user"]["follower_count"];
  if (!count.is<uint64_t>()) {
    r.error = "no follower_count (is the user.info.stats scope granted?)";
    return r;
  }
  r.status = FetchStatus::Ok;
  r.followers = count.as<uint64_t>();
  return r;
}

// Success: {"access_token":..,"expires_in":86400,"refresh_token":..,"refresh_expires_in":31536000,...}
// Error:   {"error":"invalid_grant","error_description":"...","log_id":"..."}
TokenResult parseTikTokRefresh(int httpCode, const std::string& body) {
  TokenResult r;
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, body);
  if (e) {
    r.error = "http " + std::to_string(httpCode) + ", bad json: " + e.c_str();
    return r;
  }
  if (doc["error"].is<const char*>()) {
    std::string code = doc["error"].as<const char*>();
    r.error = "tiktok " + code + ": " + truncate(doc["error_description"] | "");
    // invalid_grant = refresh token expired or revoked: only a new login fixes it
    r.status = code == "invalid_grant" ? FetchStatus::AuthExpired
               : (code == "rate_limit_exceeded" || httpCode == 429) ? FetchStatus::RateLimited
                                                                    : FetchStatus::Error;
    return r;
  }
  const char* access = doc["access_token"] | "";
  const char* refresh = doc["refresh_token"] | "";
  if (httpCode != 200 || !*access || !*refresh) {
    r.error = "http " + std::to_string(httpCode) + ", missing tokens";
    return r;
  }
  r.status = FetchStatus::Ok;
  r.accessToken = access;
  r.refreshToken = refresh;
  r.expiresIn = doc["expires_in"] | 0;
  r.refreshExpiresIn = doc["refresh_expires_in"] | 0;
  return r;
}

// ---------- Open-Meteo ----------
// {"current":{"temperature_2m":18.3,"weather_code":3,...}}; errors: {"error":true,"reason":"..."}
WeatherResult parseOpenMeteo(int httpCode, const std::string& body) {
  WeatherResult r;
  JsonDocument filter;
  filter["current"]["temperature_2m"] = true;
  filter["current"]["weather_code"] = true;
  filter["error"] = true;
  filter["reason"] = true;
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, body, DeserializationOption::Filter(filter));
  if (e) {
    r.status = httpCode == 429 ? FetchStatus::RateLimited : FetchStatus::Error;
    r.error = "http " + std::to_string(httpCode) + ", bad json: " + e.c_str();
    return r;
  }
  if (doc["error"] | false) {
    r.status = httpCode == 429 ? FetchStatus::RateLimited : FetchStatus::Error;
    r.error = "open-meteo: " + truncate(doc["reason"] | "error");
    return r;
  }
  if (httpCode == 429) {
    r.status = FetchStatus::RateLimited;
    r.error = "http 429";
    return r;
  }
  JsonVariantConst t = doc["current"]["temperature_2m"];
  if (httpCode != 200 || !t.is<double>()) {
    r.error = "http " + std::to_string(httpCode) + ", no current temperature";
    return r;
  }
  double v = t.as<double>();
  r.tempTenths = (int32_t)(v >= 0 ? v * 10 + 0.5 : v * 10 - 0.5);
  r.code = doc["current"]["weather_code"].is<int>() ? doc["current"]["weather_code"].as<int>() : -1;
  r.status = FetchStatus::Ok;
  return r;
}

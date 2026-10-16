// Pure parsers for the platform API responses. No Arduino dependencies: unit-tested in env:native
// against test/fixtures/*.json. Never put tokens into error strings.
#pragma once

#include <stdint.h>

#include <string>

enum class FetchStatus : uint8_t {
  Ok,
  AuthExpired,  // token invalid/expired/revoked: user must re-authorize
  RateLimited,  // back off, retry later
  Error,        // anything else (network, malformed body, unexpected API error)
};

const char* fetchStatusName(FetchStatus s);

struct ParseResult {
  FetchStatus status = FetchStatus::Error;
  uint64_t followers = 0;
  std::string error;  // short human-readable reason (no tokens)
};

struct TokenResult {
  FetchStatus status = FetchStatus::Error;
  std::string accessToken;
  std::string refreshToken;     // TikTok only; always store the NEW one
  uint32_t expiresIn = 0;       // seconds
  uint32_t refreshExpiresIn = 0;
  std::string error;
};

// GET graph.instagram.com/<ver>/me?fields=username,followers_count
ParseResult parseInstagram(int httpCode, const std::string& body);
// GET graph.facebook.com/<ver>/<PAGE_ID>?fields=name,followers_count
ParseResult parseFacebook(int httpCode, const std::string& body);
// GET open.tiktokapis.com/v2/user/info/?fields=display_name,follower_count
ParseResult parseTikTok(int httpCode, const std::string& body);

// GET graph.instagram.com/refresh_access_token?grant_type=ig_refresh_token
TokenResult parseInstagramRefresh(int httpCode, const std::string& body);
// POST open.tiktokapis.com/v2/oauth/token/ grant_type=refresh_token
TokenResult parseTikTokRefresh(int httpCode, const std::string& body);

// Keeps only the last 4 characters, e.g. "…a1b2". Use for every log/web display of a secret.
std::string maskSecret(const std::string& secret);

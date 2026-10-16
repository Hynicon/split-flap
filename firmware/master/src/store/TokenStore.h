// Per-platform OAuth tokens in NVS (Preferences). Tokens are never printed unmasked.
#pragma once

#include <Arduino.h>

#include "display/layout.h"

struct PlatformTokens {
  String accessToken;
  String refreshToken;      // TikTok
  String pageId;            // Facebook
  String clientKey;         // TikTok (needed for refresh)
  String clientSecret;      // TikTok (needed for refresh)
  uint64_t issuedAt = 0;          // epoch s, access token
  uint64_t expiresAt = 0;         // epoch s, access token (0 = never / unknown)
  uint64_t refreshExpiresAt = 0;  // epoch s, TikTok refresh token

  bool present() const { return accessToken.length() > 0; }
};

class TokenStore {
 public:
  PlatformTokens load(Platform p);
  // Writes the refresh token before the access token, so a power cut mid-save never leaves
  // a new access token paired with an already-consumed refresh token.
  void save(Platform p, const PlatformTokens& t);
  void clear(Platform p);
};

// Follower-count sources. One per platform; ManualSource stands in when an API isn't available
// (e.g. TikTok app not approved). Official APIs only, no scraping.
#pragma once

#include <Arduino.h>

#include "display/layout.h"
#include "sources/parsers.h"
#include "store/TokenStore.h"

// VERIFIED 2026-10-16: Graph API v26.0 is current (released 2026-07-29). Bump when Meta deprecates it.
#define META_GRAPH_VERSION "v26.0"

struct FetchResult {
  FetchStatus status = FetchStatus::Error;
  uint64_t followers = 0;
  String error;
  uint32_t retryAfterSec = 0;

  bool ok() const { return status == FetchStatus::Ok; }
  bool authExpired() const { return status == FetchStatus::AuthExpired; }
};

class Source {
 public:
  explicit Source(Platform p) : platform_(p) {}
  virtual ~Source() = default;

  Platform platform() const { return platform_; }
  const char* id() const { return platformId(platform_); }

  virtual bool configured() = 0;  // has what it needs to fetch
  virtual FetchResult fetch() = 0;
  // Refresh tokens if due (`now` = epoch s, 0 if NTP not synced). Ok when nothing to do.
  virtual FetchResult maintainToken(uint64_t now) {
    (void)now;
    FetchResult r;
    r.status = FetchStatus::Ok;
    return r;
  }

 protected:
  Platform platform_;
};

class InstagramSource : public Source {
 public:
  explicit InstagramSource(TokenStore& store) : Source(Platform::Instagram), store_(store) {}
  bool configured() override;
  FetchResult fetch() override;
  FetchResult maintainToken(uint64_t now) override;

 private:
  TokenStore& store_;
};

class FacebookSource : public Source {
 public:
  explicit FacebookSource(TokenStore& store) : Source(Platform::Facebook), store_(store) {}
  bool configured() override;
  FetchResult fetch() override;  // Page token from a long-lived user token: no expiry, no refresh

 private:
  TokenStore& store_;
};

class TikTokSource : public Source {
 public:
  explicit TikTokSource(TokenStore& store) : Source(Platform::TikTok), store_(store) {}
  bool configured() override;
  FetchResult fetch() override;
  FetchResult maintainToken(uint64_t now) override;

 private:
  TokenStore& store_;
};

// Count typed into the web UI.
class ManualSource : public Source {
 public:
  ManualSource(Platform p, const uint64_t& count) : Source(p), count_(count) {}
  bool configured() override { return true; }
  FetchResult fetch() override {
    FetchResult r;
    r.status = FetchStatus::Ok;
    r.followers = count_;
    return r;
  }

 private:
  const uint64_t& count_;
};

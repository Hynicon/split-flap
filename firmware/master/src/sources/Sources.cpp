#include "logic/policy.h"
#include "sources/Https.h"
#include "sources/Source.h"

static FetchResult toFetchResult(const ParseResult& p, const HttpResponse& http) {
  FetchResult r;
  r.status = p.status;
  r.followers = p.followers;
  r.error = http.code < 0 ? String("network error ") + http.code : String(p.error.c_str());
  if (http.code < 0) r.status = FetchStatus::Error;
  r.retryAfterSec = http.retryAfterSec;
  return r;
}

static FetchResult missing(const char* what) {
  FetchResult r;
  r.status = FetchStatus::AuthExpired;
  r.error = String("no ") + what + " - run tools/auth or paste it in the web UI";
  return r;
}

// ---------- Instagram (Instagram API with Instagram Login) ----------
bool InstagramSource::configured() { return store_.load(platform_).present(); }

FetchResult InstagramSource::fetch() {
  PlatformTokens t = store_.load(platform_);
  if (!t.present()) return missing("Instagram token");
  HttpResponse http = httpsGet(String("https://graph.instagram.com/" META_GRAPH_VERSION
                                      "/me?fields=username,followers_count&access_token=") +
                               urlEncode(t.accessToken));
  return toFetchResult(parseInstagram(http.code, http.body), http);
}

FetchResult InstagramSource::maintainToken(uint64_t now) {
  FetchResult r;
  r.status = FetchStatus::Ok;
  PlatformTokens t = store_.load(platform_);
  if (!t.present() || !instagramNeedsRefresh(now, t.issuedAt, t.expiresAt)) return r;

  Serial.printf("[instagram] refreshing long-lived token %s\n", maskSecret(t.accessToken.c_str()).c_str());
  HttpResponse http = httpsGet("https://graph.instagram.com/refresh_access_token?grant_type=ig_refresh_token&access_token=" +
                               urlEncode(t.accessToken));
  TokenResult tr = parseInstagramRefresh(http.code, http.body);
  if (tr.status != FetchStatus::Ok) {
    r.status = http.code < 0 ? FetchStatus::Error : tr.status;
    r.error = http.code < 0 ? String("network error ") + http.code : String(tr.error.c_str());
    return r;
  }
  t.accessToken = tr.accessToken.c_str();
  t.issuedAt = now;
  t.expiresAt = tr.expiresIn ? now + tr.expiresIn : now + 60ull * DAY_S;
  store_.save(platform_, t);
  Serial.printf("[instagram] token refreshed, new %s\n", maskSecret(tr.accessToken).c_str());
  return r;
}

// ---------- Facebook Page ----------
bool FacebookSource::configured() {
  PlatformTokens t = store_.load(platform_);
  return t.present() && t.pageId.length();
}

FetchResult FacebookSource::fetch() {
  PlatformTokens t = store_.load(platform_);
  if (!t.present() || !t.pageId.length()) return missing("Facebook page token / page id");
  HttpResponse http = httpsGet(String("https://graph.facebook.com/" META_GRAPH_VERSION "/") + urlEncode(t.pageId) +
                               "?fields=name,followers_count&access_token=" + urlEncode(t.accessToken));
  return toFetchResult(parseFacebook(http.code, http.body), http);
}

// ---------- TikTok (Login Kit, user.info.stats) ----------
bool TikTokSource::configured() {
  PlatformTokens t = store_.load(platform_);
  return t.present() || t.refreshToken.length();
}

FetchResult TikTokSource::fetch() {
  PlatformTokens t = store_.load(platform_);
  if (!t.present()) return missing("TikTok token");
  HttpResponse http = httpsGet("https://open.tiktokapis.com/v2/user/info/?fields=display_name,follower_count", t.accessToken);
  return toFetchResult(parseTikTok(http.code, http.body), http);
}

FetchResult TikTokSource::maintainToken(uint64_t now) {
  FetchResult r;
  r.status = FetchStatus::Ok;
  PlatformTokens t = store_.load(platform_);
  if (!t.refreshToken.length() || !tiktokNeedsRefresh(now, t.expiresAt)) return r;
  if (!t.clientKey.length() || !t.clientSecret.length()) {
    r.status = FetchStatus::AuthExpired;
    r.error = "TikTok client key/secret missing on device - re-run tools/auth tiktok";
    return r;
  }
  if (t.refreshExpiresAt && now >= t.refreshExpiresAt) {
    r.status = FetchStatus::AuthExpired;
    r.error = "TikTok refresh token expired - re-run tools/auth tiktok";
    return r;
  }

  Serial.printf("[tiktok] refreshing access token (refresh %s)\n", maskSecret(t.refreshToken.c_str()).c_str());
  String form = "client_key=" + urlEncode(t.clientKey) + "&client_secret=" + urlEncode(t.clientSecret) +
                "&grant_type=refresh_token&refresh_token=" + urlEncode(t.refreshToken);
  HttpResponse http = httpsPostForm("https://open.tiktokapis.com/v2/oauth/token/", form);
  TokenResult tr = parseTikTokRefresh(http.code, http.body);
  if (tr.status != FetchStatus::Ok) {
    r.status = http.code < 0 ? FetchStatus::Error : tr.status;
    r.error = http.code < 0 ? String("network error ") + http.code : String(tr.error.c_str());
    return r;
  }
  // TikTok may rotate the refresh token: always persist the new one.
  t.refreshToken = tr.refreshToken.c_str();
  t.refreshExpiresAt = tr.refreshExpiresIn ? now + tr.refreshExpiresIn : t.refreshExpiresAt;
  t.accessToken = tr.accessToken.c_str();
  t.issuedAt = now;
  t.expiresAt = now + (tr.expiresIn ? tr.expiresIn : DAY_S);
  store_.save(platform_, t);
  Serial.printf("[tiktok] token refreshed, access %s\n", maskSecret(tr.accessToken).c_str());
  return r;
}

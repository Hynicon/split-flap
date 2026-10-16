#include "store/TokenStore.h"

#include <Preferences.h>

static const char* nsFor(Platform p) {
  switch (p) {
    case Platform::TikTok: return "tok_tt";
    case Platform::Instagram: return "tok_ig";
    case Platform::Facebook: return "tok_fb";
  }
  return "tok_x";
}

PlatformTokens TokenStore::load(Platform p) {
  PlatformTokens t;
  Preferences prefs;
  if (!prefs.begin(nsFor(p), true)) return t;  // namespace doesn't exist yet
  t.refreshToken = prefs.getString("rt", "");
  t.accessToken = prefs.getString("at", "");
  t.pageId = prefs.getString("pid", "");
  t.clientKey = prefs.getString("ck", "");
  t.clientSecret = prefs.getString("cs", "");
  t.issuedAt = prefs.getULong64("iss", 0);
  t.expiresAt = prefs.getULong64("exp", 0);
  t.refreshExpiresAt = prefs.getULong64("rexp", 0);
  prefs.end();
  return t;
}

void TokenStore::save(Platform p, const PlatformTokens& t) {
  Preferences prefs;
  prefs.begin(nsFor(p), false);
  prefs.putString("rt", t.refreshToken);
  prefs.putULong64("rexp", t.refreshExpiresAt);
  prefs.putString("at", t.accessToken);
  prefs.putULong64("iss", t.issuedAt);
  prefs.putULong64("exp", t.expiresAt);
  prefs.putString("pid", t.pageId);
  prefs.putString("ck", t.clientKey);
  prefs.putString("cs", t.clientSecret);
  prefs.end();
}

void TokenStore::clear(Platform p) {
  Preferences prefs;
  prefs.begin(nsFor(p), false);
  prefs.clear();
  prefs.end();
}

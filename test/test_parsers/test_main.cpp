#include <unity.h>

#include <fstream>
#include <sstream>

#include "logic/policy.h"
#include "sources/parsers.h"

void setUp() {}
void tearDown() {}

// pio test runs from the project root
static std::string fixture(const char* name) {
  std::ifstream f(std::string("test/fixtures/") + name, std::ios::binary);
  TEST_ASSERT_TRUE_MESSAGE(f.good(), name);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

#define ASSERT_STATUS(expected, actual) TEST_ASSERT_EQUAL_STRING(fetchStatusName(expected), fetchStatusName(actual))

// ---- Instagram ----
void test_ig_success() {
  ParseResult r = parseInstagram(200, fixture("ig_success.json"));
  ASSERT_STATUS(FetchStatus::Ok, r.status);
  TEST_ASSERT_EQUAL_UINT64(48213, r.followers);
}
void test_ig_expired() {
  ParseResult r = parseInstagram(400, fixture("ig_expired.json"));
  ASSERT_STATUS(FetchStatus::AuthExpired, r.status);
  TEST_ASSERT_TRUE(r.error.find("190") != std::string::npos);
}
void test_ig_rate_limit() {
  ASSERT_STATUS(FetchStatus::RateLimited, parseInstagram(403, fixture("meta_rate_limit.json")).status);
  ASSERT_STATUS(FetchStatus::RateLimited, parseInstagram(429, "").status);
}
void test_ig_malformed() {
  ParseResult r = parseInstagram(200, fixture("malformed.json"));
  ASSERT_STATUS(FetchStatus::Error, r.status);
  ASSERT_STATUS(FetchStatus::Error, parseInstagram(500, "<html>oops</html>").status);
  ASSERT_STATUS(FetchStatus::Error, parseInstagram(200, "{}").status);
  ASSERT_STATUS(FetchStatus::Error, parseInstagram(200, "{\"followers_count\":\"12\"}").status);
}
void test_ig_refresh() {
  TokenResult r = parseInstagramRefresh(200, fixture("ig_refresh_success.json"));
  ASSERT_STATUS(FetchStatus::Ok, r.status);
  TEST_ASSERT_EQUAL_STRING("IGAAnewtoken1234", r.accessToken.c_str());
  TEST_ASSERT_EQUAL_UINT32(5183944, r.expiresIn);
  ASSERT_STATUS(FetchStatus::AuthExpired, parseInstagramRefresh(400, fixture("ig_expired.json")).status);
  ASSERT_STATUS(FetchStatus::Error, parseInstagramRefresh(200, fixture("malformed.json")).status);
}

// ---- Facebook ----
void test_fb_success() {
  ParseResult r = parseFacebook(200, fixture("fb_success.json"));
  ASSERT_STATUS(FetchStatus::Ok, r.status);
  TEST_ASSERT_EQUAL_UINT64(1250345, r.followers);
}
void test_fb_expired() { ASSERT_STATUS(FetchStatus::AuthExpired, parseFacebook(400, fixture("fb_expired.json")).status); }
void test_fb_rate_limit() { ASSERT_STATUS(FetchStatus::RateLimited, parseFacebook(400, fixture("meta_rate_limit.json")).status); }
void test_fb_malformed() { ASSERT_STATUS(FetchStatus::Error, parseFacebook(200, fixture("malformed.json")).status); }

// ---- TikTok ----
void test_tt_success() {
  ParseResult r = parseTikTok(200, fixture("tt_success.json"));
  ASSERT_STATUS(FetchStatus::Ok, r.status);
  TEST_ASSERT_EQUAL_UINT64(10234567, r.followers);
}
void test_tt_expired() {
  ParseResult r = parseTikTok(401, fixture("tt_expired.json"));
  ASSERT_STATUS(FetchStatus::AuthExpired, r.status);
  TEST_ASSERT_TRUE(r.error.find("access_token_invalid") != std::string::npos);
}
void test_tt_rate_limit() { ASSERT_STATUS(FetchStatus::RateLimited, parseTikTok(429, fixture("tt_rate_limit.json")).status); }
void test_tt_missing_scope() {
  ParseResult r = parseTikTok(200, fixture("tt_missing_scope.json"));
  ASSERT_STATUS(FetchStatus::Error, r.status);
  TEST_ASSERT_TRUE(r.error.find("user.info.stats") != std::string::npos);
}
void test_tt_malformed() { ASSERT_STATUS(FetchStatus::Error, parseTikTok(200, fixture("malformed.json")).status); }
void test_tt_refresh() {
  TokenResult r = parseTikTokRefresh(200, fixture("tt_refresh_success.json"));
  ASSERT_STATUS(FetchStatus::Ok, r.status);
  TEST_ASSERT_EQUAL_STRING("act.newaccess5678", r.accessToken.c_str());
  TEST_ASSERT_EQUAL_STRING("rft.newrefresh9012", r.refreshToken.c_str());
  TEST_ASSERT_EQUAL_UINT32(86400, r.expiresIn);
  TEST_ASSERT_EQUAL_UINT32(31536000, r.refreshExpiresIn);
  TokenResult bad = parseTikTokRefresh(400, fixture("tt_refresh_invalid.json"));
  ASSERT_STATUS(FetchStatus::AuthExpired, bad.status);
  ASSERT_STATUS(FetchStatus::Error, parseTikTokRefresh(200, fixture("malformed.json")).status);
}

// ---- secrets never leak ----
void test_mask() {
  TEST_ASSERT_EQUAL_STRING("", maskSecret("").c_str());
  TEST_ASSERT_EQUAL_STRING("****", maskSecret("short").c_str());
  TEST_ASSERT_EQUAL_STRING("****9012", maskSecret("rft.newrefresh9012").c_str());
}
void test_errors_have_no_tokens() {
  TEST_ASSERT_TRUE(parseTikTokRefresh(400, fixture("tt_refresh_invalid.json")).error.find("rft.") == std::string::npos);
}

// ---- policy ----
void test_backoff() {
  TEST_ASSERT_EQUAL_UINT32(15, backoffMinutes(0, 15));
  TEST_ASSERT_EQUAL_UINT32(5, backoffMinutes(0, 5));
  TEST_ASSERT_EQUAL_UINT32(15, backoffMinutes(1, 15));
  TEST_ASSERT_EQUAL_UINT32(30, backoffMinutes(2, 15));
  TEST_ASSERT_EQUAL_UINT32(60, backoffMinutes(3, 15));
  TEST_ASSERT_EQUAL_UINT32(120, backoffMinutes(4, 15));
  TEST_ASSERT_EQUAL_UINT32(120, backoffMinutes(200, 15));
  TEST_ASSERT_EQUAL_UINT32(61, backoffMinutes(1, 15, 3601));  // Retry-After wins
}
void test_instagram_refresh_window() {
  const uint64_t issued = 1700000000;
  const uint64_t expires = issued + 60 * DAY_S;
  TEST_ASSERT_FALSE(instagramNeedsRefresh(0, issued, expires));  // no NTP yet
  TEST_ASSERT_FALSE(instagramNeedsRefresh(issued + 29 * DAY_S, issued, expires));
  TEST_ASSERT_TRUE(instagramNeedsRefresh(issued + 30 * DAY_S, issued, expires));
  TEST_ASSERT_FALSE(instagramNeedsRefresh(expires + 1, issued, expires));  // expired: re-login needed
}
void test_tiktok_refresh_window() {
  const uint64_t exp = 1700000000;
  TEST_ASSERT_FALSE(tiktokNeedsRefresh(exp - 3 * HOUR_S, exp));
  TEST_ASSERT_TRUE(tiktokNeedsRefresh(exp - 2 * HOUR_S, exp));
  TEST_ASSERT_TRUE(tiktokNeedsRefresh(exp + 10, exp));
  TEST_ASSERT_TRUE(tiktokNeedsRefresh(exp, 0));
  TEST_ASSERT_FALSE(tiktokNeedsRefresh(0, exp));
}
void test_next_platform() {
  TEST_ASSERT_EQUAL_INT(1, nextPlatform(0, 0b111, 3));
  TEST_ASSERT_EQUAL_INT(0, nextPlatform(2, 0b111, 3));
  TEST_ASSERT_EQUAL_INT(2, nextPlatform(0, 0b100, 3));
  TEST_ASSERT_EQUAL_INT(2, nextPlatform(2, 0b100, 3));  // only one enabled: stays
  TEST_ASSERT_EQUAL_INT(0, nextPlatform(-1, 0b101, 3));  // first pick
  TEST_ASSERT_EQUAL_INT(-1, nextPlatform(0, 0, 3));
}
void test_quiet_hours() {
  TEST_ASSERT_FALSE(inQuietHours(600, 0, 0));
  TEST_ASSERT_TRUE(inQuietHours(23 * 60, 22 * 60, 7 * 60));
  TEST_ASSERT_TRUE(inQuietHours(3 * 60, 22 * 60, 7 * 60));
  TEST_ASSERT_FALSE(inQuietHours(7 * 60, 22 * 60, 7 * 60));
  TEST_ASSERT_FALSE(inQuietHours(12 * 60, 22 * 60, 7 * 60));
  TEST_ASSERT_TRUE(inQuietHours(13 * 60, 12 * 60, 14 * 60));
}
void test_milestone() {
  TEST_ASSERT_TRUE(crossedMilestone(999, 1000, 1000));
  TEST_ASSERT_TRUE(crossedMilestone(1999, 3005, 1000));
  TEST_ASSERT_FALSE(crossedMilestone(1000, 1999, 1000));
  TEST_ASSERT_FALSE(crossedMilestone(1001, 999, 1000));  // going down
  TEST_ASSERT_FALSE(crossedMilestone(0, 5000, 1000));    // first value after boot
  TEST_ASSERT_FALSE(crossedMilestone(999, 1000, 0));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_ig_success);
  RUN_TEST(test_ig_expired);
  RUN_TEST(test_ig_rate_limit);
  RUN_TEST(test_ig_malformed);
  RUN_TEST(test_ig_refresh);
  RUN_TEST(test_fb_success);
  RUN_TEST(test_fb_expired);
  RUN_TEST(test_fb_rate_limit);
  RUN_TEST(test_fb_malformed);
  RUN_TEST(test_tt_success);
  RUN_TEST(test_tt_expired);
  RUN_TEST(test_tt_rate_limit);
  RUN_TEST(test_tt_missing_scope);
  RUN_TEST(test_tt_malformed);
  RUN_TEST(test_tt_refresh);
  RUN_TEST(test_mask);
  RUN_TEST(test_errors_have_no_tokens);
  RUN_TEST(test_backoff);
  RUN_TEST(test_instagram_refresh_window);
  RUN_TEST(test_tiktok_refresh_window);
  RUN_TEST(test_next_platform);
  RUN_TEST(test_quiet_hours);
  RUN_TEST(test_milestone);
  return UNITY_END();
}

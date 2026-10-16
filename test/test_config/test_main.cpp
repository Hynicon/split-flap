#include <unity.h>

#include "logic/config.h"

void setUp() {}
void tearDown() {}

void test_defaults_round_trip() {
  Config a;
  a.adminKey = "secret-key-123";
  Config b;
  TEST_ASSERT_EQUAL_STRING("", applyConfigJson(b, configToJson(a, true)).c_str());
  TEST_ASSERT_EQUAL_STRING(configToJson(a, true).c_str(), configToJson(b, true).c_str());
}

void test_public_json_hides_admin_key() {
  Config c;
  c.adminKey = "secret-key-123";
  std::string pub = configToJson(c, false);
  TEST_ASSERT_TRUE(pub.find("secret-key-123") == std::string::npos);
  TEST_ASSERT_TRUE(pub.find("\"admin_key_set\":true") != std::string::npos);
}

void test_partial_update() {
  Config c;
  TEST_ASSERT_EQUAL_STRING("", applyConfigJson(c, R"({"platforms":{"tiktok":{"source":"manual","manual_count":4200},"facebook":{"enabled":false}},"cycle_seconds":30,"mode":"single:tiktok","quiet_start":"22:30","quiet_end":"07:00","text":"Olá"})").c_str());
  TEST_ASSERT_EQUAL_UINT8(0b011, c.enabledMask);
  TEST_ASSERT_TRUE(c.source[0] == SourceKind::Manual);
  TEST_ASSERT_EQUAL_UINT64(4200, c.manualCount[0]);
  TEST_ASSERT_EQUAL_UINT16(30, c.cycleSeconds);
  TEST_ASSERT_TRUE(c.mode == DisplayMode::Single);
  TEST_ASSERT_TRUE(c.single == Platform::TikTok);
  TEST_ASSERT_EQUAL_UINT16(22 * 60 + 30, c.quietStart);
  TEST_ASSERT_EQUAL_UINT16(7 * 60, c.quietEnd);
  TEST_ASSERT_EQUAL_STRING("Olá", c.text.c_str());
  TEST_ASSERT_EQUAL_UINT16(15, c.pollMinutes);  // untouched
}

void test_invalid_rejected_atomically() {
  Config c;
  const char* bad[] = {
      R"({"cycle_seconds":10})",            // below the 20 s minimum
      R"({"poll_minutes":1})",
      R"({"speed":13})",
      R"({"units":0})",
      R"({"label_width":3})",
      R"({"units":2,"label_width":2})",
      R"({"mode":"party"})",
      R"({"mode":"single:myspace"})",
      R"({"quiet_start":"25:00"})",
      R"({"quiet_end":"7am"})",
      R"({"platforms":{"myspace":{"enabled":true}}})",
      R"({"platforms":{"tiktok":{"source":"scrape"}}})",
      R"({"platforms":{"tiktok":{"manual_count":-5}}})",
      R"({"admin_key":"short"})",
      R"({"text":"0123456789012345678901234567890123456789012345678901234567890123456789"})",
      R"([1,2])",
      "not json",
  };
  for (const char* json : bad) {
    std::string before = configToJson(c, true);
    TEST_ASSERT_FALSE_MESSAGE(applyConfigJson(c, json).empty(), json);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(before.c_str(), configToJson(c, true).c_str(), json);
  }
  // a valid field next to an invalid one must not be applied either
  TEST_ASSERT_FALSE(applyConfigJson(c, R"({"speed":7,"cycle_seconds":10})").empty());
  TEST_ASSERT_EQUAL_UINT8(10, c.speedRpm);
}

void test_token_update() {
  TokenUpdate t;
  TEST_ASSERT_EQUAL_STRING("", parseTokenUpdate(R"({"platform":"tiktok","access_token":"a","refresh_token":"r","client_key":"k","client_secret":"s","expires_at":1700086400,"refresh_expires_at":1731536000,"issued_at":1700000000})", t).c_str());
  TEST_ASSERT_TRUE(t.platform == Platform::TikTok);
  TEST_ASSERT_EQUAL_STRING("r", t.refreshToken.c_str());
  TEST_ASSERT_EQUAL_UINT64(1700086400, t.expiresAt);
  TEST_ASSERT_EQUAL_UINT64(1731536000, t.refreshExpiresAt);

  TEST_ASSERT_EQUAL_STRING("", parseTokenUpdate(R"({"platform":"facebook","clear":true})", t).c_str());
  TEST_ASSERT_TRUE(t.clear);

  TEST_ASSERT_FALSE(parseTokenUpdate(R"({"platform":"facebook","access_token":"a"})", t).empty());  // no page_id
  TEST_ASSERT_FALSE(parseTokenUpdate(R"({"platform":"tiktok","refresh_token":"r"})", t).empty());  // no client creds
  TEST_ASSERT_FALSE(parseTokenUpdate(R"({"platform":"instagram"})", t).empty());
  TEST_ASSERT_FALSE(parseTokenUpdate(R"({"platform":"x","access_token":"a"})", t).empty());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_defaults_round_trip);
  RUN_TEST(test_public_json_hides_admin_key);
  RUN_TEST(test_partial_update);
  RUN_TEST(test_invalid_rejected_atomically);
  RUN_TEST(test_token_update);
  return UNITY_END();
}

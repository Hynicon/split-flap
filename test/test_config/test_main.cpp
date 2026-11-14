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
  TEST_ASSERT_EQUAL_STRING("", applyConfigJson(c, R"({"platforms":{"tiktok":{"source":"manual","manual_count":4200},"facebook":{"enabled":false}},"cycle_seconds":30,"mode":"single:tiktok","quiet_start":"22:30","quiet_end":"07:00","text":"Olá","label_logos":false})").c_str());
  TEST_ASSERT_EQUAL_UINT8(0b011, c.enabledMask);
  TEST_ASSERT_TRUE(c.source[0] == SourceKind::Manual);
  TEST_ASSERT_EQUAL_UINT64(4200, c.manualCount[0]);
  TEST_ASSERT_EQUAL_UINT16(30, c.cycleSeconds);
  TEST_ASSERT_TRUE(c.mode == DisplayMode::Single);
  TEST_ASSERT_TRUE(c.single == Platform::TikTok);
  TEST_ASSERT_EQUAL_UINT16(22 * 60 + 30, c.quietStart);
  TEST_ASSERT_EQUAL_UINT16(7 * 60, c.quietEnd);
  TEST_ASSERT_EQUAL_STRING("Olá", c.text.c_str());
  TEST_ASSERT_FALSE(c.frame.labelLogos);
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
      R"({"label_logos":"yes"})",
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
      R"({"random_seconds":5})",
      R"({"words":"TACOS"})",
      R"({"words":[1,2]})",
      R"({"words":["0123456789012345678901234567890123"]})",  // 34 bytes
      R"({"mix":[{"kind":"disco","seconds":30}]})",
      R"({"mix":[{"kind":"clock","seconds":5}]})",
      R"({"mix":{"kind":"clock"}})",
      R"({"mix":[{"kind":"clock"},{"kind":"clock"},{"kind":"clock"},{"kind":"clock"},{"kind":"clock"},{"kind":"clock"},{"kind":"clock"},{"kind":"clock"},{"kind":"clock"}]})",
      R"({"clock_24h":"yes"})",
      R"({"date_lang":"fr"})",
      R"({"remote_url":"flap.example.com"})",
      R"({"remote_url":"ftp://flap.example.com"})",
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

void test_words_mix_and_formats() {
  Config c;
  TEST_ASSERT_EQUAL_STRING("", applyConfigJson(c, R"({"mode":"mix","random_seconds":45,"words":["Tacos","  ","Olá","Burrito"],"presets":["OPEN","SOLD OUT"],"mix":[{"kind":"followers","seconds":30},{"kind":"random"},{"kind":"date","seconds":20}],"clock_24h":false,"date_lang":"pt"})").c_str());
  TEST_ASSERT_TRUE(c.mode == DisplayMode::Mix);
  TEST_ASSERT_EQUAL_UINT16(45, c.randomSeconds);
  TEST_ASSERT_EQUAL_UINT(3, c.words.size());  // the blank entry is dropped
  TEST_ASSERT_EQUAL_STRING("Olá", c.words[1].c_str());
  TEST_ASSERT_EQUAL_UINT(2, c.presets.size());
  TEST_ASSERT_EQUAL_UINT(3, c.mix.size());
  TEST_ASSERT_TRUE(c.mix[0].kind == MixKind::Followers && c.mix[0].seconds == 30);
  TEST_ASSERT_TRUE(c.mix[1].kind == MixKind::Random && c.mix[1].seconds == 60);  // default time
  TEST_ASSERT_TRUE(c.mix[2].kind == MixKind::Date);
  TEST_ASSERT_FALSE(c.clock24h);
  TEST_ASSERT_TRUE(c.dateLang == DateLang::Pt);

  // everything survives a round trip
  c.adminKey = "secret-key-123";
  Config d;
  TEST_ASSERT_EQUAL_STRING("", applyConfigJson(d, configToJson(c, true)).c_str());
  TEST_ASSERT_EQUAL_STRING(configToJson(c, true).c_str(), configToJson(d, true).c_str());

  // the lists can be stored apart from the rest and put back
  std::string core = configToJson(c, true, false);
  TEST_ASSERT_TRUE(core.find("Tacos") == std::string::npos && core.find("SOLD OUT") == std::string::npos);
  Config e;
  TEST_ASSERT_EQUAL_STRING("", applyConfigJson(e, core).c_str());
  TEST_ASSERT_EQUAL_STRING("", applyConfigJson(e, wordsToJson(c)).c_str());
  TEST_ASSERT_EQUAL_STRING("", applyConfigJson(e, presetsToJson(c)).c_str());
  TEST_ASSERT_EQUAL_STRING(configToJson(c, true).c_str(), configToJson(e, true).c_str());

  // an update that leaves the lists out keeps them; an empty list clears
  TEST_ASSERT_EQUAL_STRING("", applyConfigJson(c, R"({"mode":"random"})").c_str());
  TEST_ASSERT_EQUAL_UINT(3, c.words.size());
  TEST_ASSERT_EQUAL_STRING("", applyConfigJson(c, R"({"words":[]})").c_str());
  TEST_ASSERT_EQUAL_UINT(0, c.words.size());
}

void test_word_list_limits() {
  std::string json = R"({"words":[)";
  for (int i = 0; i < 100; i++) json += std::string(i ? "," : "") + "\"W" + std::to_string(i) + "\"";
  Config c;
  TEST_ASSERT_EQUAL_STRING("", applyConfigJson(c, json + "]}").c_str());
  TEST_ASSERT_EQUAL_UINT(100, c.words.size());
  TEST_ASSERT_FALSE(applyConfigJson(c, json + ",\"ONE MORE\"]}").empty());
  TEST_ASSERT_EQUAL_UINT(100, c.words.size());
  TEST_ASSERT_EQUAL_STRING("", applyConfigJson(c, R"({"words":["01234567890123456789012345678901"]})").c_str());  // exactly 32 bytes
}

void test_remote_settings() {
  Config c;
  TEST_ASSERT_EQUAL_STRING("", applyConfigJson(c, R"({"remote_url":"https://flap.example.com/","remote_token":"device-token-1"})").c_str());
  TEST_ASSERT_EQUAL_STRING("https://flap.example.com", c.remoteUrl.c_str());  // trailing slash removed
  std::string pub = configToJson(c, false);
  TEST_ASSERT_TRUE(pub.find("device-token-1") == std::string::npos);
  TEST_ASSERT_TRUE(pub.find("\"remote_token_set\":true") != std::string::npos);
  TEST_ASSERT_TRUE(configToJson(c, true).find("device-token-1") != std::string::npos);
  TEST_ASSERT_EQUAL_STRING("", applyConfigJson(c, R"({"remote_url":""})").c_str());  // empty switches the relay off
  TEST_ASSERT_EQUAL_STRING("", c.remoteUrl.c_str());
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
  RUN_TEST(test_words_mix_and_formats);
  RUN_TEST(test_word_list_limits);
  RUN_TEST(test_remote_settings);
  RUN_TEST(test_token_update);
  return UNITY_END();
}

#include <unity.h>

#include <algorithm>
#include <set>
#include <string>

#include "logic/rotation.h"

void setUp() {}
void tearDown() {}

static uint32_t seed = 12345;
static uint32_t rng() {
  seed = seed * 1664525u + 1013904223u;
  return seed >> 8;
}

static Config withWords(std::initializer_list<const char*> words) {
  Config c;
  c.mode = DisplayMode::Random;
  c.randomSeconds = 30;
  for (const char* w : words) c.words.push_back(w);
  return c;
}

void test_simple_modes() {
  Rotation r(rng);
  Config c;
  c.mode = DisplayMode::Off;
  TEST_ASSERT_TRUE(r.update(c, 0).kind == SceneKind::Blank);
  c.mode = DisplayMode::Text;
  TEST_ASSERT_TRUE(r.update(c, 10).kind == SceneKind::Text);
  c.mode = DisplayMode::Clock;
  TEST_ASSERT_TRUE(r.update(c, 20).kind == SceneKind::Clock);
  c.mode = DisplayMode::Date;
  TEST_ASSERT_TRUE(r.update(c, 30).kind == SceneKind::Date);
  c.mode = DisplayMode::Single;
  c.single = Platform::Facebook;
  Scene s = r.update(c, 40);
  TEST_ASSERT_TRUE(s.kind == SceneKind::Followers && s.platform == Platform::Facebook);
}

void test_cycle_steps_through_enabled_platforms() {
  Rotation r(rng);
  Config c;
  c.mode = DisplayMode::Cycle;
  c.cycleSeconds = 60;
  c.enabledMask = 0b101;  // TikTok and Facebook
  TEST_ASSERT_TRUE(r.update(c, 1000).platform == Platform::TikTok);
  TEST_ASSERT_TRUE(r.update(c, 60999).platform == Platform::TikTok);
  TEST_ASSERT_TRUE(r.update(c, 61000).platform == Platform::Facebook);
  TEST_ASSERT_TRUE(r.update(c, 121000).platform == Platform::TikTok);
  c.enabledMask = 0b100;  // the platform on show is switched off: move on at once
  TEST_ASSERT_TRUE(r.update(c, 121500).platform == Platform::Facebook);
  c.enabledMask = 0;
  TEST_ASSERT_TRUE(r.update(c, 122000).kind == SceneKind::Blank);
}

void test_cycle_jump() {
  Rotation r(rng);
  Config c;
  c.mode = DisplayMode::Cycle;
  c.cycleSeconds = 60;
  r.update(c, 0);
  r.jumpTo(Platform::Facebook, 50000);
  TEST_ASSERT_TRUE(r.update(c, 100000).platform == Platform::Facebook);  // a full turn from the jump
  TEST_ASSERT_TRUE(r.update(c, 110000).platform == Platform::TikTok);
}

void test_cycle_survives_clock_wrap() {
  Rotation r(rng);
  Config c;
  c.mode = DisplayMode::Cycle;
  c.cycleSeconds = 60;
  const uint32_t start = 0xFFFFFFFFu - 10000;
  TEST_ASSERT_TRUE(r.update(c, start).platform == Platform::TikTok);
  TEST_ASSERT_TRUE(r.update(c, start + 30000).platform == Platform::TikTok);  // wrapped past zero
  TEST_ASSERT_TRUE(r.update(c, start + 60000).platform == Platform::Instagram);
}

void test_random_shows_every_word_before_repeating() {
  Rotation r(rng);
  Config c = withWords({"TACOS", "BURRITO", "NACHOS", "SALSA", "CHURROS"});
  uint32_t now = 0;
  std::string last;
  for (int round = 0; round < 20; round++) {
    std::set<std::string> seen;
    for (int i = 0; i < 5; i++) {
      Scene s = r.update(c, now);
      TEST_ASSERT_TRUE(s.kind == SceneKind::Word);
      TEST_ASSERT_TRUE_MESSAGE(s.word != last, "same word twice in a row");
      last = s.word;
      seen.insert(s.word);
      now += 30000;
    }
    TEST_ASSERT_EQUAL_UINT(5, seen.size());
  }
}

void test_random_holds_a_word_for_its_time() {
  Rotation r(rng);
  Config c = withWords({"A", "B", "C"});
  std::string first = r.update(c, 0).word;
  TEST_ASSERT_EQUAL_STRING(first.c_str(), r.update(c, 29999).word.c_str());
  TEST_ASSERT_TRUE(r.update(c, 30000).word != first);
}

void test_random_empty_and_single() {
  Rotation r(rng);
  Config c = withWords({});
  TEST_ASSERT_TRUE(r.update(c, 0).kind == SceneKind::Blank);
  c.words.push_back("ONLY");
  r.wordsChanged();
  TEST_ASSERT_EQUAL_STRING("ONLY", r.update(c, 10).word.c_str());  // shows as soon as it is added
  TEST_ASSERT_EQUAL_STRING("ONLY", r.update(c, 40000).word.c_str());
}

void test_random_word_list_edits() {
  Rotation r(rng);
  Config c = withWords({"A", "B", "C"});
  std::string shown = r.update(c, 0).word;
  // Adding a word does not disturb the one on show.
  c.words.push_back("D");
  r.wordsChanged();
  TEST_ASSERT_EQUAL_STRING(shown.c_str(), r.update(c, 1000).word.c_str());
  // Removing the one on show replaces it at once.
  c.words.erase(std::find(c.words.begin(), c.words.end(), shown));
  r.wordsChanged();
  Scene s = r.update(c, 2000);
  TEST_ASSERT_TRUE(s.kind == SceneKind::Word && s.word != shown);
  // Shrinking the list never yields a stale entry.
  c.words = {"Z"};
  r.wordsChanged();
  for (uint32_t t = 3000; t < 300000; t += 30000) TEST_ASSERT_EQUAL_STRING("Z", r.update(c, t).word.c_str());
}

void test_mix_runs_entries_in_order() {
  Rotation r(rng);
  Config c = withWords({"HELLO"});
  c.mode = DisplayMode::Mix;
  c.enabledMask = 0b011;  // TikTok, Instagram
  c.mix = {{MixKind::Followers, 20}, {MixKind::Random, 30}, {MixKind::Clock, 40}, {MixKind::Text, 25}, {MixKind::Date, 20}};
  Scene s = r.update(c, 0);
  TEST_ASSERT_TRUE(s.kind == SceneKind::Followers && s.platform == Platform::TikTok);
  s = r.update(c, 20000);  // followers: each enabled platform gets the entry's time
  TEST_ASSERT_TRUE(s.kind == SceneKind::Followers && s.platform == Platform::Instagram);
  s = r.update(c, 40000);
  TEST_ASSERT_TRUE(s.kind == SceneKind::Word);
  TEST_ASSERT_EQUAL_STRING("HELLO", s.word.c_str());
  TEST_ASSERT_TRUE(r.update(c, 69999).kind == SceneKind::Word);
  TEST_ASSERT_TRUE(r.update(c, 70000).kind == SceneKind::Clock);
  TEST_ASSERT_TRUE(r.update(c, 110000).kind == SceneKind::Text);
  TEST_ASSERT_TRUE(r.update(c, 135000).kind == SceneKind::Date);
  s = r.update(c, 155000);  // back to the start
  TEST_ASSERT_TRUE(s.kind == SceneKind::Followers && s.platform == Platform::TikTok);
}

void test_mix_skips_entries_with_nothing_to_show() {
  Rotation r(rng);
  Config c;
  c.mode = DisplayMode::Mix;
  c.enabledMask = 0;  // no platforms, no words
  c.mix = {{MixKind::Followers, 20}, {MixKind::Random, 20}};
  TEST_ASSERT_TRUE(r.update(c, 0).kind == SceneKind::Blank);
  c.mix.push_back({MixKind::Clock, 20});
  r.restart();
  TEST_ASSERT_TRUE(r.update(c, 100).kind == SceneKind::Clock);
  TEST_ASSERT_TRUE(r.update(c, 20100).kind == SceneKind::Clock);  // the only usable entry repeats
  c.mix.clear();
  r.restart();
  TEST_ASSERT_TRUE(r.update(c, 30000).kind == SceneKind::Blank);
}

void test_mix_single_random_entry_keeps_changing() {
  Rotation r(rng);
  Config c = withWords({"A", "B", "C"});
  c.mode = DisplayMode::Mix;
  c.mix = {{MixKind::Random, 20}};
  std::string last = r.update(c, 0).word;
  for (uint32_t t = 20000; t < 400000; t += 20000) {
    std::string w = r.update(c, t).word;
    TEST_ASSERT_TRUE(w != last);
    last = w;
  }
}

void test_mode_change_restarts() {
  Rotation r(rng);
  Config c = withWords({"A"});
  c.mode = DisplayMode::Mix;
  c.mix = {{MixKind::Clock, 20}, {MixKind::Date, 20}};
  r.update(c, 0);
  TEST_ASSERT_TRUE(r.update(c, 20000).kind == SceneKind::Date);
  c.mode = DisplayMode::Off;
  r.update(c, 21000);
  c.mode = DisplayMode::Mix;
  TEST_ASSERT_TRUE(r.update(c, 22000).kind == SceneKind::Clock);  // from the first entry again
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_simple_modes);
  RUN_TEST(test_cycle_steps_through_enabled_platforms);
  RUN_TEST(test_cycle_jump);
  RUN_TEST(test_cycle_survives_clock_wrap);
  RUN_TEST(test_random_shows_every_word_before_repeating);
  RUN_TEST(test_random_holds_a_word_for_its_time);
  RUN_TEST(test_random_empty_and_single);
  RUN_TEST(test_random_word_list_edits);
  RUN_TEST(test_mix_runs_entries_in_order);
  RUN_TEST(test_mix_skips_entries_with_nothing_to_show);
  RUN_TEST(test_mix_single_random_entry_keeps_changing);
  RUN_TEST(test_mode_change_restarts);
  return UNITY_END();
}

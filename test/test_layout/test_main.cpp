#include <unity.h>

#include "charset.h"
#include "display/layout.h"

void setUp() {}
void tearDown() {}

static void eq(const char* expected, const std::string& actual) {
  TEST_ASSERT_EQUAL_STRING(expected, actual.c_str());
}

// ---- formatCount: fits as plain digits, right-aligned, blank padded ----
void test_zero() { eq("      0", formatCount(0, 7)); }
void test_nine() { eq("      9", formatCount(9, 7)); }
void test_full_width() { eq("1234567", formatCount(1234567, 7)); }
void test_max_plain() { eq("9999999", formatCount(9999999, 7)); }
void test_999999_width6() { eq("999999", formatCount(999999, 6)); }
void test_9999_width4_plain() { eq("9999", formatCount(9999, 4)); }

// ---- abbreviation, never wider than width, always rounded down ----
void test_10M_width7() { eq(" 10.00M", formatCount(10000000, 7)); }
void test_12345678_width7() { eq(" 12.34M", formatCount(12345678, 7)); }
void test_987654_width6_fits() { eq("987654", formatCount(987654, 6)); }
void test_9876543_width6() { eq("9.87M", formatCount(9876543, 6).substr(1)); }
void test_987654_width5() { eq(" 987K", formatCount(987654, 5)); }
void test_1234_width3() { eq(" 1K", formatCount(1234, 3)); }
void test_12345_width4() { eq(" 12K", formatCount(12345, 4)); }
void test_12345_width5() { eq("12345", formatCount(12345, 5)); }
void test_123456_width5() { eq(" 123K", formatCount(123456, 5)); }
void test_9999_width3_rounds_down() { eq(" 9K", formatCount(9999, 3)); }
void test_99999_width4_rounds_down() { eq(" 99K", formatCount(99999, 4)); }
void test_1999_width3_rounds_down() { eq(" 1K", formatCount(1999, 3)); }
void test_999999999_width7() { eq("999.99M", formatCount(999999999, 7)); }
void test_1999999_width6() { eq(" 1.99M", formatCount(1999999, 6)); }
void test_billions() { eq(" 12.34B", formatCount(12345678901ULL, 7)); }
void test_never_wider() {
  const uint64_t samples[] = {0, 1, 9, 10, 999, 1000, 9999, 10000, 99999, 123456, 999999, 1000000,
                              9999999, 10000000, 99999999, 999999999, 1000000000, 18446744073709551615ULL};
  for (uint8_t w = 1; w <= 9; w++) {
    for (uint64_t n : samples) {
      TEST_ASSERT_EQUAL_UINT(w, formatCount(n, w).size());
    }
  }
}
void test_too_narrow_is_overflow() { eq("-", formatCount(12, 1)); }

// ---- composeFrame ----
void test_frame_label2() {
  FrameConfig c;  // 9 units, label width 2
  eq("TT   1234", composeFrame(Platform::TikTok, 1234, c));
  eq("IG1234567", composeFrame(Platform::Instagram, 1234567, c));
  eq("FB 12.34M", composeFrame(Platform::Facebook, 12345678, c));
}
void test_frame_label1() {
  FrameConfig c;
  c.units = 8;
  c.labelWidth = 1;
  eq("T   1234", composeFrame(Platform::TikTok, 1234, c));
  eq("I     42", composeFrame(Platform::Instagram, 42, c));
  eq("F      0", composeFrame(Platform::Facebook, 0, c));
}
void test_frame_missing_count_dashes() {
  FrameConfig c;
  eq("IG-------", composeFrame(Platform::Instagram, std::nullopt, c));
  c.units = 8;
  c.labelWidth = 1;
  eq("T-------", composeFrame(Platform::TikTok, std::nullopt, c));
}

// ---- composeText (manual text mode) ----
void test_text_align_and_fold() {
  eq("OLA      ", composeText("olá", 9, Align::Left));
  eq("      OLA", composeText("olá", 9, Align::Right));
  eq("   OLA   ", composeText("olá", 9, Align::Center));
  eq("CORACAO  ", composeText("coração", 9, Align::Left));
  eq("ABCDEFGHI", composeText("abcdefghijkl", 9, Align::Right));  // truncated
}

// ---- charset ----
void test_flap_index() {
  TEST_ASSERT_EQUAL_INT8(0, flapIndex(' '));
  TEST_ASSERT_EQUAL_INT8(1, flapIndex('A'));
  TEST_ASSERT_EQUAL_INT8(1, flapIndex('a'));
  TEST_ASSERT_EQUAL_INT8(26, flapIndex('Z'));
  TEST_ASSERT_EQUAL_INT8(27, flapIndex('$'));
  TEST_ASSERT_EQUAL_INT8(29, flapIndex('#'));
  TEST_ASSERT_EQUAL_INT8(30, flapIndex('0'));
  TEST_ASSERT_EQUAL_INT8(39, flapIndex('9'));
  TEST_ASSERT_EQUAL_INT8(41, flapIndex('.'));
  TEST_ASSERT_EQUAL_INT8(42, flapIndex('-'));
  TEST_ASSERT_EQUAL_INT8(44, flapIndex('!'));
  TEST_ASSERT_EQUAL_INT8(-1, flapIndex(','));
  TEST_ASSERT_EQUAL_INT8(-1, flapIndex('\xC3'));
}
void test_fold_portuguese() {
  char out[64];
  foldToFlaps("ÁÉÍÓÚ ÂÊÔ ÃÕ Ç À áéíóú âêô ãõ ç à", out, sizeof out);
  TEST_ASSERT_EQUAL_STRING("AEIOU AEO AO C A AEIOU AEO AO C A", out);
  foldToFlaps("São João, 12€!", out, sizeof out);
  TEST_ASSERT_EQUAL_STRING("SAO JOAO  12 !", out);  // ',' and '€' have no flap -> blank
}
void test_fold_bounds_and_invalid() {
  char out[4];
  TEST_ASSERT_EQUAL_UINT(3, foldToFlaps("abcdef", out, sizeof out));
  TEST_ASSERT_EQUAL_STRING("ABC", out);
  char out2[8];
  foldToFlaps("a\xC3" "b", out2, sizeof out2);  // truncated UTF-8 sequence must not eat 'b'
  TEST_ASSERT_EQUAL_STRING("A B", out2);
  foldToFlaps("\xF0\x9F\x98\x80x", out2, sizeof out2);  // emoji -> one blank
  TEST_ASSERT_EQUAL_STRING(" X", out2);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_zero);
  RUN_TEST(test_nine);
  RUN_TEST(test_full_width);
  RUN_TEST(test_max_plain);
  RUN_TEST(test_999999_width6);
  RUN_TEST(test_9999_width4_plain);
  RUN_TEST(test_10M_width7);
  RUN_TEST(test_12345678_width7);
  RUN_TEST(test_987654_width6_fits);
  RUN_TEST(test_9876543_width6);
  RUN_TEST(test_987654_width5);
  RUN_TEST(test_1234_width3);
  RUN_TEST(test_12345_width4);
  RUN_TEST(test_12345_width5);
  RUN_TEST(test_123456_width5);
  RUN_TEST(test_9999_width3_rounds_down);
  RUN_TEST(test_99999_width4_rounds_down);
  RUN_TEST(test_1999_width3_rounds_down);
  RUN_TEST(test_999999999_width7);
  RUN_TEST(test_1999999_width6);
  RUN_TEST(test_billions);
  RUN_TEST(test_never_wider);
  RUN_TEST(test_too_narrow_is_overflow);
  RUN_TEST(test_frame_label2);
  RUN_TEST(test_frame_label1);
  RUN_TEST(test_frame_missing_count_dashes);
  RUN_TEST(test_text_align_and_fold);
  RUN_TEST(test_flap_index);
  RUN_TEST(test_fold_portuguese);
  RUN_TEST(test_fold_bounds_and_invalid);
  return UNITY_END();
}

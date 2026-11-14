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
void test_too_narrow_is_overflow() { eq(" ", formatCount(12, 1)); }

// ---- composeFrame ----
void test_frame_label2() {
  FrameConfig c;  // 9 units, label width 2
  c.labelLogos = false;
  eq("TT   1234", composeFrame(Platform::TikTok, 1234, c));
  eq("IG1234567", composeFrame(Platform::Instagram, 1234567, c));
  eq("FB 12.34M", composeFrame(Platform::Facebook, 12345678, c));
}
void test_frame_label1() {
  FrameConfig c;
  c.units = 8;
  c.labelWidth = 1;
  c.labelLogos = false;
  eq("T   1234", composeFrame(Platform::TikTok, 1234, c));
  eq("I     42", composeFrame(Platform::Instagram, 42, c));
  eq("F      0", composeFrame(Platform::Facebook, 0, c));
}
void test_frame_logos() {
  FrameConfig c;  // logos on by default
  eq("\x03    1234", composeFrame(Platform::TikTok, 1234, c));
  eq("\x01 1234567", composeFrame(Platform::Instagram, 1234567, c));
  eq("\x02  12.34M", composeFrame(Platform::Facebook, 12345678, c));
  c.units = 8;
  c.labelWidth = 1;
  eq("\x01     42", composeFrame(Platform::Instagram, 42, c));
  eq("\x03   1234", composeFrame(Platform::TikTok, 1234, c));
}
void test_frame_missing_count_blank() {
  FrameConfig c;
  eq("\x01        ", composeFrame(Platform::Instagram, std::nullopt, c));
  c.labelLogos = false;
  eq("IG       ", composeFrame(Platform::Instagram, std::nullopt, c));
  c.units = 8;
  c.labelWidth = 1;
  eq("T       ", composeFrame(Platform::TikTok, std::nullopt, c));
}

// ---- composeText (manual text mode) ----
void test_text_align_and_fold() {
  eq("OL\xC1      ", composeText("olá", 9, Align::Left));
  eq("      OL\xC1", composeText("olá", 9, Align::Right));
  eq("   OL\xC1   ", composeText("olá", 9, Align::Center));
  eq("CORA\xC7\xC3O  ", composeText("coração", 9, Align::Left));
  eq("VOCE     ", composeText("você", 9, Align::Left));
  eq("ABCDEFGHI", composeText("abcdefghijkl", 9, Align::Right));  // truncated
}

// ---- charset ----
void test_flap_index() {
  TEST_ASSERT_EQUAL_INT8(0, flapIndex(' '));
  TEST_ASSERT_EQUAL_INT8(1, flapIndex('A'));
  TEST_ASSERT_EQUAL_INT8(1, flapIndex('a'));
  TEST_ASSERT_EQUAL_INT8(26, flapIndex('Z'));
  TEST_ASSERT_EQUAL_INT8(27, flapIndex('\xC3'));  // Ã
  TEST_ASSERT_EQUAL_INT8(27, flapIndex('\xE3'));  // ã
  TEST_ASSERT_EQUAL_INT8(28, flapIndex('\xC7'));  // Ç
  TEST_ASSERT_EQUAL_INT8(29, flapIndex('\xC9'));  // É
  TEST_ASSERT_EQUAL_INT8(30, flapIndex('0'));
  TEST_ASSERT_EQUAL_INT8(39, flapIndex('9'));
  TEST_ASSERT_EQUAL_INT8(40, flapIndex('\xC1'));  // Á
  TEST_ASSERT_EQUAL_INT8(41, flapIndex('.'));
  TEST_ASSERT_EQUAL_INT8(42, flapIndex(FLAP_TIKTOK));
  TEST_ASSERT_EQUAL_INT8(43, flapIndex(FLAP_INSTAGRAM));
  TEST_ASSERT_EQUAL_INT8(44, flapIndex(FLAP_FACEBOOK));
  TEST_ASSERT_EQUAL_INT8(-1, flapIndex('-'));
  TEST_ASSERT_EQUAL_INT8(-1, flapIndex('$'));
  TEST_ASSERT_EQUAL_INT8(-1, flapIndex('!'));
  TEST_ASSERT_EQUAL_INT8(-1, flapIndex(','));
  TEST_ASSERT_EQUAL_INT8(-1, flapIndex('\xD5'));  // Õ
  TEST_ASSERT_EQUAL_INT8(-1, flapIndex('\0'));
}
void test_fold_portuguese() {
  char out[64];
  foldToFlaps("ÁÉÍÓÚ ÂÊÔ ÃÕ Ç À áéíóú âêô ãõ ç à", out, sizeof out);
  TEST_ASSERT_EQUAL_STRING("\xC1\xC9IOU AEO \xC3O \xC7 A \xC1\xC9IOU AEO \xC3O \xC7 A", out);
  foldToFlaps("São João, 12€!", out, sizeof out);
  TEST_ASSERT_EQUAL_STRING("S\xC3O JO\xC3O  12  ", out);  // ',' '€' '!' have no flap -> blank
  foldToFlaps("a\x01" "b\x02" "c\x03", out, sizeof out);  // control codes can't reach the logo flaps
  TEST_ASSERT_EQUAL_STRING("A B C ", out);
}
void test_frame_to_utf8() {
  eq("\xEE\x80\x80 S\xC3\x83O \xC3\x87\xEE\x80\x81\xEE\x80\x82", frameToUtf8("\x01 S\xC3O \xC7\x02\x03"));
  eq("FB 12.34M", frameToUtf8("FB 12.34M"));
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

void test_clock() {
  eq("  14 32  ", composeClock(14, 32, true, 9));
  eq("  00 05  ", composeClock(0, 5, true, 9));
  eq("14 32", composeClock(14, 32, true, 5));
  eq("1432", composeClock(14, 32, true, 4));   // no room for the gap
  eq("   ", composeClock(14, 32, true, 3));    // too narrow: blank
  eq(" 2 32 PM ", composeClock(14, 32, false, 9));
  eq("12 05 AM ", composeClock(0, 5, false, 9));    // midnight is 12 AM
  eq("12 00 PM ", composeClock(12, 0, false, 9));   // noon is 12 PM
  eq("11 59 PM", composeClock(23, 59, false, 8));
  eq(" 2 32 ", composeClock(14, 32, false, 6));     // AM/PM dropped when it doesn't fit
  eq("232", composeClock(14, 32, false, 3));
  eq("         ", composeClock(24, 0, true, 9));    // invalid time
  eq("         ", composeClock(10, 60, true, 9));
}

void test_date() {
  eq(" 13 NOV  ", composeDate(13, 11, DateLang::En, 9));
  eq("  1 FEB  ", composeDate(1, 2, DateLang::En, 9));
  eq("  1 FEV  ", composeDate(1, 2, DateLang::Pt, 9));
  eq(" 25 DEZ  ", composeDate(25, 12, DateLang::Pt, 9));
  eq("13NOV", composeDate(13, 11, DateLang::En, 5));
  eq("NOV ", composeDate(13, 11, DateLang::En, 4));
  eq("  ", composeDate(13, 11, DateLang::En, 2));
  eq("         ", composeDate(0, 11, DateLang::En, 9));
  eq("         ", composeDate(13, 13, DateLang::En, 9));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_clock);
  RUN_TEST(test_date);
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
  RUN_TEST(test_frame_logos);
  RUN_TEST(test_frame_missing_count_blank);
  RUN_TEST(test_text_align_and_fold);
  RUN_TEST(test_flap_index);
  RUN_TEST(test_fold_portuguese);
  RUN_TEST(test_fold_bounds_and_invalid);
  RUN_TEST(test_frame_to_utf8);
  return UNITY_END();
}

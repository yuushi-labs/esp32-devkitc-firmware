#include <unity.h>

#include <cstring>

#include "graphics/qr_code.h"
#include "graphics/qr_render.h"

void setUp() {}
void tearDown() {}

namespace {

using namespace fw;

void test_short_text_uses_version_one() {
  QrCode qr;
  TEST_ASSERT_TRUE(qr.encode("hello world"));
  TEST_ASSERT_EQUAL_INT(1, qr.version());
  TEST_ASSERT_EQUAL_INT(21, qr.size());
  TEST_ASSERT_TRUE(qr.valid());
  TEST_ASSERT_TRUE(qr.mask() >= 0 && qr.mask() <= 7);
}

void test_version_grows_with_the_payload() {
  QrCode small;
  TEST_ASSERT_TRUE(small.encode("hello world"));
  TEST_ASSERT_EQUAL_INT(1, small.version());

  QrCode medium;
  TEST_ASSERT_TRUE(medium.encode("01234567890123456789"));
  TEST_ASSERT_EQUAL_INT(2, medium.version());

  QrCode longer;
  TEST_ASSERT_TRUE(longer.encode("http://192.168.1.42/dashboard?station=esp32"));
  TEST_ASSERT_TRUE(longer.version() >= 3);
}

void test_medium_ecc_needs_at_least_as_much_room() {
  const char* text = "http://192.168.1.42/";
  QrCode low;
  QrCode medium;
  TEST_ASSERT_TRUE(low.encode(text, QrEcc::kLow));
  TEST_ASSERT_TRUE(medium.encode(text, QrEcc::kMedium));
  TEST_ASSERT_TRUE(medium.version() >= low.version());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(QrEcc::kMedium),
                        static_cast<int>(medium.ecc()));
}

void test_oversized_payload_is_rejected() {
  char text[400];
  std::memset(text, 'a', sizeof(text) - 1);
  text[sizeof(text) - 1] = '\0';

  QrCode qr;
  TEST_ASSERT_FALSE(qr.encode(text));
  TEST_ASSERT_FALSE(qr.valid());
  TEST_ASSERT_EQUAL_INT(0, qr.size());
}

void test_explicit_mask_is_used_and_changes_the_matrix() {
  QrCode first;
  QrCode second;
  TEST_ASSERT_TRUE(first.encodeWithMask("hello world", QrEcc::kLow, 0));
  TEST_ASSERT_TRUE(second.encodeWithMask("hello world", QrEcc::kLow, 1));
  TEST_ASSERT_EQUAL_INT(0, first.mask());
  TEST_ASSERT_EQUAL_INT(1, second.mask());
  TEST_ASSERT_EQUAL_INT(first.size(), second.size());

  bool differs = false;
  for (int y = 0; y < first.size() && !differs; ++y) {
    for (int x = 0; x < first.size(); ++x) {
      if (first.module(x, y) != second.module(x, y)) {
        differs = true;
        break;
      }
    }
  }
  TEST_ASSERT_TRUE(differs);

  QrCode invalid;
  TEST_ASSERT_FALSE(invalid.encodeWithMask("hello", QrEcc::kLow, 8));
  TEST_ASSERT_FALSE(invalid.encodeWithMask("hello", QrEcc::kLow, -2));
}

void test_finder_patterns_and_separators() {
  QrCode qr;
  TEST_ASSERT_TRUE(qr.encode("hello world"));
  const int size = qr.size();

  const int corners[3][2] = {{0, 0}, {size - 7, 0}, {0, size - 7}};
  for (const auto& corner : corners) {
    const int originX = corner[0];
    const int originY = corner[1];
    // Outer ring dark, inner ring light, 3x3 centre dark.
    TEST_ASSERT_TRUE(qr.module(originX, originY));
    TEST_ASSERT_TRUE(qr.module(originX + 6, originY));
    TEST_ASSERT_TRUE(qr.module(originX, originY + 6));
    TEST_ASSERT_FALSE(qr.module(originX + 1, originY + 1));
    TEST_ASSERT_TRUE(qr.module(originX + 3, originY + 3));
  }

  // The separator around the top-left finder is light.
  TEST_ASSERT_FALSE(qr.module(7, 7));
}

void test_timing_pattern_alternates() {
  QrCode qr;
  TEST_ASSERT_TRUE(qr.encode("hello world"));
  const int size = qr.size();
  for (int index = 8; index < size - 8; ++index) {
    TEST_ASSERT_EQUAL_INT(index % 2 == 0, qr.module(6, index));
    TEST_ASSERT_EQUAL_INT(index % 2 == 0, qr.module(index, 6));
  }
}

void test_dark_module_is_always_set() {
  QrCode qr;
  TEST_ASSERT_TRUE(qr.encode("hello world"));
  TEST_ASSERT_TRUE(qr.module(8, qr.size() - 8));
}

void test_out_of_range_modules_are_light() {
  QrCode qr;
  TEST_ASSERT_TRUE(qr.encode("hello world"));
  TEST_ASSERT_FALSE(qr.module(-1, 0));
  TEST_ASSERT_FALSE(qr.module(0, -1));
  TEST_ASSERT_FALSE(qr.module(qr.size(), 0));
  TEST_ASSERT_FALSE(qr.module(0, qr.size()));
}

void test_encoding_clears_previous_state() {
  QrCode qr;
  TEST_ASSERT_TRUE(qr.encode("http://192.168.1.42/dashboard?station=esp32"));
  const int firstSize = qr.size();
  TEST_ASSERT_TRUE(qr.encode("x"));
  TEST_ASSERT_EQUAL_INT(1, qr.version());
  TEST_ASSERT_EQUAL_INT(21, qr.size());
  TEST_ASSERT_TRUE(firstSize > qr.size());

  qr.clear();
  TEST_ASSERT_FALSE(qr.valid());
  TEST_ASSERT_EQUAL_INT(0, qr.size());
}

void test_renderer_fits_and_centres_the_code() {
  QrCode qr;
  TEST_ASSERT_TRUE(qr.encode("hello"));

  Framebuffer framebuffer;
  const int scale = drawQrCode(framebuffer, qr, Framebuffer::kWidth, Framebuffer::kHeight);
  // Version 1 is 21 modules; with an eight module quiet zone it fits at 2x.
  TEST_ASSERT_EQUAL_INT(2, scale);

  // Quiet zone stays light, the finder pattern has dark pixels.
  TEST_ASSERT_FALSE(framebuffer.pixel(0, 0));
  size_t dark = 0;
  for (int y = 0; y < Framebuffer::kHeight; ++y) {
    for (int x = 0; x < Framebuffer::kWidth; ++x) {
      dark += framebuffer.pixel(x, y) ? 1 : 0;
    }
  }
  TEST_ASSERT_TRUE(dark > 100);
}

void test_renderer_rejects_empty_codes() {
  QrCode qr;
  Framebuffer framebuffer;
  TEST_ASSERT_EQUAL_INT(0, drawQrCode(framebuffer, qr, 128, 64));
}

}  // namespace

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_short_text_uses_version_one);
  RUN_TEST(test_version_grows_with_the_payload);
  RUN_TEST(test_medium_ecc_needs_at_least_as_much_room);
  RUN_TEST(test_oversized_payload_is_rejected);
  RUN_TEST(test_explicit_mask_is_used_and_changes_the_matrix);
  RUN_TEST(test_finder_patterns_and_separators);
  RUN_TEST(test_timing_pattern_alternates);
  RUN_TEST(test_dark_module_is_always_set);
  RUN_TEST(test_out_of_range_modules_are_light);
  RUN_TEST(test_encoding_clears_previous_state);
  RUN_TEST(test_renderer_fits_and_centres_the_code);
  RUN_TEST(test_renderer_rejects_empty_codes);
  return UNITY_END();
}

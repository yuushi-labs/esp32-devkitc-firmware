#include <unity.h>

#include <cstring>
#include <vector>

#include "display/framebuffer.h"
#include "display/page_lines.h"
#include "display/ssd1306.h"
#include "test_support/fake_i2c.h"

void setUp() {}
void tearDown() {}

namespace {

using fw::Framebuffer;
using fw::PageLines;
using fw::Ssd1306Display;
using fw::test::FakeI2cTransport;

void test_new_framebuffer_is_blank() {
  Framebuffer framebuffer;
  for (uint8_t y = 0; y < Framebuffer::kHeight; ++y) {
    for (uint8_t x = 0; x < Framebuffer::kWidth; ++x) {
      TEST_ASSERT_FALSE(framebuffer.pixel(x, y));
    }
  }
}

void test_pixels_are_page_addressed_and_clipped() {
  Framebuffer framebuffer;
  framebuffer.setPixel(0, 0);
  framebuffer.setPixel(127, 63);
  framebuffer.setPixel(5, 9);

  TEST_ASSERT_TRUE(framebuffer.pixel(0, 0));
  TEST_ASSERT_TRUE(framebuffer.pixel(127, 63));
  TEST_ASSERT_TRUE(framebuffer.pixel(5, 9));
  TEST_ASSERT_FALSE(framebuffer.pixel(5, 8));

  // Bit 0 of the first byte is the top row of page 0.
  TEST_ASSERT_EQUAL_UINT8(0x01, framebuffer.buffer()[0]);
  // Page 1 covers y 8..15; y=9 is bit 1.
  TEST_ASSERT_EQUAL_UINT8(0x02, framebuffer.buffer()[Framebuffer::kWidth + 5]);
  // Last byte is page 7, column 127, row 63 -> bit 7.
  TEST_ASSERT_EQUAL_UINT8(0x80, framebuffer.buffer()[1023]);

  framebuffer.setPixel(-1, 0);
  framebuffer.setPixel(0, -1);
  framebuffer.setPixel(128, 0);
  framebuffer.setPixel(0, 64);
  framebuffer.clear();
  TEST_ASSERT_FALSE(framebuffer.pixel(0, 0));
  TEST_ASSERT_FALSE(framebuffer.pixel(127, 63));
}

void test_clear_and_fill_rect() {
  Framebuffer framebuffer;
  framebuffer.fillRect(0, 0, 10, 3);
  TEST_ASSERT_TRUE(framebuffer.pixel(9, 2));
  TEST_ASSERT_FALSE(framebuffer.pixel(10, 2));
  TEST_ASSERT_FALSE(framebuffer.pixel(0, 3));

  framebuffer.drawFrame(20, 20, 10, 10);
  TEST_ASSERT_TRUE(framebuffer.pixel(20, 20));
  TEST_ASSERT_TRUE(framebuffer.pixel(29, 20));
  TEST_ASSERT_TRUE(framebuffer.pixel(20, 29));
  TEST_ASSERT_TRUE(framebuffer.pixel(29, 29));
  TEST_ASSERT_FALSE(framebuffer.pixel(25, 25));

  framebuffer.clear();
  for (size_t index = 0; index < Framebuffer::kWidth * Framebuffer::kPageCount;
       ++index) {
    TEST_ASSERT_EQUAL_UINT8(0, framebuffer.buffer()[index]);
  }
}

void test_glyph_pixels_match_the_font_table() {
  Framebuffer framebuffer;
  framebuffer.drawChar(0, 0, 'A');

  // kFont5x7['A' - 0x20] = {0x7C, 0x12, 0x11, 0x12, 0x7C}
  TEST_ASSERT_FALSE(framebuffer.pixel(0, 0));
  TEST_ASSERT_FALSE(framebuffer.pixel(0, 1));
  TEST_ASSERT_TRUE(framebuffer.pixel(0, 2));
  TEST_ASSERT_TRUE(framebuffer.pixel(0, 6));
  TEST_ASSERT_FALSE(framebuffer.pixel(0, 7));
  TEST_ASSERT_TRUE(framebuffer.pixel(1, 1));
  TEST_ASSERT_FALSE(framebuffer.pixel(1, 0));
}

void test_text_advances_and_scales() {
  Framebuffer framebuffer;
  TEST_ASSERT_EQUAL_UINT32(12, static_cast<uint32_t>(Framebuffer::textWidth("AB")));
  TEST_ASSERT_EQUAL_UINT32(24, static_cast<uint32_t>(Framebuffer::textWidth("AB", 2)));
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(Framebuffer::textWidth("")));

  framebuffer.drawText(0, 0, "AB");
  TEST_ASSERT_TRUE(framebuffer.pixel(6, 2));  // 'B' starts in the second cell

  framebuffer.clear();
  framebuffer.drawText(0, 0, "A", 2);
  TEST_ASSERT_TRUE(framebuffer.pixel(0, 4));
  TEST_ASSERT_TRUE(framebuffer.pixel(1, 5));
  TEST_ASSERT_TRUE(framebuffer.pixel(0, 12));
  TEST_ASSERT_FALSE(framebuffer.pixel(0, 14));
}

void test_degree_sign_counts_as_one_cell() {
  Framebuffer framebuffer;
  const char* text = "23.4 \xC2\xB0" "C";
  TEST_ASSERT_EQUAL_UINT32(7 * 6, static_cast<uint32_t>(Framebuffer::textWidth(text)));

  framebuffer.drawText(0, 0, "A\xC2\xB0");
  // The degree glyph is a small ring: row 1..2 of the second cell.
  TEST_ASSERT_TRUE(framebuffer.pixel(6, 1) || framebuffer.pixel(6, 2));
}

void test_text_is_clipped_at_the_right_edge() {
  Framebuffer framebuffer;
  framebuffer.drawText(124, 0, "AB");
  TEST_ASSERT_TRUE(framebuffer.pixel(124, 2));
  // Nothing is written outside the panel buffer.
  for (size_t index = 0; index < Framebuffer::kWidth * Framebuffer::kPageCount;
       ++index) {
    (void)framebuffer.buffer()[index];
  }
}

void test_fit_chars_for_the_panel_width() {
  TEST_ASSERT_EQUAL_UINT32(21, static_cast<uint32_t>(Framebuffer::fitChars(128)));
  TEST_ASSERT_EQUAL_UINT32(10, static_cast<uint32_t>(Framebuffer::fitChars(128, 2)));
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(Framebuffer::fitChars(5)));
}

void test_page_lines_stack_and_truncate() {
  PageLines page;
  char longLine[64];
  std::memset(longLine, 'W', sizeof(longLine));
  longLine[sizeof(longLine) - 1] = '\0';

  for (size_t index = 0; index < 10; ++index) {
    page.add(index == 0 ? longLine : "line", 1);
  }
  TEST_ASSERT_EQUAL_UINT32(PageLines::kMaxLines, static_cast<uint32_t>(page.count));
  TEST_ASSERT_EQUAL_UINT32(21, static_cast<uint32_t>(std::strlen(page.lines[0].text)));
  TEST_ASSERT_EQUAL_UINT32(8 * PageLines::kMaxLines,
                           static_cast<uint32_t>(page.height()));

  page.clear();
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(page.count));

  page.add("BIG", 2);
  TEST_ASSERT_EQUAL_UINT32(16, static_cast<uint32_t>(page.height()));
  TEST_ASSERT_EQUAL_UINT8(2, page.lines[0].scale);
}

void test_draw_page_lines_places_each_line() {
  PageLines page;
  page.add("A");
  page.add("A");

  Framebuffer framebuffer;
  fw::drawPageLines(framebuffer, page);

  TEST_ASSERT_TRUE(framebuffer.pixel(2, 2));   // first line, first column, row 2
  TEST_ASSERT_TRUE(framebuffer.pixel(2, 10));  // second line starts at y = 8
}

void test_draw_page_lines_stops_at_the_bottom() {
  PageLines page;
  for (size_t index = 0; index < PageLines::kMaxLines; ++index) {
    page.add("BIG", 2);  // 16 px each, only four fit
  }
  TEST_ASSERT_TRUE(page.height() > Framebuffer::kHeight);

  Framebuffer framebuffer;
  fw::drawPageLines(framebuffer, page);
  TEST_ASSERT_TRUE(framebuffer.pixel(2, 2));
  TEST_ASSERT_TRUE(framebuffer.pixel(2, 50));
}

void test_display_sends_the_init_sequence() {
  FakeI2cTransport bus;
  bus.addDevice(0x3C);
  Framebuffer framebuffer;
  Ssd1306Display display(bus, 0x3C, framebuffer);

  TEST_ASSERT_TRUE(display.begin());
  TEST_ASSERT_TRUE(display.initialised());
  TEST_ASSERT_TRUE(display.present());
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(bus.writeLog.size()));

  const std::vector<uint8_t>& init = bus.writeLog[0];
  TEST_ASSERT_EQUAL_UINT8(0x00, init[0]);
  TEST_ASSERT_EQUAL_UINT8(0xAE, init[1]);
  TEST_ASSERT_EQUAL_UINT8(0xAF, init[init.size() - 1]);
}

void test_display_reports_a_missing_panel() {
  FakeI2cTransport bus;
  Framebuffer framebuffer;
  Ssd1306Display display(bus, 0x3C, framebuffer);

  TEST_ASSERT_FALSE(display.begin());
  TEST_ASSERT_FALSE(display.initialised());
  TEST_ASSERT_FALSE(display.flush());
}

void test_detect_address_prefers_0x3c() {
  FakeI2cTransport none;
  TEST_ASSERT_EQUAL_UINT8(0, Ssd1306Display::detectAddress(none));

  FakeI2cTransport alternate;
  alternate.addDevice(0x3D);
  TEST_ASSERT_EQUAL_UINT8(0x3D, Ssd1306Display::detectAddress(alternate));

  FakeI2cTransport both;
  both.addDevice(0x3C);
  both.addDevice(0x3D);
  TEST_ASSERT_EQUAL_UINT8(0x3C, Ssd1306Display::detectAddress(both));
}

void test_flush_uploads_every_page() {
  FakeI2cTransport bus;
  bus.addDevice(0x3C);
  Framebuffer framebuffer;
  Ssd1306Display display(bus, 0x3C, framebuffer);

  TEST_ASSERT_TRUE(display.begin());
  framebuffer.setPixel(0, 0);
  framebuffer.setPixel(127, 63);

  const size_t before = bus.writeLog.size();
  TEST_ASSERT_TRUE(display.flush());
  const size_t writes = bus.writeLog.size() - before;

  // Per page: one page/column command write plus 128 / 16 data writes.
  const size_t writesPerPage = 1 + Framebuffer::kWidth / Ssd1306Display::kDataChunkSize;
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(writesPerPage * Framebuffer::kPageCount),
                           static_cast<uint32_t>(writes));

  const std::vector<uint8_t>& pageCommand = bus.writeLog[before];
  TEST_ASSERT_EQUAL_UINT8(0x00, pageCommand[0]);
  TEST_ASSERT_EQUAL_UINT8(0xB0, pageCommand[1]);

  const std::vector<uint8_t>& firstData = bus.writeLog[before + 1];
  TEST_ASSERT_EQUAL_UINT8(0x40, firstData[0]);
  TEST_ASSERT_EQUAL_UINT8(0x01, firstData[1]);

  const std::vector<uint8_t>& lastData = bus.writeLog.back();
  TEST_ASSERT_EQUAL_UINT8(0x40, lastData[0]);
  // Last chunk is page 7, columns 112..127; the pixel at (127, 63) is the top
  // bit of the last column, which is the last payload byte.
  TEST_ASSERT_EQUAL_UINT8(0x80, lastData[1 + 15]);
}

void test_flush_failure_is_reported() {
  FakeI2cTransport bus;
  bus.addDevice(0x3C);
  Framebuffer framebuffer;
  Ssd1306Display display(bus, 0x3C, framebuffer);
  TEST_ASSERT_TRUE(display.begin());

  bus.failWrites = true;
  TEST_ASSERT_FALSE(display.flush());
}

}  // namespace

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_new_framebuffer_is_blank);
  RUN_TEST(test_pixels_are_page_addressed_and_clipped);
  RUN_TEST(test_clear_and_fill_rect);
  RUN_TEST(test_glyph_pixels_match_the_font_table);
  RUN_TEST(test_text_advances_and_scales);
  RUN_TEST(test_degree_sign_counts_as_one_cell);
  RUN_TEST(test_text_is_clipped_at_the_right_edge);
  RUN_TEST(test_fit_chars_for_the_panel_width);
  RUN_TEST(test_page_lines_stack_and_truncate);
  RUN_TEST(test_draw_page_lines_places_each_line);
  RUN_TEST(test_draw_page_lines_stops_at_the_bottom);
  RUN_TEST(test_display_sends_the_init_sequence);
  RUN_TEST(test_display_reports_a_missing_panel);
  RUN_TEST(test_detect_address_prefers_0x3c);
  RUN_TEST(test_flush_uploads_every_page);
  RUN_TEST(test_flush_failure_is_reported);
  return UNITY_END();
}

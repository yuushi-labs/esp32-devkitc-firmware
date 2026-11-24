// 1 bit per pixel framebuffer for an SSD1306 128x64 panel.
//
// The framebuffer is pure C++ (no Arduino), so the page renderers and the text
// layout are unit tested on the host. The panel driver only has to send the
// finished bytes over I2C.
#pragma once

#include <cstddef>
#include <cstdint>

#include "display/font5x7.h"

namespace fw {

class Framebuffer {
 public:
  static constexpr uint8_t kWidth = 128;
  static constexpr uint8_t kHeight = 64;
  static constexpr uint8_t kPageCount = kHeight / 8;
  static constexpr uint8_t kTextWidth = kFontWidth + 1;   // 5 pixel glyph + gap
  static constexpr uint8_t kTextHeight = kFontHeight + 1; // 7 pixel glyph + gap

  Framebuffer();

  void clear();

  void setPixel(int x, int y, bool on = true);
  bool pixel(int x, int y) const;

  void fillRect(int x, int y, int width, int height, bool on = true);
  void drawHLine(int x, int y, int width, bool on = true);
  void drawVLine(int x, int y, int height, bool on = true);
  void drawFrame(int x, int y, int width, int height, bool on = true);

  void drawChar(int x, int y, char c, uint8_t scale = 1);
  void drawText(int x, int y, const char* text, uint8_t scale = 1);

  // Width in pixels of `text` drawn at `scale`.
  static size_t textWidth(const char* text, uint8_t scale = 1);
  // Characters that fit into `pixelWidth` pixels at `scale`.
  static size_t fitChars(size_t pixelWidth, uint8_t scale = 1);

  const uint8_t* buffer() const { return buffer_; }
  uint8_t width() const { return kWidth; }
  uint8_t height() const { return kHeight; }

 private:
  // One byte holds eight vertical pixels; bit 0 is the top row of the page.
  uint8_t buffer_[kWidth * kPageCount];
};

}  // namespace fw

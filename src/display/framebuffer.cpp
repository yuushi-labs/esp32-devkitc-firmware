#include "display/framebuffer.h"

#include <cstring>

namespace fw {
namespace {

constexpr uint8_t kUtf8LeadC2 = 0xC2;
constexpr uint8_t kDegreeSign = 0xB0;

}  // namespace

Framebuffer::Framebuffer() { clear(); }

void Framebuffer::clear() { std::memset(buffer_, 0, sizeof(buffer_)); }

void Framebuffer::setPixel(int x, int y, bool on) {
  if (x < 0 || y < 0 || x >= kWidth || y >= kHeight) {
    return;
  }
  const size_t index = static_cast<size_t>(y / 8) * kWidth + static_cast<size_t>(x);
  const uint8_t mask = static_cast<uint8_t>(1u << (y % 8));
  if (on) {
    buffer_[index] = static_cast<uint8_t>(buffer_[index] | mask);
  } else {
    buffer_[index] = static_cast<uint8_t>(buffer_[index] & static_cast<uint8_t>(~mask));
  }
}

bool Framebuffer::pixel(int x, int y) const {
  if (x < 0 || y < 0 || x >= kWidth || y >= kHeight) {
    return false;
  }
  const size_t index = static_cast<size_t>(y / 8) * kWidth + static_cast<size_t>(x);
  return (buffer_[index] & static_cast<uint8_t>(1u << (y % 8))) != 0;
}

void Framebuffer::fillRect(int x, int y, int width, int height, bool on) {
  for (int row = 0; row < height; ++row) {
    drawHLine(x, y + row, width, on);
  }
}

void Framebuffer::drawHLine(int x, int y, int width, bool on) {
  for (int column = 0; column < width; ++column) {
    setPixel(x + column, y, on);
  }
}

void Framebuffer::drawVLine(int x, int y, int height, bool on) {
  for (int row = 0; row < height; ++row) {
    setPixel(x, y + row, on);
  }
}

void Framebuffer::drawFrame(int x, int y, int width, int height, bool on) {
  if (width <= 0 || height <= 0) {
    return;
  }
  drawHLine(x, y, width, on);
  drawHLine(x, y + height - 1, width, on);
  drawVLine(x, y, height, on);
  drawVLine(x + width - 1, y, height, on);
}

void Framebuffer::drawChar(int x, int y, char c, uint8_t scale) {
  if (scale == 0) {
    return;
  }
  const uint8_t code = static_cast<uint8_t>(c);
  const uint8_t* glyph = nullptr;
  if (code >= kFontFirstChar && code <= kFontLastChar) {
    glyph = kFont5x7[code - kFontFirstChar];
  } else if (code == kDegreeSign) {
    glyph = kFontDegreeGlyph;
  }
  if (glyph == nullptr) {
    return;
  }

  for (uint8_t column = 0; column < kFontWidth; ++column) {
    const uint8_t bits = glyph[column];
    for (uint8_t row = 0; row < kFontHeight; ++row) {
      if ((bits & static_cast<uint8_t>(1u << row)) == 0) {
        continue;
      }
      for (uint8_t dy = 0; dy < scale; ++dy) {
        for (uint8_t dx = 0; dx < scale; ++dx) {
          setPixel(x + column * scale + dx, y + row * scale + dy, true);
        }
      }
    }
  }
}

void Framebuffer::drawText(int x, int y, const char* text, uint8_t scale) {
  if (text == nullptr || scale == 0) {
    return;
  }
  int cursor = x;
  for (const char* p = text; *p != '\0'; ++p) {
    const uint8_t code = static_cast<uint8_t>(*p);
    if (code == kUtf8LeadC2 && p[1] == static_cast<char>(kDegreeSign)) {
      drawChar(cursor, y, static_cast<char>(kDegreeSign), scale);
      cursor += kTextWidth * scale;
      ++p;
      continue;
    }
    if (code >= 0x80) {
      // Multi byte sequences other than the degree sign are not in the font.
      continue;
    }
    drawChar(cursor, y, *p, scale);
    cursor += kTextWidth * scale;
  }
}

size_t Framebuffer::textWidth(const char* text, uint8_t scale) {
  if (text == nullptr) {
    return 0;
  }
  size_t cells = 0;
  for (const char* p = text; *p != '\0'; ++p) {
    const uint8_t code = static_cast<uint8_t>(*p);
    if (code == kUtf8LeadC2 && p[1] == static_cast<char>(kDegreeSign)) {
      ++cells;
      ++p;
      continue;
    }
    if (code >= 0x80) {
      continue;
    }
    ++cells;
  }
  return cells * kTextWidth * scale;
}

size_t Framebuffer::fitChars(size_t pixelWidth, uint8_t scale) {
  if (scale == 0) {
    return 0;
  }
  const size_t cell = static_cast<size_t>(kTextWidth) * scale;
  return pixelWidth / cell;
}

}  // namespace fw

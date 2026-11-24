#include "display/page_lines.h"

#include <cstring>

namespace fw {
namespace {

// Some padding so a line never touches the panel edge.
constexpr size_t kLinePaddingPx = 2;

// A text unit is either the two byte degree sign (one cell, like drawText()),
// another non ASCII byte (not drawn, no cell) or a plain ASCII character.
struct TextUnit {
  size_t bytes;
  size_t cells;
};

TextUnit unitAt(const char* text, size_t index) {
  const uint8_t code = static_cast<uint8_t>(text[index]);
  if (code == 0xC2 && text[index + 1] == static_cast<char>(0xB0)) {
    return TextUnit{2, 1};
  }
  if (code >= 0x80) {
    return TextUnit{1, 0};
  }
  return TextUnit{1, 1};
}

// Longest prefix that fits in `maxCells` drawn cells and `maxBytes` bytes
// without cutting a UTF-8 sequence.
size_t prefixLength(const char* text, size_t maxCells, size_t maxBytes) {
  size_t index = 0;
  size_t cells = 0;
  while (text[index] != '\0') {
    const TextUnit unit = unitAt(text, index);
    if (cells + unit.cells > maxCells || index + unit.bytes > maxBytes) {
      break;
    }
    index += unit.bytes;
    cells += unit.cells;
  }
  return index;
}

}  // namespace

void PageLines::add(const char* text, uint8_t scale) {
  if (count >= kMaxLines || text == nullptr) {
    return;
  }
  if (scale == 0) {
    scale = 1;
  }
  const size_t cellLimit =
      Framebuffer::fitChars(Framebuffer::kWidth - kLinePaddingPx, scale);
  Line& line = lines[count];
  line.scale = scale;
  const size_t length = prefixLength(text, cellLimit, kLineLength - 1);
  std::memcpy(line.text, text, length);
  line.text[length] = '\0';
  ++count;
}

void PageLines::clear() {
  count = 0;
  for (size_t index = 0; index < kMaxLines; ++index) {
    lines[index].text[0] = '\0';
    lines[index].scale = 1;
  }
}

size_t PageLines::height() const {
  size_t total = 0;
  for (size_t index = 0; index < count; ++index) {
    total += static_cast<size_t>(Framebuffer::kTextHeight) * lines[index].scale;
  }
  return total;
}

void drawPageLines(Framebuffer& framebuffer, const PageLines& page) {
  // Only "on" pixels are ever written, so a fresh page must start blank.
  framebuffer.clear();

  int y = 0;
  for (size_t index = 0; index < page.count; ++index) {
    const PageLines::Line& line = page.lines[index];
    const int lineHeight =
        static_cast<int>(Framebuffer::kTextHeight) * static_cast<int>(line.scale);
    if (y + lineHeight > Framebuffer::kHeight) {
      break;
    }
    framebuffer.drawText(kLinePaddingPx, y, line.text, line.scale);
    y += lineHeight;
  }
}

}  // namespace fw

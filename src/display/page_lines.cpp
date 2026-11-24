#include "display/page_lines.h"

#include <cstring>

namespace fw {
namespace {

// Some padding so a line never touches the panel edge.
constexpr size_t kLinePaddingPx = 2;

size_t clampTextForScale(const char* text, uint8_t scale) {
  const size_t availablePx = Framebuffer::kWidth - kLinePaddingPx;
  return Framebuffer::fitChars(availablePx, scale);
}

}  // namespace

void PageLines::add(const char* text, uint8_t scale) {
  if (count >= kMaxLines || text == nullptr) {
    return;
  }
  if (scale == 0) {
    scale = 1;
  }
  const size_t limit = clampTextForScale(text, scale);
  Line& line = lines[count];
  line.scale = scale;
  size_t length = std::strlen(text);
  if (length > limit) {
    length = limit;
  }
  if (length > kLineLength - 1) {
    length = kLineLength - 1;
  }
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

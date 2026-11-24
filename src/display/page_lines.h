// Text lines rendered on the OLED.
//
// Page renderers fill a PageLines value (pure C++, easy to test); drawPageLines
// turns it into pixels. A line can be drawn at scale 2 for the big readout.
#pragma once

#include <cstddef>
#include <cstdint>

#include "display/framebuffer.h"

namespace fw {

struct PageLines {
  static constexpr size_t kMaxLines = 8;
  static constexpr size_t kLineLength = 32;

  struct Line {
    char text[kLineLength] = {0};
    uint8_t scale = 1;
  };

  Line lines[kMaxLines];
  size_t count = 0;

  // Adds a line, truncating text that would not fit the panel width.
  void add(const char* text, uint8_t scale = 1);
  void clear();
  // Height in pixels the lines need when stacked.
  size_t height() const;
};

// Draws the lines from the top of the panel, one below the other.
void drawPageLines(Framebuffer& framebuffer, const PageLines& page);

}  // namespace fw

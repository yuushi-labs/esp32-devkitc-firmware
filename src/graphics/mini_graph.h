// Ring buffer and mini line graph for the OLED trend screens.
//
// Pure C++ so the scaling (flat lines, missing samples, out-of-range values)
// is unit tested against the real Framebuffer.
#pragma once

#include <cstddef>
#include <cstdint>

#include "display/framebuffer.h"

namespace fw {

class GraphBuffer {
 public:
  static constexpr size_t kMaxCapacity = 64;

  explicit GraphBuffer(size_t capacity = kMaxCapacity);

  void push(float value);
  void clear();

  size_t count() const { return count_; }
  size_t capacity() const { return capacity_; }
  bool empty() const { return count_ == 0; }

  // 0 is the oldest retained sample.
  float at(size_t index) const;
  float oldest() const;
  float newest() const;
  float minimum() const;
  float maximum() const;

 private:
  float values_[kMaxCapacity] = {0};
  size_t capacity_;
  size_t count_ = 0;
  size_t next_ = 0;
};

// Draws the frame and a polyline of every retained sample into the rectangle
// (x, y, width, height). An empty buffer draws only the frame; a constant
// series draws a horizontal line in the middle.
void drawMiniGraph(Framebuffer& framebuffer, const GraphBuffer& graph, int x,
                   int y, int width, int height);

// Same, without the surrounding frame (for embedding in a larger layout).
void drawSparkline(Framebuffer& framebuffer, const GraphBuffer& graph, int x,
                   int y, int width, int height);

}  // namespace fw

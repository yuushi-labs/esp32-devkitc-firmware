#include "graphics/mini_graph.h"

namespace fw {
namespace {

constexpr float kFlatEpsilon = 0.0001f;

int clamp(int value, int low, int high) {
  if (value < low) {
    return low;
  }
  if (value > high) {
    return high;
  }
  return value;
}

float sampleAt(const GraphBuffer& graph, int x, int width) {
  const int count = static_cast<int>(graph.count());
  if (count == 1 || width <= 1) {
    return graph.newest();
  }
  // Map the pixel column onto the sample index range, interpolating between
  // neighbouring samples so a sparse series still spans the whole graph.
  const float position =
      static_cast<float>(x) * static_cast<float>(count - 1) / static_cast<float>(width - 1);
  const int index = static_cast<int>(position);
  const float fraction = position - static_cast<float>(index);
  if (index >= count - 1) {
    return graph.at(static_cast<size_t>(count - 1));
  }
  const float a = graph.at(static_cast<size_t>(index));
  const float b = graph.at(static_cast<size_t>(index + 1));
  return a + (b - a) * fraction;
}

void plot(const GraphBuffer& graph, Framebuffer& framebuffer, int x, int y,
          int width, int height) {
  if (graph.empty() || width <= 0 || height <= 0) {
    return;
  }

  const float minimum = graph.minimum();
  const float maximum = graph.maximum();
  const float range = maximum - minimum;
  const bool flat = range < kFlatEpsilon;
  const int middle = y + height / 2;

  int previousY = -1;
  for (int column = 0; column < width; ++column) {
    int row = middle;
    if (!flat) {
      const float value = sampleAt(graph, column, width);
      const float normalized = (value - minimum) / range;
      row = y + height - 1 - static_cast<int>(normalized * (height - 1) + 0.5f);
    }
    row = clamp(row, y, y + height - 1);

    framebuffer.setPixel(x + column, row, true);
    if (previousY >= 0 && previousY != row) {
      const int top = previousY < row ? previousY : row;
      const int bottom = previousY < row ? row : previousY;
      for (int fill = top; fill <= bottom; ++fill) {
        framebuffer.setPixel(x + column, fill, true);
      }
    }
    previousY = row;
  }
}

}  // namespace

GraphBuffer::GraphBuffer(size_t capacity) : capacity_(capacity) {
  if (capacity_ == 0) {
    capacity_ = 1;
  }
  if (capacity_ > kMaxCapacity) {
    capacity_ = kMaxCapacity;
  }
}

void GraphBuffer::push(float value) {
  values_[next_] = value;
  next_ = (next_ + 1) % capacity_;
  if (count_ < capacity_) {
    ++count_;
  }
}

void GraphBuffer::clear() {
  count_ = 0;
  next_ = 0;
}

float GraphBuffer::at(size_t index) const {
  if (count_ == 0) {
    return 0.0f;
  }
  if (index >= count_) {
    index = count_ - 1;
  }
  const size_t oldest = (next_ + capacity_ - count_) % capacity_;
  return values_[(oldest + index) % capacity_];
}

float GraphBuffer::oldest() const { return at(0); }

float GraphBuffer::newest() const {
  return count_ == 0 ? 0.0f : at(count_ - 1);
}

float GraphBuffer::minimum() const {
  if (count_ == 0) {
    return 0.0f;
  }
  float value = at(0);
  for (size_t index = 1; index < count_; ++index) {
    const float sample = at(index);
    if (sample < value) {
      value = sample;
    }
  }
  return value;
}

float GraphBuffer::maximum() const {
  if (count_ == 0) {
    return 0.0f;
  }
  float value = at(0);
  for (size_t index = 1; index < count_; ++index) {
    const float sample = at(index);
    if (sample > value) {
      value = sample;
    }
  }
  return value;
}

void drawMiniGraph(Framebuffer& framebuffer, const GraphBuffer& graph, int x,
                   int y, int width, int height) {
  if (width <= 0 || height <= 0) {
    return;
  }
  framebuffer.drawFrame(x, y, width, height, true);
  plot(graph, framebuffer, x + 1, y + 1, width - 2, height - 2);
}

void drawSparkline(Framebuffer& framebuffer, const GraphBuffer& graph, int x,
                   int y, int width, int height) {
  plot(graph, framebuffer, x, y, width, height);
}

}  // namespace fw

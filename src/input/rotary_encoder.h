// Quadrature rotary encoder decoder.
//
// Pure state machine: the GPIO layer only passes the two pin levels, so the
// direction/step logic is unit tested with level sequences.
#pragma once

#include <cstdint>

namespace fw {

class RotaryEncoder {
 public:
  // Mechanical encoders produce four quadrature transitions per detent.
  static constexpr int8_t kStepsPerDetent = 4;

  explicit RotaryEncoder(bool invert = false);

  // Feeds the current A/B levels. Returns +1 / -1 when a detent was completed
  // by this call, 0 otherwise.
  int8_t update(bool levelA, bool levelB);

  // Detents turned since the last reset (negative when counter clockwise).
  int32_t position() const { return position_; }
  void reset();

 private:
  bool invert_;
  uint8_t previousState_ = 0;
  int8_t accumulator_ = 0;
  int32_t position_ = 0;
};

}  // namespace fw

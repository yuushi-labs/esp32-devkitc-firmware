#include "input/rotary_encoder.h"

namespace fw {
namespace {

// Index is (previousState << 2) | newState, where the state is (A << 1) | B.
// A valid clockwise step moves through 00 -> 01 -> 11 -> 10 and returns +1 per
// transition, the other direction -1. Bounce (same state) and impossible jumps
// contribute nothing.
constexpr int8_t kTransitionTable[16] = {
    0,  1,  -1, 0,
    -1, 0,  0,  1,
    1,  0,  0,  -1,
    0,  -1, 1,  0,
};

}  // namespace

RotaryEncoder::RotaryEncoder(bool invert) : invert_(invert) {}

int8_t RotaryEncoder::update(bool levelA, bool levelB) {
  const uint8_t state = static_cast<uint8_t>((levelA ? 2 : 0) | (levelB ? 1 : 0));
  const int8_t step = kTransitionTable[static_cast<uint8_t>((previousState_ << 2) | state)];
  previousState_ = state;

  accumulator_ = static_cast<int8_t>(accumulator_ + (invert_ ? -step : step));

  int8_t detents = 0;
  while (accumulator_ >= kStepsPerDetent) {
    accumulator_ = static_cast<int8_t>(accumulator_ - kStepsPerDetent);
    ++detents;
  }
  while (accumulator_ <= -kStepsPerDetent) {
    accumulator_ = static_cast<int8_t>(accumulator_ + kStepsPerDetent);
    --detents;
  }

  position_ += detents;
  return detents;
}

void RotaryEncoder::reset() {
  previousState_ = 0;
  accumulator_ = 0;
  position_ = 0;
}

}  // namespace fw

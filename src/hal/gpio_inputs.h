// Encoder and button GPIO layer.
//
// Reads the three buttons (active low, internal pull-ups) and the two encoder
// phases, then hands the levels to the pure decoders in src/input.
#pragma once

#include <Arduino.h>

#include <cstddef>
#include <cstdint>

#include "app_config.h"
#include "input/button.h"
#include "input/rotary_encoder.h"

namespace fw {

enum class InputButton : uint8_t {
  kPush = 0,
  kBack = 1,
  kConfirm = 2,
};

constexpr size_t kInputButtonCount = 3;

struct InputEvent {
  int8_t encoderDelta = 0;
  bool press[kInputButtonCount] = {false, false, false};
  bool release[kInputButtonCount] = {false, false, false};
  bool longPress[kInputButtonCount] = {false, false, false};

  bool any() const;
};

class GpioInputs {
 public:
  GpioInputs();

  void begin();
  InputEvent poll(uint32_t nowMs);

  int32_t encoderPosition() const { return encoder_.position(); }
  bool down(InputButton button) const;
  uint32_t pressCount(InputButton button) const;

  static const char* buttonName(InputButton button);

 private:
  static bool pressedLevel(uint8_t pin);

  RotaryEncoder encoder_;
  DebouncedButton push_;
  DebouncedButton back_;
  DebouncedButton confirm_;
  uint32_t pressCounts_[kInputButtonCount] = {0, 0, 0};
};

}  // namespace fw

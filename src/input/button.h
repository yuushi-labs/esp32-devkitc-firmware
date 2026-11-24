// Debounced push button with long press detection.
//
// Time is passed in by the caller (milliseconds, wrapping allowed) so the
// debounce and long press behaviour is unit tested without sleeping.
#pragma once

#include <cstdint>

namespace fw {

enum class ButtonEvent {
  kNone,
  kPress,
  kRelease,
  kLongPress,
};

const char* buttonEventName(ButtonEvent event);

class DebouncedButton {
 public:
  DebouncedButton(uint32_t debounceMs, uint32_t longPressMs);

  // Feeds the raw pin level (true = pressed, active low inputs are inverted by
  // the GPIO layer). Returns the event this update produced.
  ButtonEvent update(bool rawPressed, uint32_t nowMs);

  bool pressed() const { return pressed_; }
  bool longPressFired() const { return longPressFired_; }
  // How long the button has been held at `nowMs` (0 when not pressed).
  uint32_t heldMs(uint32_t nowMs) const;
  uint32_t pressCount() const { return pressCount_; }
  void reset();

 private:
  uint32_t debounceMs_;
  uint32_t longPressMs_;
  bool rawPressed_ = false;
  bool pressed_ = false;
  bool longPressFired_ = false;
  bool pendingChange_ = false;
  bool pendingValue_ = false;
  uint32_t lastRawChangeMs_ = 0;
  uint32_t pressedAtMs_ = 0;
  uint32_t pressCount_ = 0;
};

}  // namespace fw

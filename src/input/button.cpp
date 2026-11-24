#include "input/button.h"

namespace fw {

const char* buttonEventName(ButtonEvent event) {
  switch (event) {
    case ButtonEvent::kPress:
      return "press";
    case ButtonEvent::kRelease:
      return "release";
    case ButtonEvent::kLongPress:
      return "long-press";
    case ButtonEvent::kNone:
      break;
  }
  return "none";
}

DebouncedButton::DebouncedButton(uint32_t debounceMs, uint32_t longPressMs)
    : debounceMs_(debounceMs), longPressMs_(longPressMs) {}

ButtonEvent DebouncedButton::update(bool rawPressed, uint32_t nowMs) {
  if (rawPressed != rawPressed_) {
    rawPressed_ = rawPressed;
    pendingChange_ = true;
    pendingValue_ = rawPressed;
    lastRawChangeMs_ = nowMs;
  }

  ButtonEvent event = ButtonEvent::kNone;

  if (pendingChange_ && (nowMs - lastRawChangeMs_) >= debounceMs_) {
    pendingChange_ = false;
    if (pendingValue_ != pressed_) {
      pressed_ = pendingValue_;
      if (pressed_) {
        pressedAtMs_ = lastRawChangeMs_;
        longPressFired_ = false;
        ++pressCount_;
        event = ButtonEvent::kPress;
      } else {
        longPressFired_ = false;
        event = ButtonEvent::kRelease;
      }
    }
  }

  if (pressed_ && !longPressFired_ &&
      (nowMs - pressedAtMs_) >= longPressMs_) {
    // A long press is reported after the press it belongs to; keep a release
    // that happened in the same call from being swallowed, and only mark it as
    // reported once it is actually reported (a delayed press confirmation must
    // not swallow the long press).
    if (event == ButtonEvent::kNone) {
      longPressFired_ = true;
      event = ButtonEvent::kLongPress;
    }
  }

  return event;
}

uint32_t DebouncedButton::heldMs(uint32_t nowMs) const {
  if (!pressed_) {
    return 0;
  }
  return nowMs - pressedAtMs_;
}

void DebouncedButton::reset() {
  rawPressed_ = false;
  pressed_ = false;
  longPressFired_ = false;
  pendingChange_ = false;
  pendingValue_ = false;
  lastRawChangeMs_ = 0;
  pressedAtMs_ = 0;
  pressCount_ = 0;
}

}  // namespace fw

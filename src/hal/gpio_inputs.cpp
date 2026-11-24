#include "hal/gpio_inputs.h"

namespace fw {

bool InputEvent::any() const {
  if (encoderDelta != 0) {
    return true;
  }
  for (size_t index = 0; index < kInputButtonCount; ++index) {
    if (press[index] || release[index] || longPress[index]) {
      return true;
    }
  }
  return false;
}

GpioInputs::GpioInputs()
    : encoder_(),
      push_(kButtonDebounceMs, kButtonLongPressMs),
      back_(kButtonDebounceMs, kButtonLongPressMs),
      confirm_(kButtonDebounceMs, kButtonLongPressMs) {}

void GpioInputs::begin() {
  pinMode(kEncoderPinA, INPUT_PULLUP);
  pinMode(kEncoderPinB, INPUT_PULLUP);
  pinMode(kEncoderPushPin, INPUT_PULLUP);
  pinMode(kBackPin, INPUT_PULLUP);
  pinMode(kConfirmPin, INPUT_PULLUP);

  // Seed the encoder with the current levels so the first poll does not look
  // like a rotation from 00.
  encoder_.update(digitalRead(kEncoderPinA) == HIGH, digitalRead(kEncoderPinB) == HIGH);
}

bool GpioInputs::pressedLevel(uint8_t pin) { return digitalRead(pin) == LOW; }

InputEvent GpioInputs::poll(uint32_t nowMs) {
  InputEvent event;

  event.encoderDelta =
      encoder_.update(digitalRead(kEncoderPinA) == HIGH, digitalRead(kEncoderPinB) == HIGH);

  DebouncedButton* buttons[kInputButtonCount] = {&push_, &back_, &confirm_};
  const uint8_t pins[kInputButtonCount] = {kEncoderPushPin, kBackPin, kConfirmPin};

  for (size_t index = 0; index < kInputButtonCount; ++index) {
    const ButtonEvent buttonEvent = buttons[index]->update(pressedLevel(pins[index]), nowMs);
    switch (buttonEvent) {
      case ButtonEvent::kPress:
        event.press[index] = true;
        pressCounts_[index] = buttons[index]->pressCount();
        break;
      case ButtonEvent::kRelease:
        event.release[index] = true;
        break;
      case ButtonEvent::kLongPress:
        event.longPress[index] = true;
        break;
      case ButtonEvent::kNone:
        break;
    }
  }

  return event;
}

bool GpioInputs::down(InputButton button) const {
  switch (button) {
    case InputButton::kPush:
      return push_.pressed();
    case InputButton::kBack:
      return back_.pressed();
    case InputButton::kConfirm:
      return confirm_.pressed();
  }
  return false;
}

uint32_t GpioInputs::pressCount(InputButton button) const {
  const size_t index = static_cast<size_t>(button);
  return index < kInputButtonCount ? pressCounts_[index] : 0;
}

const char* GpioInputs::buttonName(InputButton button) {
  switch (button) {
    case InputButton::kPush:
      return "PSH";
    case InputButton::kBack:
      return "BAK";
    case InputButton::kConfirm:
      return "CON";
  }
  return "?";
}

}  // namespace fw

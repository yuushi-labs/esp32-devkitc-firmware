#include <unity.h>

#include "input/button.h"
#include "input/rotary_encoder.h"

void setUp() {}
void tearDown() {}

namespace {

using fw::ButtonEvent;
using fw::DebouncedButton;
using fw::RotaryEncoder;

int8_t stepSequence(RotaryEncoder& encoder, const bool pairs[][2], size_t count) {
  int8_t detents = 0;
  for (size_t index = 0; index < count; ++index) {
    detents = static_cast<int8_t>(detents + encoder.update(pairs[index][0], pairs[index][1]));
  }
  return detents;
}

void test_full_quadrature_cycle_is_one_detent() {
  RotaryEncoder encoder;
  const bool clockwise[][2] = {
      {false, true}, {true, true}, {true, false}, {false, false}};

  TEST_ASSERT_EQUAL_INT8(0, encoder.update(false, false));  // no movement
  TEST_ASSERT_EQUAL_INT8(0, encoder.update(false, true));
  TEST_ASSERT_EQUAL_INT8(0, encoder.update(true, true));
  TEST_ASSERT_EQUAL_INT8(0, encoder.update(true, false));
  TEST_ASSERT_EQUAL_INT8(1, encoder.update(false, false));
  TEST_ASSERT_EQUAL_INT32(1, encoder.position());
}

void test_counter_clockwise_is_negative() {
  RotaryEncoder encoder;
  TEST_ASSERT_EQUAL_INT8(0, encoder.update(true, false));
  TEST_ASSERT_EQUAL_INT8(0, encoder.update(true, true));
  TEST_ASSERT_EQUAL_INT8(0, encoder.update(false, true));
  TEST_ASSERT_EQUAL_INT8(-1, encoder.update(false, false));
  TEST_ASSERT_EQUAL_INT32(-1, encoder.position());
}

void test_repeated_cycles_accumulate() {
  RotaryEncoder encoder;
  const bool clockwise[][2] = {
      {false, true}, {true, true}, {true, false}, {false, false}};
  for (int cycle = 0; cycle < 3; ++cycle) {
    stepSequence(encoder, clockwise, 4);
  }
  TEST_ASSERT_EQUAL_INT32(3, encoder.position());
}

void test_bounce_and_skipped_states_are_ignored() {
  RotaryEncoder encoder;
  TEST_ASSERT_EQUAL_INT8(0, encoder.update(false, false));
  TEST_ASSERT_EQUAL_INT8(0, encoder.update(false, false));  // contact bounce
  TEST_ASSERT_EQUAL_INT8(0, encoder.update(false, true));
  TEST_ASSERT_EQUAL_INT8(0, encoder.update(true, false));  // invalid double jump
  TEST_ASSERT_EQUAL_INT8(0, encoder.update(false, false));
  TEST_ASSERT_EQUAL_INT32(0, encoder.position());
}

void test_invert_swaps_the_direction() {
  RotaryEncoder encoder(true);
  TEST_ASSERT_EQUAL_INT8(0, encoder.update(false, true));
  TEST_ASSERT_EQUAL_INT8(0, encoder.update(true, true));
  TEST_ASSERT_EQUAL_INT8(0, encoder.update(true, false));
  TEST_ASSERT_EQUAL_INT8(-1, encoder.update(false, false));
  TEST_ASSERT_EQUAL_INT32(-1, encoder.position());
}

void test_reset_clears_the_position() {
  RotaryEncoder encoder;
  TEST_ASSERT_EQUAL_INT8(0, encoder.update(false, true));
  TEST_ASSERT_EQUAL_INT8(0, encoder.update(true, true));
  TEST_ASSERT_EQUAL_INT8(0, encoder.update(true, false));
  TEST_ASSERT_EQUAL_INT8(1, encoder.update(false, false));
  encoder.reset();
  TEST_ASSERT_EQUAL_INT32(0, encoder.position());
}

void test_button_reports_press_and_release_after_debounce() {
  DebouncedButton button(25, 900);

  TEST_ASSERT_EQUAL_INT(static_cast<int>(ButtonEvent::kNone),
                        static_cast<int>(button.update(true, 0)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ButtonEvent::kPress),
                        static_cast<int>(button.update(true, 30)));
  TEST_ASSERT_TRUE(button.pressed());
  TEST_ASSERT_EQUAL_UINT32(1, button.pressCount());

  TEST_ASSERT_EQUAL_INT(static_cast<int>(ButtonEvent::kNone),
                        static_cast<int>(button.update(true, 300)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ButtonEvent::kNone),
                        static_cast<int>(button.update(false, 310)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ButtonEvent::kRelease),
                        static_cast<int>(button.update(false, 340)));
  TEST_ASSERT_FALSE(button.pressed());
  TEST_ASSERT_EQUAL_UINT32(1, button.pressCount());
}

void test_button_ignores_contact_bounce() {
  DebouncedButton button(25, 900);
  button.update(true, 0);
  button.update(false, 10);   // glitch shorter than the debounce window
  button.update(false, 50);   // settles as "not pressed"
  TEST_ASSERT_FALSE(button.pressed());
  TEST_ASSERT_EQUAL_UINT32(0, button.pressCount());
}

void test_long_press_fires_once() {
  DebouncedButton button(25, 900);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ButtonEvent::kNone),
                        static_cast<int>(button.update(true, 0)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ButtonEvent::kPress),
                        static_cast<int>(button.update(true, 30)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ButtonEvent::kNone),
                        static_cast<int>(button.update(true, 500)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ButtonEvent::kLongPress),
                        static_cast<int>(button.update(true, 930)));
  TEST_ASSERT_TRUE(button.longPressFired());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ButtonEvent::kNone),
                        static_cast<int>(button.update(true, 1500)));

  button.update(false, 1510);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(ButtonEvent::kRelease),
                        static_cast<int>(button.update(false, 1540)));
  TEST_ASSERT_FALSE(button.longPressFired());
}

void test_held_time_and_event_names() {
  DebouncedButton button(25, 900);
  TEST_ASSERT_EQUAL_UINT32(0, button.heldMs(1234));
  button.update(true, 0);
  button.update(true, 30);
  TEST_ASSERT_EQUAL_UINT32(500, button.heldMs(500));

  TEST_ASSERT_EQUAL_STRING("press", fw::buttonEventName(ButtonEvent::kPress));
  TEST_ASSERT_EQUAL_STRING("release", fw::buttonEventName(ButtonEvent::kRelease));
  TEST_ASSERT_EQUAL_STRING("long-press", fw::buttonEventName(ButtonEvent::kLongPress));
  TEST_ASSERT_EQUAL_STRING("none", fw::buttonEventName(ButtonEvent::kNone));
}

void test_reset_restores_the_initial_state() {
  DebouncedButton button(25, 900);
  button.update(true, 0);
  button.update(true, 30);
  button.reset();
  TEST_ASSERT_FALSE(button.pressed());
  TEST_ASSERT_FALSE(button.longPressFired());
  TEST_ASSERT_EQUAL_UINT32(0, button.pressCount());
  TEST_ASSERT_EQUAL_UINT32(0, button.heldMs(100));
}

}  // namespace

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_full_quadrature_cycle_is_one_detent);
  RUN_TEST(test_counter_clockwise_is_negative);
  RUN_TEST(test_repeated_cycles_accumulate);
  RUN_TEST(test_bounce_and_skipped_states_are_ignored);
  RUN_TEST(test_invert_swaps_the_direction);
  RUN_TEST(test_reset_clears_the_position);
  RUN_TEST(test_button_reports_press_and_release_after_debounce);
  RUN_TEST(test_button_ignores_contact_bounce);
  RUN_TEST(test_long_press_fires_once);
  RUN_TEST(test_held_time_and_event_names);
  RUN_TEST(test_reset_restores_the_initial_state);
  return UNITY_END();
}

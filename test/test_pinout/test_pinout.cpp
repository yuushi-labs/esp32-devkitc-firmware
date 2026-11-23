#include <unity.h>

#include <cstring>

#include "pinout.h"

void setUp() {}
void tearDown() {}

namespace {

fw::PinWiring makePin() {
  fw::PinWiring pin{};
  pin.gpio = 2;
  pin.label = "onboard LED";
  pin.direction = "output";
  pin.activeLevel = "HIGH";
  pin.note = "220 ohm to GND";
  return pin;
}

void assertContains(const char* haystack, const char* needle) {
  TEST_ASSERT_NOT_NULL_MESSAGE(std::strstr(haystack, needle), needle);
}

void test_embedded_pinout_matches_pins_json() {
  const fw::PinoutTable table = fw::embeddedPinout();

  TEST_ASSERT_EQUAL_INT(1, table.schemaVersion);
  TEST_ASSERT_NOT_NULL(table.pins);
  TEST_ASSERT_EQUAL_UINT32(2, static_cast<uint32_t>(table.pinCount));
  TEST_ASSERT_EQUAL_STRING("esp32dev", table.board);

  TEST_ASSERT_EQUAL_INT(2, table.pins[0].gpio);
  TEST_ASSERT_EQUAL_STRING("output", table.pins[0].direction);
  TEST_ASSERT_EQUAL_STRING("HIGH", table.pins[0].activeLevel);
  TEST_ASSERT_NOT_EQUAL('\0', table.pins[0].label[0]);
  TEST_ASSERT_NOT_EQUAL('\0', table.pins[0].note[0]);

  TEST_ASSERT_EQUAL_INT(0, table.pins[1].gpio);
  TEST_ASSERT_EQUAL_STRING("input_pullup", table.pins[1].direction);
  TEST_ASSERT_EQUAL_STRING("LOW", table.pins[1].activeLevel);

  TEST_ASSERT_NOT_EQUAL('\0', table.powerNote[0]);
  TEST_ASSERT_EQUAL_UINT32(3, static_cast<uint32_t>(table.noteCount));
}

void test_pinout_lists_every_pin_and_note() {
  const fw::PinoutTable table = fw::embeddedPinout();

  char out[1024];
  const size_t needed = fw::formatPinout(table, out, sizeof(out));

  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(std::strlen(out)),
                           static_cast<uint32_t>(needed));
  assertContains(out, "pinout board=esp32dev pins=2\n");
  assertContains(out, "GPIO2 output active=HIGH ");
  assertContains(out, table.pins[0].label);
  assertContains(out, "  note: ");
  assertContains(out, table.pins[0].note);
  assertContains(out, "GPIO0 input_pullup active=LOW ");
  assertContains(out, "power: ");
  assertContains(out, table.powerNote);
  assertContains(out, "note: ");
  assertContains(out, table.notes[0]);
  assertContains(out, table.notes[table.noteCount - 1]);
}

void test_optional_fields_are_omitted() {
  fw::PinWiring pin = makePin();
  pin.activeLevel = "";
  pin.note = "";

  char out[128];
  const size_t needed = fw::formatPinLine(pin, out, sizeof(out));

  TEST_ASSERT_EQUAL_STRING("GPIO2 output onboard LED\n", out);
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(std::strlen(out)),
                           static_cast<uint32_t>(needed));
  TEST_ASSERT_NULL(std::strstr(out, "active="));
  TEST_ASSERT_NULL(std::strstr(out, "note:"));
}

void test_null_optional_fields_are_omitted() {
  fw::PinWiring pin = makePin();
  pin.activeLevel = nullptr;
  pin.note = nullptr;
  pin.label = nullptr;

  char out[128];
  fw::formatPinLine(pin, out, sizeof(out));

  TEST_ASSERT_EQUAL_STRING("GPIO2 output \n", out);
}

void test_pin_line_includes_the_note() {
  const fw::PinWiring pin = makePin();

  char out[128];
  fw::formatPinLine(pin, out, sizeof(out));

  TEST_ASSERT_EQUAL_STRING(
      "GPIO2 output active=HIGH onboard LED\n"
      "  note: 220 ohm to GND\n",
      out);
}

void test_empty_table_reports_missing_pin_data() {
  const fw::PinoutTable table{};

  char out[128];
  const size_t needed = fw::formatPinout(table, out, sizeof(out));

  TEST_ASSERT_EQUAL_STRING(
      "pinout: this firmware was built without pins.json\n", out);
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(std::strlen(out)),
                           static_cast<uint32_t>(needed));
}

void test_zero_sized_buffer_reports_the_needed_length() {
  const fw::PinoutTable table = fw::embeddedPinout();

  char out[1024];
  const size_t expected = fw::formatPinout(table, out, sizeof(out));
  const size_t needed = fw::formatPinout(table, nullptr, 0);

  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(expected),
                           static_cast<uint32_t>(needed));
}

void test_truncated_output_is_nul_terminated() {
  const fw::PinoutTable table = fw::embeddedPinout();

  char out[16];
  std::memset(out, 'x', sizeof(out));
  const size_t needed = fw::formatPinout(table, out, sizeof(out));

  TEST_ASSERT_TRUE(needed > sizeof(out) - 1);
  TEST_ASSERT_EQUAL_UINT32(sizeof(out) - 1,
                           static_cast<uint32_t>(std::strlen(out)));
  TEST_ASSERT_EQUAL_STRING_LEN("pinout board=es", out, 15);
}

void test_table_without_pins_is_treated_as_empty() {
  fw::PinoutTable table = fw::embeddedPinout();
  table.pinCount = 0;

  char out[128];
  fw::formatPinout(table, out, sizeof(out));

  TEST_ASSERT_EQUAL_STRING(
      "pinout: this firmware was built without pins.json\n", out);
}

}  // namespace

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_embedded_pinout_matches_pins_json);
  RUN_TEST(test_pinout_lists_every_pin_and_note);
  RUN_TEST(test_optional_fields_are_omitted);
  RUN_TEST(test_null_optional_fields_are_omitted);
  RUN_TEST(test_pin_line_includes_the_note);
  RUN_TEST(test_empty_table_reports_missing_pin_data);
  RUN_TEST(test_zero_sized_buffer_reports_the_needed_length);
  RUN_TEST(test_truncated_output_is_nul_terminated);
  RUN_TEST(test_table_without_pins_is_treated_as_empty);
  return UNITY_END();
}

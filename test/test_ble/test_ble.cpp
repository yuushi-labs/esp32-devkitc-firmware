#include <unity.h>

#include <cstring>

#include "ble/ble_protocol.h"

void setUp() {}
void tearDown() {}

namespace {

using namespace fw;

SensorSnapshot healthySensors() {
  SensorSnapshot sensors;
  sensors.temperatureValid = true;
  sensors.temperatureC = 23.4f;
  sensors.humidityValid = true;
  sensors.humidityPct = 45.0f;
  sensors.pressureValid = true;
  sensors.pressureHpa = 1006.5f;
  return sensors;
}

void test_media_actions_map_to_consumer_usages() {
  TEST_ASSERT_EQUAL_HEX16(0x00E9, mediaActionUsage(MediaAction::kVolumeUp));
  TEST_ASSERT_EQUAL_HEX16(0x00EA, mediaActionUsage(MediaAction::kVolumeDown));
  TEST_ASSERT_EQUAL_HEX16(0x00CD, mediaActionUsage(MediaAction::kPlayPause));
  TEST_ASSERT_EQUAL_HEX16(0x00B5, mediaActionUsage(MediaAction::kNextTrack));
  TEST_ASSERT_EQUAL_HEX16(0x00B6, mediaActionUsage(MediaAction::kPreviousTrack));
  TEST_ASSERT_EQUAL_HEX16(0x0000, mediaActionUsage(MediaAction::kNone));
}

void test_media_action_names() {
  TEST_ASSERT_EQUAL_STRING("volume-up", mediaActionName(MediaAction::kVolumeUp));
  TEST_ASSERT_EQUAL_STRING("volume-down", mediaActionName(MediaAction::kVolumeDown));
  TEST_ASSERT_EQUAL_STRING("play-pause", mediaActionName(MediaAction::kPlayPause));
  TEST_ASSERT_EQUAL_STRING("next-track", mediaActionName(MediaAction::kNextTrack));
  TEST_ASSERT_EQUAL_STRING("previous-track",
                           mediaActionName(MediaAction::kPreviousTrack));
  TEST_ASSERT_EQUAL_STRING("none", mediaActionName(MediaAction::kNone));
}

void test_encoder_and_buttons_map_to_actions() {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(MediaAction::kVolumeUp),
                        static_cast<int>(mediaActionForEncoder(1)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(MediaAction::kVolumeDown),
                        static_cast<int>(mediaActionForEncoder(-1)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(MediaAction::kNone),
                        static_cast<int>(mediaActionForEncoder(0)));

  TEST_ASSERT_EQUAL_INT(static_cast<int>(MediaAction::kPlayPause),
                        static_cast<int>(mediaActionForButtons(true, true, true)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(MediaAction::kNextTrack),
                        static_cast<int>(mediaActionForButtons(false, true, true)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(MediaAction::kPreviousTrack),
                        static_cast<int>(mediaActionForButtons(false, false, true)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(MediaAction::kNone),
                        static_cast<int>(mediaActionForButtons(false, false, false)));
}

void test_hid_report_is_two_byte_little_endian() {
  uint8_t report[2] = {0, 0};
  TEST_ASSERT_EQUAL_UINT32(2, static_cast<uint32_t>(
                                  encodeHidConsumerReport(MediaAction::kPlayPause,
                                                          report, sizeof(report))));
  TEST_ASSERT_EQUAL_HEX8(0xCD, report[0]);
  TEST_ASSERT_EQUAL_HEX8(0x00, report[1]);

  encodeHidConsumerReport(MediaAction::kVolumeUp, report, sizeof(report));
  TEST_ASSERT_EQUAL_HEX8(0xE9, report[0]);
  TEST_ASSERT_EQUAL_HEX8(0x00, report[1]);

  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(
                                  encodeHidConsumerReport(MediaAction::kVolumeUp,
                                                          report, 1)));
}

void test_temperature_encoding_rounds_and_clamps() {
  uint8_t out[2] = {0, 0};
  TEST_ASSERT_EQUAL_UINT32(2, static_cast<uint32_t>(
                                  encodeBleTemperature(23.45f, out, sizeof(out))));
  TEST_ASSERT_EQUAL_HEX8(0x29, out[0]);
  TEST_ASSERT_EQUAL_HEX8(0x09, out[1]);

  encodeBleTemperature(-5.5f, out, sizeof(out));
  TEST_ASSERT_EQUAL_HEX8(0xDA, out[0]);
  TEST_ASSERT_EQUAL_HEX8(0xFD, out[1]);

  encodeBleTemperature(400.0f, out, sizeof(out));
  TEST_ASSERT_EQUAL_HEX8(0xFF, out[0]);
  TEST_ASSERT_EQUAL_HEX8(0x7F, out[1]);

  encodeBleTemperature(-400.0f, out, sizeof(out));
  TEST_ASSERT_EQUAL_HEX8(0x00, out[0]);
  TEST_ASSERT_EQUAL_HEX8(0x80, out[1]);

  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(
                                  encodeBleTemperature(20.0f, out, 1)));
}

void test_humidity_encoding_clamps() {
  uint8_t out[2] = {0, 0};
  TEST_ASSERT_EQUAL_UINT32(2, static_cast<uint32_t>(
                                  encodeBleHumidity(45.04f, out, sizeof(out))));
  TEST_ASSERT_EQUAL_HEX8(0x98, out[0]);
  TEST_ASSERT_EQUAL_HEX8(0x11, out[1]);

  encodeBleHumidity(150.0f, out, sizeof(out));
  TEST_ASSERT_EQUAL_HEX8(0x10, out[0]);
  TEST_ASSERT_EQUAL_HEX8(0x27, out[1]);

  encodeBleHumidity(-5.0f, out, sizeof(out));
  TEST_ASSERT_EQUAL_HEX8(0x00, out[0]);
  TEST_ASSERT_EQUAL_HEX8(0x00, out[1]);
}

void test_pressure_encoding_is_01pa_little_endian() {
  uint8_t out[4] = {0, 0, 0, 0};
  TEST_ASSERT_EQUAL_UINT32(4, static_cast<uint32_t>(
                                  encodeBlePressure(1006.5f, out, sizeof(out))));
  TEST_ASSERT_EQUAL_HEX8(0xA4, out[0]);
  TEST_ASSERT_EQUAL_HEX8(0x5B, out[1]);
  TEST_ASSERT_EQUAL_HEX8(0x0F, out[2]);
  TEST_ASSERT_EQUAL_HEX8(0x00, out[3]);

  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(
                                  encodeBlePressure(1000.0f, out, 3)));
}

void test_status_string() {
  char out[48];
  const SensorSnapshot sensors = healthySensors();
  formatBleStatus(sensors, false, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("T=23.4,H=45.0,P=1006.5,A=0", out);

  formatBleStatus(sensors, true, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("T=23.4,H=45.0,P=1006.5,A=1", out);

  SensorSnapshot empty;
  formatBleStatus(empty, false, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("T=-,H=-,P=-,A=0", out);
}

void test_status_string_truncation_reports_needed_length() {
  char tiny[8];
  const size_t needed = formatBleStatus(healthySensors(), true, tiny, sizeof(tiny));
  TEST_ASSERT_TRUE(needed > sizeof(tiny) - 1);
  TEST_ASSERT_EQUAL_UINT32(sizeof(tiny) - 1, static_cast<uint32_t>(std::strlen(tiny)));
}

}  // namespace

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_media_actions_map_to_consumer_usages);
  RUN_TEST(test_media_action_names);
  RUN_TEST(test_encoder_and_buttons_map_to_actions);
  RUN_TEST(test_hid_report_is_two_byte_little_endian);
  RUN_TEST(test_temperature_encoding_rounds_and_clamps);
  RUN_TEST(test_humidity_encoding_clamps);
  RUN_TEST(test_pressure_encoding_is_01pa_little_endian);
  RUN_TEST(test_status_string);
  RUN_TEST(test_status_string_truncation_reports_needed_length);
  return UNITY_END();
}

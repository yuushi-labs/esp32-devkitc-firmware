#include <unity.h>

#include <cstdint>
#include <cstring>

#include "sensors/dht.h"

void setUp() {}
void tearDown() {}

namespace {

using fw::DhtFrame;
using fw::DhtReading;
using fw::DhtType;

void buildPulses(const uint8_t bytes[5], uint32_t* pulses) {
  for (size_t bit = 0; bit < 40; ++bit) {
    const uint8_t mask = static_cast<uint8_t>(0x80u >> (bit % 8));
    pulses[bit] = (bytes[bit / 8] & mask) != 0 ? 70 : 27;
  }
}

void buildFrame(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3, uint8_t* out) {
  out[0] = b0;
  out[1] = b1;
  out[2] = b2;
  out[3] = b3;
  out[4] = static_cast<uint8_t>(b0 + b1 + b2 + b3);
}

void test_dht22_frame_decodes_humidity_and_temperature() {
  uint8_t bytes[5];
  buildFrame(0x01, 0xC2, 0x00, 0xEA, bytes);  // 45.0 % / 23.4 C
  uint32_t pulses[40];
  buildPulses(bytes, pulses);

  DhtReading reading;
  TEST_ASSERT_TRUE(fw::decodeDhtFrame(DhtType::kDht22, pulses, 40, &reading));
  TEST_ASSERT_TRUE(reading.valid);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 45.0f, reading.humidityPct);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 23.4f, reading.temperatureC);
}

void test_dht22_negative_temperature() {
  uint8_t bytes[5];
  buildFrame(0x02, 0x26, 0x80, 0x37, bytes);  // 55.0 % / -5.5 C
  uint32_t pulses[40];
  buildPulses(bytes, pulses);

  DhtReading reading;
  TEST_ASSERT_TRUE(fw::decodeDhtFrame(DhtType::kDht22, pulses, 40, &reading));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -5.5f, reading.temperatureC);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 55.0f, reading.humidityPct);
}

void test_dht11_frame_decodes_integer_fields() {
  uint8_t bytes[5];
  buildFrame(40, 0, 24, 0, bytes);
  uint32_t pulses[40];
  buildPulses(bytes, pulses);

  DhtReading reading;
  TEST_ASSERT_TRUE(fw::decodeDhtFrame(DhtType::kDht11, pulses, 40, &reading));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 40.0f, reading.humidityPct);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 24.0f, reading.temperatureC);
}

void test_dht11_negative_temperature_uses_the_sign_bit() {
  // 35 00 05 80: 35 % RH, -5.0 C (DHT11 sign flag in the decimal byte).
  uint8_t bytes[5];
  buildFrame(0x23, 0x00, 0x05, 0x80, bytes);
  uint32_t pulses[40];
  buildPulses(bytes, pulses);

  DhtReading reading;
  TEST_ASSERT_TRUE(fw::decodeDhtFrame(DhtType::kDht11, pulses, 40, &reading));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 35.0f, reading.humidityPct);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -5.0f, reading.temperatureC);

  // Auto detection does not fall back to the DHT22 reading of the same frame.
  const DhtFrame frame = fw::decodeDhtFrameAuto(pulses, 40);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(DhtType::kDht11), static_cast<int>(frame.type));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -5.0f, frame.reading.temperatureC);
}

void test_dht11_accepts_the_datasheet_temperature_range() {
  DhtReading reading;
  reading.valid = true;
  reading.humidityPct = 53.0f;
  reading.temperatureC = -15.0f;
  TEST_ASSERT_TRUE(fw::dhtReadingPlausible(DhtType::kDht11, reading));

  reading.temperatureC = -21.0f;
  TEST_ASSERT_FALSE(fw::dhtReadingPlausible(DhtType::kDht11, reading));
}

void test_checksum_mismatch_is_rejected() {
  uint8_t bytes[5];
  buildFrame(0x01, 0xC2, 0x00, 0xEA, bytes);
  bytes[4] = static_cast<uint8_t>(bytes[4] + 1);
  uint32_t pulses[40];
  buildPulses(bytes, pulses);

  DhtReading reading;
  TEST_ASSERT_FALSE(fw::decodeDhtFrame(DhtType::kDht22, pulses, 40, &reading));
  TEST_ASSERT_FALSE(reading.valid);
}

void test_short_frame_is_rejected() {
  uint32_t pulses[39] = {0};
  DhtReading reading;
  TEST_ASSERT_FALSE(fw::decodeDhtFrame(DhtType::kDht22, pulses, 39, &reading));
  TEST_ASSERT_FALSE(fw::decodeDhtFrame(DhtType::kDht22, nullptr, 40, &reading));
}

void test_out_of_range_humidity_is_rejected() {
  // 110.0 % cannot come from a working DHT22.
  uint8_t bytes[5];
  buildFrame(0x04, 0x4C, 0x00, 0xEA, bytes);
  uint32_t pulses[40];
  buildPulses(bytes, pulses);

  DhtReading reading;
  TEST_ASSERT_FALSE(fw::decodeDhtFrame(DhtType::kDht22, pulses, 40, &reading));
}

void test_auto_detection_picks_the_right_family() {
  uint8_t dht22[5];
  buildFrame(0x01, 0xC2, 0x00, 0xEA, dht22);
  uint32_t pulses[40];
  buildPulses(dht22, pulses);

  const DhtFrame frame = fw::decodeDhtFrameAuto(pulses, 40);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(DhtType::kDht22), static_cast<int>(frame.type));
  TEST_ASSERT_TRUE(frame.reading.valid);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 45.0f, frame.reading.humidityPct);

  uint8_t dht11[5];
  buildFrame(40, 0, 24, 0, dht11);
  buildPulses(dht11, pulses);

  const DhtFrame legacy = fw::decodeDhtFrameAuto(pulses, 40);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(DhtType::kDht11), static_cast<int>(legacy.type));
  TEST_ASSERT_TRUE(legacy.reading.valid);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 24.0f, legacy.reading.temperatureC);
}

void test_auto_detection_reports_unknown_for_a_broken_frame() {
  uint32_t pulses[40];
  for (size_t index = 0; index < 40; ++index) {
    pulses[index] = 5;  // all zero bits, checksum byte 0 -> valid 0 %/0 C frame
  }
  pulses[0] = 70;
  pulses[39] = 70;  // break the checksum

  const DhtFrame frame = fw::decodeDhtFrameAuto(pulses, 40);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(DhtType::kUnknown), static_cast<int>(frame.type));
  TEST_ASSERT_FALSE(frame.reading.valid);
}

void test_type_names_and_plausibility() {
  TEST_ASSERT_EQUAL_STRING("DHT11", fw::dhtTypeName(DhtType::kDht11));
  TEST_ASSERT_EQUAL_STRING("DHT22", fw::dhtTypeName(DhtType::kDht22));
  TEST_ASSERT_EQUAL_STRING("unknown", fw::dhtTypeName(DhtType::kUnknown));

  DhtReading reading;
  reading.valid = true;
  reading.humidityPct = 50.0f;
  reading.temperatureC = 20.0f;
  TEST_ASSERT_TRUE(fw::dhtReadingPlausible(DhtType::kDht11, reading));
  TEST_ASSERT_TRUE(fw::dhtReadingPlausible(DhtType::kDht22, reading));

  reading.temperatureC = 70.0f;
  TEST_ASSERT_FALSE(fw::dhtReadingPlausible(DhtType::kDht11, reading));
  TEST_ASSERT_TRUE(fw::dhtReadingPlausible(DhtType::kDht22, reading));

  reading.temperatureC = 90.0f;
  TEST_ASSERT_FALSE(fw::dhtReadingPlausible(DhtType::kDht22, reading));

  reading.valid = false;
  TEST_ASSERT_FALSE(fw::dhtReadingPlausible(DhtType::kDht22, reading));
}

}  // namespace

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_dht22_frame_decodes_humidity_and_temperature);
  RUN_TEST(test_dht22_negative_temperature);
  RUN_TEST(test_dht11_frame_decodes_integer_fields);
  RUN_TEST(test_dht11_negative_temperature_uses_the_sign_bit);
  RUN_TEST(test_dht11_accepts_the_datasheet_temperature_range);
  RUN_TEST(test_checksum_mismatch_is_rejected);
  RUN_TEST(test_short_frame_is_rejected);
  RUN_TEST(test_out_of_range_humidity_is_rejected);
  RUN_TEST(test_auto_detection_picks_the_right_family);
  RUN_TEST(test_auto_detection_reports_unknown_for_a_broken_frame);
  RUN_TEST(test_type_names_and_plausibility);
  return UNITY_END();
}

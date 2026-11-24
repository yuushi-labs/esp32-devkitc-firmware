#include <unity.h>

#include <cmath>
#include <cstring>
#include <vector>

#include "sensors/bmp280.h"
#include "test_support/fake_i2c.h"

void setUp() {}
void tearDown() {}

namespace {

using fw::Bmp280;
using fw::Bmp280Calibration;
using fw::Bmp280Reading;
using fw::Bmp280Status;
using fw::test::FakeI2cTransport;

// Calibration table from the BME280 datasheet compensation example, which is
// valid for the BMP280 formulas as well.
struct ExampleCalibration {
  uint16_t t1 = 27504;
  int16_t t2 = 26435;
  int16_t t3 = -1000;
  uint16_t p1 = 36477;
  int16_t p2 = -10685;
  int16_t p3 = 3024;
  int16_t p4 = 2855;
  int16_t p5 = 140;
  int16_t p6 = -7;
  int16_t p7 = 15500;
  int16_t p8 = -14600;
  int16_t p9 = 6000;
};

// 519888 / 415148 with this table give 25.08 C and 100653 Pa in the datasheet.
constexpr int32_t kExampleRawTemperature = 519888;
constexpr int32_t kExampleRawPressure = 415148;

void putUint16(uint8_t* out, uint16_t value) {
  out[0] = static_cast<uint8_t>(value & 0xFF);
  out[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

void putInt16(uint8_t* out, int16_t value) {
  putUint16(out, static_cast<uint16_t>(value));
}

void writeCalibration(FakeI2cTransport& bus, uint8_t address,
                      const ExampleCalibration& calibration) {
  uint8_t raw[fw::kBmp280CalibrationLength] = {0};
  putUint16(raw + 0, calibration.t1);
  putInt16(raw + 2, calibration.t2);
  putInt16(raw + 4, calibration.t3);
  putUint16(raw + 6, calibration.p1);
  putInt16(raw + 8, calibration.p2);
  putInt16(raw + 10, calibration.p3);
  putInt16(raw + 12, calibration.p4);
  putInt16(raw + 14, calibration.p5);
  putInt16(raw + 16, calibration.p6);
  putInt16(raw + 18, calibration.p7);
  putInt16(raw + 20, calibration.p8);
  putInt16(raw + 22, calibration.p9);
  bus.setRegisters(address, fw::kBmp280CalibrationRegister, raw, sizeof(raw));
}

void writeSample(FakeI2cTransport& bus, uint8_t address, int32_t rawTemperature,
                 int32_t rawPressure) {
  const uint8_t data[6] = {
      static_cast<uint8_t>((rawPressure >> 12) & 0xFF),
      static_cast<uint8_t>((rawPressure >> 4) & 0xFF),
      static_cast<uint8_t>((rawPressure & 0x0F) << 4),
      static_cast<uint8_t>((rawTemperature >> 12) & 0xFF),
      static_cast<uint8_t>((rawTemperature >> 4) & 0xFF),
      static_cast<uint8_t>((rawTemperature & 0x0F) << 4),
  };
  bus.setRegisters(address, fw::kBmp280DataRegister, data, sizeof(data));
}

void installSensor(FakeI2cTransport& bus, uint8_t address = 0x76,
                   uint8_t chipId = 0x58) {
  bus.addDevice(address);
  bus.setRegister(address, fw::kBmp280ChipIdRegister, chipId);
  writeCalibration(bus, address, ExampleCalibration{});
  writeSample(bus, address, kExampleRawTemperature, kExampleRawPressure);
}

void test_calibration_is_decoded_little_endian() {
  uint8_t raw[fw::kBmp280CalibrationLength] = {0};
  putUint16(raw + 0, 27504);
  putInt16(raw + 2, -1234);
  putInt16(raw + 4, 567);
  putUint16(raw + 6, 36000);
  putInt16(raw + 22, -14600);

  Bmp280Calibration calibration;
  TEST_ASSERT_TRUE(fw::bmp280CalibrationFromRegisters(raw, sizeof(raw), &calibration));
  TEST_ASSERT_EQUAL_UINT16(27504, calibration.t1);
  TEST_ASSERT_EQUAL_INT16(-1234, calibration.t2);
  TEST_ASSERT_EQUAL_INT16(567, calibration.t3);
  TEST_ASSERT_EQUAL_UINT16(36000, calibration.p1);
  TEST_ASSERT_EQUAL_INT16(-14600, calibration.p9);
}

void test_erased_calibration_is_rejected() {
  uint8_t zeros[fw::kBmp280CalibrationLength] = {0};
  Bmp280Calibration calibration;
  TEST_ASSERT_FALSE(fw::bmp280CalibrationFromRegisters(zeros, sizeof(zeros), &calibration));

  uint8_t ff[fw::kBmp280CalibrationLength];
  std::memset(ff, 0xFF, sizeof(ff));
  TEST_ASSERT_FALSE(fw::bmp280CalibrationFromRegisters(ff, sizeof(ff), &calibration));

  uint8_t shortBuffer[4] = {1, 2, 3, 4};
  TEST_ASSERT_FALSE(fw::bmp280CalibrationFromRegisters(shortBuffer, sizeof(shortBuffer),
                                                       &calibration));
  TEST_ASSERT_FALSE(fw::bmp280CalibrationFromRegisters(nullptr, 24, &calibration));
}

void test_compensation_matches_the_datasheet_example() {
  Bmp280Calibration calibration;
  uint8_t raw[fw::kBmp280CalibrationLength] = {0};
  FakeI2cTransport scratch;
  scratch.addDevice(0x76);
  writeCalibration(scratch, 0x76, ExampleCalibration{});
  for (size_t index = 0; index < sizeof(raw); ++index) {
    raw[index] = scratch.registerValue(
        0x76, static_cast<uint8_t>(fw::kBmp280CalibrationRegister + index));
  }
  TEST_ASSERT_TRUE(fw::bmp280CalibrationFromRegisters(raw, sizeof(raw), &calibration));

  int32_t fine = 0;
  const int32_t temperatureCenti =
      fw::bmp280CompensateTemperature(kExampleRawTemperature, calibration, &fine);
  TEST_ASSERT_EQUAL_INT32(2508, temperatureCenti);
  TEST_ASSERT_EQUAL_INT32(128422, fine);

  const uint32_t pressureQ24 =
      fw::bmp280CompensatePressureQ24(kExampleRawPressure, calibration, fine);
  TEST_ASSERT_EQUAL_UINT32(25767233u, pressureQ24);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1006.53f, fw::bmp280PressureHpa(pressureQ24));
}

void test_begin_detects_the_sensor_and_configures_it() {
  FakeI2cTransport bus;
  installSensor(bus);
  Bmp280 sensor(bus, 0x76);

  TEST_ASSERT_EQUAL_INT(static_cast<int>(Bmp280Status::kOk),
                        static_cast<int>(sensor.begin()));
  TEST_ASSERT_TRUE(sensor.present());
  TEST_ASSERT_EQUAL_STRING("BMP280", sensor.chipName());
  TEST_ASSERT_EQUAL_UINT8(0x58, sensor.chipId());

  // Normal mode with x1 oversampling on both channels, 500 ms standby.
  TEST_ASSERT_EQUAL_UINT8(0x27, bus.registerValue(0x76, fw::kBmp280CtrlMeasRegister));
  TEST_ASSERT_EQUAL_UINT8(0x80, bus.registerValue(0x76, fw::kBmp280ConfigRegister));

  // The configuration writes must follow sleep -> config -> normal so the
  // config register is not ignored while a conversion is running.
  std::vector<uint8_t> registers;
  std::vector<uint8_t> values;
  for (const std::vector<uint8_t>& write : bus.writeLog) {
    if (write.size() == 2 &&
        (write[0] == fw::kBmp280CtrlMeasRegister ||
         write[0] == fw::kBmp280ConfigRegister)) {
      registers.push_back(write[0]);
      values.push_back(write[1]);
    }
  }
  TEST_ASSERT_EQUAL_UINT32(3, static_cast<uint32_t>(registers.size()));
  TEST_ASSERT_EQUAL_UINT8(fw::kBmp280CtrlMeasRegister, registers[0]);
  TEST_ASSERT_EQUAL_UINT8(0x00, values[0]);  // sleep
  TEST_ASSERT_EQUAL_UINT8(fw::kBmp280ConfigRegister, registers[1]);
  TEST_ASSERT_EQUAL_UINT8(0x80, values[1]);  // 500 ms standby
  TEST_ASSERT_EQUAL_UINT8(fw::kBmp280CtrlMeasRegister, registers[2]);
  TEST_ASSERT_EQUAL_UINT8(0x27, values[2]);  // normal
}

void test_begin_reports_a_missing_sensor() {
  FakeI2cTransport bus;
  Bmp280 sensor(bus, 0x76);

  TEST_ASSERT_EQUAL_INT(static_cast<int>(Bmp280Status::kNotPresent),
                        static_cast<int>(sensor.begin()));
  TEST_ASSERT_FALSE(sensor.present());
  TEST_ASSERT_EQUAL_STRING("not found", fw::bmp280StatusName(Bmp280Status::kNotPresent));
}

void test_begin_rejects_an_unexpected_chip_id() {
  FakeI2cTransport bus;
  bus.addDevice(0x76);
  bus.setRegister(0x76, fw::kBmp280ChipIdRegister, 0x55);  // BMP180
  Bmp280 sensor(bus, 0x76);

  TEST_ASSERT_EQUAL_INT(static_cast<int>(Bmp280Status::kBadChipId),
                        static_cast<int>(sensor.begin()));
  TEST_ASSERT_EQUAL_STRING("BMP180", sensor.chipName());
  TEST_ASSERT_EQUAL_STRING("unexpected chip id",
                           fw::bmp280StatusName(Bmp280Status::kBadChipId));
}

void test_begin_reports_a_bus_error_when_calibration_is_unreadable() {
  FakeI2cTransport bus;
  installSensor(bus);
  bus.failReads = true;
  Bmp280 sensor(bus, 0x76);

  TEST_ASSERT_EQUAL_INT(static_cast<int>(Bmp280Status::kBusError),
                        static_cast<int>(sensor.begin()));
}

void test_bme280_is_accepted() {
  FakeI2cTransport bus;
  installSensor(bus, 0x77, 0x60);
  Bmp280 sensor(bus, 0x77);

  TEST_ASSERT_EQUAL_INT(static_cast<int>(Bmp280Status::kOk),
                        static_cast<int>(sensor.begin()));
  TEST_ASSERT_EQUAL_STRING("BME280", sensor.chipName());
}

void test_read_returns_temperature_and_pressure() {
  FakeI2cTransport bus;
  installSensor(bus);
  Bmp280 sensor(bus, 0x76);
  sensor.begin();

  Bmp280Reading reading;
  TEST_ASSERT_TRUE(sensor.read(&reading));
  TEST_ASSERT_TRUE(reading.valid);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.08f, reading.temperatureC);
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 1006.53f, reading.pressureHpa);
  TEST_ASSERT_EQUAL_INT32(kExampleRawTemperature, reading.rawTemperature);
  TEST_ASSERT_EQUAL_INT32(kExampleRawPressure, reading.rawPressure);
}

void test_read_rejects_the_no_measurement_marker() {
  FakeI2cTransport bus;
  installSensor(bus);
  writeSample(bus, 0x76, 0x80000, 0x80000);
  Bmp280 sensor(bus, 0x76);
  sensor.begin();

  Bmp280Reading reading;
  TEST_ASSERT_FALSE(sensor.read(&reading));
  TEST_ASSERT_FALSE(reading.valid);
}

void test_read_fails_cleanly_without_a_sensor_or_on_a_bus_error() {
  FakeI2cTransport emptyBus;
  Bmp280 missing(emptyBus, 0x76);
  Bmp280Reading reading;
  TEST_ASSERT_FALSE(missing.read(&reading));

  FakeI2cTransport bus;
  installSensor(bus);
  Bmp280 sensor(bus, 0x76);
  sensor.begin();
  bus.failReads = true;
  TEST_ASSERT_FALSE(sensor.read(&reading));
  // A lost sensor counts as absent so the caller reinitialises it.
  TEST_ASSERT_FALSE(sensor.present());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Bmp280Status::kBusError),
                        static_cast<int>(sensor.status()));

  bus.failReads = false;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Bmp280Status::kOk),
                        static_cast<int>(sensor.begin(0x76)));
  TEST_ASSERT_TRUE(sensor.read(&reading));
}

void test_find_bmp280_address_prefers_0x76_then_0x77() {
  FakeI2cTransport none;
  TEST_ASSERT_EQUAL_UINT8(0, fw::findBmp280Address(none));

  FakeI2cTransport alternate;
  installSensor(alternate, 0x77, 0x60);
  TEST_ASSERT_EQUAL_UINT8(0x77, fw::findBmp280Address(alternate));

  FakeI2cTransport both;
  installSensor(both, 0x76, 0x58);
  installSensor(both, 0x77, 0x58);
  TEST_ASSERT_EQUAL_UINT8(0x76, fw::findBmp280Address(both));

  FakeI2cTransport wrongChip;
  wrongChip.addDevice(0x76);
  wrongChip.setRegister(0x76, fw::kBmp280ChipIdRegister, 0x11);
  TEST_ASSERT_EQUAL_UINT8(0, fw::findBmp280Address(wrongChip));
}

void test_pressure_conversion_handles_the_bounds() {
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, fw::bmp280PressureHpa(0));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1013.25f, fw::bmp280PressureHpa(25939200u));
}

}  // namespace

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_calibration_is_decoded_little_endian);
  RUN_TEST(test_erased_calibration_is_rejected);
  RUN_TEST(test_compensation_matches_the_datasheet_example);
  RUN_TEST(test_begin_detects_the_sensor_and_configures_it);
  RUN_TEST(test_begin_reports_a_missing_sensor);
  RUN_TEST(test_begin_rejects_an_unexpected_chip_id);
  RUN_TEST(test_begin_reports_a_bus_error_when_calibration_is_unreadable);
  RUN_TEST(test_bme280_is_accepted);
  RUN_TEST(test_read_returns_temperature_and_pressure);
  RUN_TEST(test_read_rejects_the_no_measurement_marker);
  RUN_TEST(test_read_fails_cleanly_without_a_sensor_or_on_a_bus_error);
  RUN_TEST(test_find_bmp280_address_prefers_0x76_then_0x77);
  RUN_TEST(test_pressure_conversion_handles_the_bounds);
  return UNITY_END();
}

// Bosch BMP280 / BME280 pressure sensor driver (I2C).
//
// The driver is deliberately library free: it talks to the sensor through the
// I2cTransport interface, keeps the calibration table, and applies the integer
// compensation formulas from the BMP280 datasheet (section 3.11 / 8.1). That
// makes the whole thing testable on the host with a fake register file.
#pragma once

#include <cstddef>
#include <cstdint>

#include "i2c_transport.h"

namespace fw {

enum class Bmp280Status {
  kOk,
  kNotPresent,
  kBadChipId,
  kBusError,
};

const char* bmp280StatusName(Bmp280Status status);

// Registers (see the BMP280 datasheet, register map).
constexpr uint8_t kBmp280ChipIdRegister = 0xD0;
constexpr uint8_t kBmp280ChipIdBmp280 = 0x58;
constexpr uint8_t kBmp280ChipIdBme280 = 0x60;
constexpr uint8_t kBmp280ChipIdBmp180 = 0x55;
constexpr uint8_t kBmp280ResetRegister = 0xE0;
constexpr uint8_t kBmp280CalibrationRegister = 0x88;
constexpr uint8_t kBmp280DataRegister = 0xF7;
constexpr uint8_t kBmp280CtrlMeasRegister = 0xF4;
constexpr uint8_t kBmp280ConfigRegister = 0xF5;
constexpr size_t kBmp280CalibrationLength = 24;

// Name of a sensor by chip id ("BMP280", "BME280", "BMP180", "unknown").
const char* bmp280ChipName(uint8_t chipId);

struct Bmp280Calibration {
  uint16_t t1 = 0;
  int16_t t2 = 0;
  int16_t t3 = 0;
  uint16_t p1 = 0;
  int16_t p2 = 0;
  int16_t p3 = 0;
  int16_t p4 = 0;
  int16_t p5 = 0;
  int16_t p6 = 0;
  int16_t p7 = 0;
  int16_t p8 = 0;
  int16_t p9 = 0;
};

// Little endian calibration block read from 0x88 (24 bytes, dig_T1..dig_P9).
bool bmp280CalibrationFromRegisters(const uint8_t* raw, size_t length,
                                    Bmp280Calibration* out);

// Returns the temperature in 0.01 degC steps and writes the fine value used by
// the pressure compensation.
int32_t bmp280CompensateTemperature(int32_t adcTemperature,
                                    const Bmp280Calibration& calibration,
                                    int32_t* temperatureFine);

// Returns the pressure in Q24.8 format (Pa * 256), as the datasheet code does.
uint32_t bmp280CompensatePressureQ24(int32_t adcPressure,
                                    const Bmp280Calibration& calibration,
                                    int32_t temperatureFine);

// Q24.8 pascal value -> hectopascal.
float bmp280PressureHpa(uint32_t pressureQ24);

struct Bmp280Reading {
  bool valid = false;
  float temperatureC = 0.0f;
  float pressureHpa = 0.0f;
  int32_t rawTemperature = 0;
  int32_t rawPressure = 0;
};

class Bmp280 {
 public:
  Bmp280(I2cTransport& bus, uint8_t address);

  // Probes the address, checks the chip id and reads the calibration table.
  Bmp280Status begin();
  // Same, but switches to another address first (0x76/0x77 auto detection).
  Bmp280Status begin(uint8_t address);

  // Reads one measurement. Returns false (and leaves testmode samples out) when
  // the sensor is missing, the bus fails or the sample is the 0x80000 "no
  // measurement yet" marker.
  bool read(Bmp280Reading* out);

  Bmp280Status status() const { return status_; }
  bool present() const { return status_ == Bmp280Status::kOk; }
  uint8_t address() const { return address_; }
  uint8_t chipId() const { return chipId_; }
  const char* chipName() const { return bmp280ChipName(chipId_); }
  const Bmp280Calibration& calibration() const { return calibration_; }

 private:
  I2cTransport& bus_;
  uint8_t address_;
  uint8_t chipId_ = 0;
  Bmp280Status status_ = Bmp280Status::kNotPresent;
  Bmp280Calibration calibration_{};
};

// Probes 0x76 then 0x77 and returns the address of a BMP280/BME280, or 0 when
// neither answers with a known chip id.
uint8_t findBmp280Address(I2cTransport& bus);

}  // namespace fw

#include "sensors/bmp280.h"

#include "app_config.h"

namespace fw {
namespace {

// osrs_t = x1, osrs_p = x1, mode = normal (datasheet 4.3.4 / 4.3.3).
constexpr uint8_t kCtrlMeasNormal = 0x27;
// t_sb = 100 (500 ms), IIR filter off, 4 wire SPI off. Only the first two bits
// matter for I2C.
constexpr uint8_t kConfigStandby500ms = 0x80;

// Mode bits (ctrl_meas[1:0]) - sleep before a config write, normal afterwards.
constexpr uint8_t kCtrlMeasSleep = 0x00;

// Both sensors report 0x80000 until the first conversion finished.
constexpr int32_t kNoMeasurementMarker = 0x80000;

uint16_t readUint16(const uint8_t* raw) {
  return static_cast<uint16_t>(raw[0] | (static_cast<uint16_t>(raw[1]) << 8));
}

int16_t readInt16(const uint8_t* raw) {
  return static_cast<int16_t>(readUint16(raw));
}

}  // namespace

const char* bmp280StatusName(Bmp280Status status) {
  switch (status) {
    case Bmp280Status::kOk:
      return "ok";
    case Bmp280Status::kNotPresent:
      return "not found";
    case Bmp280Status::kBadChipId:
      return "unexpected chip id";
    case Bmp280Status::kBusError:
      return "bus error";
  }
  return "unknown";
}

const char* bmp280ChipName(uint8_t chipId) {
  switch (chipId) {
    case kBmp280ChipIdBmp280:
      return "BMP280";
    case kBmp280ChipIdBme280:
      return "BME280";
    case kBmp280ChipIdBmp180:
      return "BMP180";
    case 0x00:
    case 0xFF:
      return "unknown";
    default:
      break;
  }
  return "unknown";
}

bool bmp280CalibrationFromRegisters(const uint8_t* raw, size_t length,
                                    Bmp280Calibration* out) {
  if (raw == nullptr || out == nullptr || length < kBmp280CalibrationLength) {
    return false;
  }

  Bmp280Calibration calibration;
  calibration.t1 = readUint16(raw + 0);
  calibration.t2 = readInt16(raw + 2);
  calibration.t3 = readInt16(raw + 4);
  calibration.p1 = readUint16(raw + 6);
  calibration.p2 = readInt16(raw + 8);
  calibration.p3 = readInt16(raw + 10);
  calibration.p4 = readInt16(raw + 12);
  calibration.p5 = readInt16(raw + 14);
  calibration.p6 = readInt16(raw + 16);
  calibration.p7 = readInt16(raw + 18);
  calibration.p8 = readInt16(raw + 20);
  calibration.p9 = readInt16(raw + 22);

  // An erased (0x00 / 0xFF) calibration block cannot produce sane values.
  bool allZero = true;
  bool allOnes = true;
  for (size_t index = 0; index < kBmp280CalibrationLength; ++index) {
    if (raw[index] != 0x00) {
      allZero = false;
    }
    if (raw[index] != 0xFF) {
      allOnes = false;
    }
  }
  if (allZero || allOnes || calibration.t1 == 0 || calibration.p1 == 0) {
    return false;
  }

  *out = calibration;
  return true;
}

int32_t bmp280CompensateTemperature(int32_t adcTemperature,
                                    const Bmp280Calibration& calibration,
                                    int32_t* temperatureFine) {
  const int32_t t1 = static_cast<int32_t>(calibration.t1);
  const int32_t var1 =
      ((((adcTemperature >> 3) - (t1 << 1))) * static_cast<int32_t>(calibration.t2)) >> 11;
  const int32_t shifted = (adcTemperature >> 4) - t1;
  const int32_t var2 =
      (((shifted * shifted) >> 12) * static_cast<int32_t>(calibration.t3)) >> 14;
  const int32_t fine = var1 + var2;
  if (temperatureFine != nullptr) {
    *temperatureFine = fine;
  }
  return (fine * 5 + 128) >> 8;
}

uint32_t bmp280CompensatePressureQ24(int32_t adcPressure,
                                    const Bmp280Calibration& calibration,
                                    int32_t temperatureFine) {
  int64_t var1 = static_cast<int64_t>(temperatureFine) - 128000;
  int64_t var2 = var1 * var1 * static_cast<int64_t>(calibration.p6);
  // Multiplication instead of left shifts: var1 and the calibration terms can
  // be negative, and shifting a negative value is undefined behaviour.
  var2 += var1 * static_cast<int64_t>(calibration.p5) * 131072;  // x << 17
  var2 += static_cast<int64_t>(calibration.p4) * 34359738368LL;  // x << 35
  var1 = ((var1 * var1 * static_cast<int64_t>(calibration.p3)) >> 8) +
         var1 * static_cast<int64_t>(calibration.p2) * 4096;  // x << 12
  var1 = ((static_cast<int64_t>(1) << 47) + var1) *
             static_cast<int64_t>(calibration.p1) >>
         33;
  if (var1 == 0) {
    return 0;
  }

  int64_t pressure = 1048576 - static_cast<int64_t>(adcPressure);
  pressure = ((pressure * 2147483648LL - var2) * 3125) / var1;  // x << 31
  var1 = (static_cast<int64_t>(calibration.p9) * (pressure >> 13) * (pressure >> 13)) >> 25;
  var2 = (static_cast<int64_t>(calibration.p8) * pressure) >> 19;
  pressure = ((pressure + var1 + var2) >> 8) +
             static_cast<int64_t>(calibration.p7) * 16;  // x << 4

  if (pressure <= 0) {
    return 0;
  }
  if (pressure > static_cast<int64_t>(0xFFFFFFFFu)) {
    return 0xFFFFFFFFu;
  }
  return static_cast<uint32_t>(pressure);
}

float bmp280PressureHpa(uint32_t pressureQ24) {
  // Q24.8 pascal -> pascal -> hectopascal.
  return static_cast<float>(pressureQ24) / 256.0f / 100.0f;
}

Bmp280::Bmp280(I2cTransport& bus, uint8_t address) : bus_(bus), address_(address) {}

Bmp280Status Bmp280::begin() {
  status_ = Bmp280Status::kNotPresent;
  chipId_ = 0;

  if (!bus_.probe(address_)) {
    return status_;
  }

  uint8_t chipId = 0;
  if (!readRegister(bus_, address_, kBmp280ChipIdRegister, &chipId)) {
    status_ = Bmp280Status::kBusError;
    return status_;
  }
  chipId_ = chipId;
  if (chipId != kBmp280ChipIdBmp280 && chipId != kBmp280ChipIdBme280) {
    status_ = Bmp280Status::kBadChipId;
    return status_;
  }

  uint8_t raw[kBmp280CalibrationLength] = {0};
  if (!readRegisters(bus_, address_, kBmp280CalibrationRegister, raw, sizeof(raw))) {
    status_ = Bmp280Status::kBusError;
    return status_;
  }
  if (!bmp280CalibrationFromRegisters(raw, sizeof(raw), &calibration_)) {
    status_ = Bmp280Status::kBusError;
    return status_;
  }

  // The sensor ignores config writes while a conversion is running, so go to
  // sleep first (config is accepted there), then start normal mode.
  if (!writeRegister(bus_, address_, kBmp280CtrlMeasRegister, kCtrlMeasSleep)) {
    status_ = Bmp280Status::kBusError;
    return status_;
  }
  if (!writeRegister(bus_, address_, kBmp280ConfigRegister, kConfigStandby500ms)) {
    status_ = Bmp280Status::kBusError;
    return status_;
  }
  if (!writeRegister(bus_, address_, kBmp280CtrlMeasRegister, kCtrlMeasNormal)) {
    status_ = Bmp280Status::kBusError;
    return status_;
  }

  status_ = Bmp280Status::kOk;
  return status_;
}

Bmp280Status Bmp280::begin(uint8_t address) {
  address_ = address;
  return begin();
}

bool Bmp280::read(Bmp280Reading* out) {
  if (out != nullptr) {
    *out = Bmp280Reading{};
  }
  if (status_ != Bmp280Status::kOk) {
    return false;
  }

  uint8_t data[6] = {0};
  if (!readRegisters(bus_, address_, kBmp280DataRegister, data, sizeof(data))) {
    // Treat a lost sensor as absent so the caller can reinitialise it instead
    // of polling a dead bus forever.
    status_ = Bmp280Status::kBusError;
    return false;
  }

  const int32_t rawPressure = (static_cast<int32_t>(data[0]) << 12) |
                              (static_cast<int32_t>(data[1]) << 4) |
                              (static_cast<int32_t>(data[2]) >> 4);
  const int32_t rawTemperature = (static_cast<int32_t>(data[3]) << 12) |
                                 (static_cast<int32_t>(data[4]) << 4) |
                                 (static_cast<int32_t>(data[5]) >> 4);

  if (rawPressure == kNoMeasurementMarker || rawTemperature == kNoMeasurementMarker) {
    return false;
  }

  int32_t fine = 0;
  const int32_t temperatureCenti =
      bmp280CompensateTemperature(rawTemperature, calibration_, &fine);
  const uint32_t pressureQ24 =
      bmp280CompensatePressureQ24(rawPressure, calibration_, fine);

  Bmp280Reading reading;
  reading.valid = true;
  reading.temperatureC = static_cast<float>(temperatureCenti) / 100.0f;
  reading.pressureHpa = bmp280PressureHpa(pressureQ24);
  reading.rawTemperature = rawTemperature;
  reading.rawPressure = rawPressure;

  if (out != nullptr) {
    *out = reading;
  }
  return true;
}

uint8_t findBmp280Address(I2cTransport& bus) {
  const uint8_t candidates[] = {kBmp280PrimaryAddress, kBmp280AlternateAddress};
  for (uint8_t address : candidates) {
    if (!bus.probe(address)) {
      continue;
    }
    uint8_t chipId = 0;
    if (!readRegister(bus, address, kBmp280ChipIdRegister, &chipId)) {
      continue;
    }
    if (chipId == kBmp280ChipIdBmp280 || chipId == kBmp280ChipIdBme280) {
      return address;
    }
  }
  return 0;
}

}  // namespace fw

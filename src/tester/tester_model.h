// Parts tester: the state shown on the panel and printed to the serial port.
//
// The tester firmware exists to answer "is this part wired and alive?" at a
// glance: every I2C address on the bus, the BMP280 chip id and readings, the
// DHT response, and a live counter for each of the four inputs.
#pragma once

#include <cstddef>
#include <cstdint>

#include "display/page_lines.h"
#include "i2c_scan.h"
#include "sensors/bmp280.h"
#include "sensors/sensor_snapshot.h"

namespace fw {

enum class TesterPage {
  kInputs = 0,
  kScan,
  kSensors,
};

constexpr size_t kTesterPageCount = 3;

const char* testerPageName(TesterPage page);
TesterPage nextTesterPage(TesterPage page);

struct TesterInputs {
  int32_t encoderPosition = 0;
  uint32_t pushPresses = 0;
  bool pushDown = false;
  uint32_t backPresses = 0;
  bool backDown = false;
  uint32_t confirmPresses = 0;
  bool confirmDown = false;
};

struct TesterSensors {
  I2cScanResult scan;
  bool oledPresent = false;
  uint8_t oledAddress = 0;

  Bmp280Status bmp280Status = Bmp280Status::kNotPresent;
  uint8_t bmp280Address = 0;
  const char* bmp280Chip = "unknown";
  bool bmp280ReadingValid = false;
  float temperatureC = 0.0f;
  float pressureHpa = 0.0f;

  bool dhtPresent = false;
  DhtType dhtType = DhtType::kUnknown;
  bool dhtReadingValid = false;
  float humidityPct = 0.0f;
  float dhtTemperatureC = 0.0f;
};

void buildTesterPage(TesterPage page, const TesterSensors& sensors,
                     const TesterInputs& inputs, PageLines* out);

// Multi line serial reports; NUL terminated, snprintf semantics.
size_t formatTesterSensorReport(const TesterSensors& sensors, char* out, size_t outSize);
size_t formatTesterInputReport(const TesterInputs& inputs, char* out, size_t outSize);

// "PSH" / "BAK" / "CON" name for a button index.
const char* testerButtonName(size_t index);

}  // namespace fw

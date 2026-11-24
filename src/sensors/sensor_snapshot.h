// Latest sensor values shared by the display pages, the serial commands and the
// CSV log.
#pragma once

#include <cstdint>

#include "sensors/dht.h"

namespace fw {

struct SensorSnapshot {
  bool bmp280Present = false;
  const char* bmp280Chip = "unknown";
  uint8_t bmp280Address = 0;
  bool temperatureValid = false;
  float temperatureC = 0.0f;
  bool pressureValid = false;
  float pressureHpa = 0.0f;
  int8_t pressureTrend = 0;

  bool dhtPresent = false;
  DhtType dhtType = DhtType::kUnknown;
  bool humidityValid = false;
  float humidityPct = 0.0f;
  float dhtTemperatureC = 0.0f;

  bool hasTemperature() const { return temperatureValid || dhtValidTemperature(); }
  bool dhtValidTemperature() const {
    return dhtPresent && dhtType != DhtType::kUnknown && humidityValid;
  }
  // Temperature shown to the user: BMP280 first, DHT as the fallback.
  float displayTemperatureC() const {
    return temperatureValid ? temperatureC : dhtTemperatureC;
  }
  bool hasPressure() const { return pressureValid; }
  bool hasHumidity() const { return humidityValid; }
};

}  // namespace fw

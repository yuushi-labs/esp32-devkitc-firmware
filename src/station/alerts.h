// Threshold alerts shown on the OLED and mirrored by the LED.
#pragma once

#include <cstddef>
#include <cstdint>

#include "sensors/sensor_snapshot.h"

namespace fw {

struct AlertState {
  bool highTemperature = false;
  bool lowHumidity = false;
  bool pressureJump = false;

  bool any() const { return highTemperature || lowHumidity || pressureJump; }
};

// `pressureDeltaHpa` is the change over the trend window (current minus the
// reference sample); pass 0 when no reference exists.
AlertState evaluateAlerts(const SensorSnapshot& sensors, float pressureDeltaHpa);

// "HOT 31.2C" / "DRY 30%" / "P 1.8hPa" joined with spaces, or "none".
size_t formatAlertLine(const AlertState& alerts, const SensorSnapshot& sensors,
                       char* out, size_t outSize);

}  // namespace fw

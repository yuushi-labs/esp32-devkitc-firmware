#include "station/alerts.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "app_config.h"

namespace fw {
namespace {

int formatInto(char* out, size_t outSize, const char* format, ...) {
  char scratch = '\0';
  va_list args;
  va_start(args, format);
  const int needed =
      std::vsnprintf(outSize > 0 ? out : &scratch, outSize, format, args);
  va_end(args);
  return needed;
}

}  // namespace

AlertState evaluateAlerts(const SensorSnapshot& sensors, float pressureDeltaHpa) {
  AlertState alerts;
  if (sensors.hasTemperature()) {
    alerts.highTemperature =
        sensors.displayTemperatureC() >= kHighTemperatureAlertC;
  }
  if (sensors.hasHumidity()) {
    alerts.lowHumidity = sensors.humidityPct <= kLowHumidityAlertPct;
  }
  // The delta is only meaningful while the sensor still answers: after a failed
  // read the last value must not keep an alert blinking.
  if (sensors.hasPressure()) {
    const float delta = pressureDeltaHpa < 0 ? -pressureDeltaHpa : pressureDeltaHpa;
    alerts.pressureJump = delta >= kPressureJumpAlertHpa;
  }
  return alerts;
}

size_t formatAlertLine(const AlertState& alerts, const SensorSnapshot& sensors,
                       char* out, size_t outSize) {
  if (!alerts.any()) {
    const int needed = formatInto(out, outSize, "none");
    return needed > 0 ? static_cast<size_t>(needed) : 0;
  }

  char text[48] = {0};
  size_t used = 0;
  auto append = [&text, &used](const char* format, ...) {
    if (used >= sizeof(text)) {
      return;
    }
    va_list args;
    va_start(args, format);
    const int written = std::vsnprintf(text + used, sizeof(text) - used, format, args);
    va_end(args);
    if (written > 0) {
      used += static_cast<size_t>(written);
      if (used >= sizeof(text)) {
        used = sizeof(text) - 1;
      }
    }
  };

  if (alerts.highTemperature) {
    append("%sHOT %.1fC", used > 0 ? " " : "",
           static_cast<double>(sensors.displayTemperatureC()));
  }
  if (alerts.lowHumidity) {
    append("%sDRY %.0f%%", used > 0 ? " " : "",
           static_cast<double>(sensors.humidityPct));
  }
  if (alerts.pressureJump) {
    append("%sP JUMP", used > 0 ? " " : "");
  }

  const int needed = formatInto(out, outSize, "%s", text);
  return needed > 0 ? static_cast<size_t>(needed) : 0;
}

}  // namespace fw

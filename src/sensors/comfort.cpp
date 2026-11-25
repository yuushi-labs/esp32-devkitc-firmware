#include "sensors/comfort.h"

#include <cstdint>

namespace fw {

float discomfortIndex(float temperatureC, float humidityPct) {
  return 0.81f * temperatureC +
         0.01f * humidityPct * (0.99f * temperatureC - 14.3f) + 46.3f;
}

const char* comfortLabel(float index) {
  if (index < 55.0f) {
    return "cold";
  }
  if (index < 60.0f) {
    return "cool";
  }
  if (index < 65.0f) {
    return "comfortable";
  }
  if (index < 70.0f) {
    return "warm";
  }
  if (index < 75.0f) {
    return "hot";
  }
  return "very hot";
}

const char* weatherTrendLabel(int8_t trend) {
  if (trend > 0) {
    return "sunny";
  }
  if (trend < 0) {
    return "rain";
  }
  return "cloudy";
}

}  // namespace fw

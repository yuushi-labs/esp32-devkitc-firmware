// Comfort index and weather tendency derived from the sensor readings.
//
// Both are small pure functions so the OLED, the web dashboard and the BLE
// status string all show the same wording, and the thresholds are unit tested.
#pragma once

#include <cstdint>

namespace fw {

// Japanese discomfort index: 0.81 T + 0.01 H (0.99 T - 14.3) + 46.3.
float discomfortIndex(float temperatureC, float humidityPct);

// ASCII label that fits the 5x7 OLED font.
const char* comfortLabel(float index);

// Pressure trend (-1 falling, 0 steady, +1 rising) -> weather hint.
const char* weatherTrendLabel(int8_t trend);

}  // namespace fw

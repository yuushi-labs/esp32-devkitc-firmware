// Build-time configuration shared by the firmware and its host tests.
#pragma once

#include <cstddef>
#include <cstdint>

#ifndef FW_VERSION
#define FW_VERSION "0.1.0"
#endif

#ifndef FW_BOARD
#define FW_BOARD "esp32dev"
#endif

namespace fw {

constexpr uint32_t kSerialBaud = 115200;

constexpr uint8_t kLedPin = 2;

constexpr uint32_t kHeartbeatIntervalMs = 5000;
// Toggle every 500 ms so the blink period is 1 s.
constexpr uint32_t kBlinkToggleIntervalMs = 500;

// Longest accepted serial command, excluding the terminator.
constexpr size_t kMaxLineLength = 64;

}  // namespace fw

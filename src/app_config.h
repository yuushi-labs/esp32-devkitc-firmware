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
constexpr uint32_t kBlinkIntervalMs = 1000;

// Longest accepted serial command, excluding the terminator.
constexpr size_t kMaxLineLength = 64;

}  // namespace fw

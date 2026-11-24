// Build-time configuration shared by the firmware and its host tests.
#pragma once

#include <cstddef>
#include <cstdint>

#ifndef FW_VERSION
#define FW_VERSION "0.2.0"
#endif

#ifndef FW_BOARD
#define FW_BOARD "esp32dev"
#endif

#if defined(FW_TARGET_TESTER) && defined(FW_TARGET_STATION)
#error "Build either the station or the tester firmware, not both"
#endif

namespace fw {

constexpr uint32_t kSerialBaud = 115200;

constexpr uint8_t kLedPin = 2;

constexpr uint32_t kHeartbeatIntervalMs = 5000;
// Toggle every 500 ms so the blink period is 1 s.
constexpr uint32_t kBlinkToggleIntervalMs = 500;

// Longest accepted serial command, excluding the terminator.
constexpr size_t kMaxLineLength = 64;

// ---------------------------------------------------------------------------
// Wiring (mirrors pins.json, which is the source of truth for documentation).
// ---------------------------------------------------------------------------

// I2C bus shared by the OLED and the BMP280 on the same two wires.
constexpr uint8_t kI2cSdaPin = 21;
constexpr uint8_t kI2cSclPin = 22;
constexpr uint32_t kI2cClockHz = 100000;

// OLED + encoder + button combo module (VCC GND BAK TRB TRA PSH SCL SDA CON).
constexpr uint8_t kEncoderPinA = 32;      // TRA
constexpr uint8_t kEncoderPinB = 33;      // TRB
constexpr uint8_t kEncoderPushPin = 25;   // PSH
constexpr uint8_t kBackPin = 26;          // BAK
constexpr uint8_t kConfirmPin = 27;       // CON

// Temperature/humidity sensor data line (DHT11 / DHT22 / DHT21).
constexpr uint8_t kDhtPin = 4;

// ---------------------------------------------------------------------------
// Inputs
// ---------------------------------------------------------------------------

constexpr uint32_t kButtonDebounceMs = 25;
constexpr uint32_t kButtonLongPressMs = 900;

// ---------------------------------------------------------------------------
// I2C devices
// ---------------------------------------------------------------------------

constexpr uint8_t kOledPrimaryAddress = 0x3C;
constexpr uint8_t kOledAlternateAddress = 0x3D;

constexpr uint8_t kBmp280PrimaryAddress = 0x76;
constexpr uint8_t kBmp280AlternateAddress = 0x77;

// ---------------------------------------------------------------------------
// Station behaviour
// ---------------------------------------------------------------------------

constexpr const char* kLogPath = "/log.csv";
constexpr const char* kLogMountPoint = "/littlefs";

// Record intervals offered by the encoder push button: 10 s / 1 min / 5 min.
constexpr uint32_t kLogIntervalsMs[] = {10000, 60000, 300000};
constexpr size_t kLogIntervalCount = sizeof(kLogIntervalsMs) / sizeof(kLogIntervalsMs[0]);

// How often the sensors are polled and the panel contents are rebuilt.
constexpr uint32_t kSensorPollIntervalMs = 2000;
constexpr uint32_t kDhtPollIntervalMs = 2500;
constexpr uint32_t kDisplayRefreshMs = 200;

// A pressure change of this many hPa counts as a trend (rising/falling).
constexpr float kPressureTrendThresholdHpa = 0.25f;
constexpr uint32_t kPressureTrendWindowMs = 30000;

// Gives the DHT sensor time before the first read after power-up.
constexpr uint32_t kDhtStartupDelayMs = 1500;

// ---------------------------------------------------------------------------
// Tester behaviour
// ---------------------------------------------------------------------------

constexpr uint32_t kTesterSerialEchoThrottleMs = 150;

// ---------------------------------------------------------------------------
// Firmware flavour helpers
// ---------------------------------------------------------------------------

enum class FirmwareTarget { kStation, kTester };

#if defined(FW_TARGET_TESTER)
constexpr FirmwareTarget kFirmwareTarget = FirmwareTarget::kTester;
#else
constexpr FirmwareTarget kFirmwareTarget = FirmwareTarget::kStation;
#endif

constexpr const char* firmwareTargetName() {
  return kFirmwareTarget == FirmwareTarget::kTester ? "tester" : "station";
}

}  // namespace fw

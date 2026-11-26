// Build-time configuration shared by the firmware and its host tests.
#pragma once

#include <cstddef>
#include <cstdint>

#ifndef FW_VERSION
#define FW_VERSION "0.4.1"
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
// The panel is only re-sent when its pixels change; this is the cap for the
// time based parts of the UI (clock, countdown).
constexpr uint32_t kDisplayRefreshMs = 500;
// The pet walks and wags its tail, so its screen is rebuilt more often. A full
// frame costs about 90 ms on the 100 kHz bus, so 250 ms keeps the I2C bus free
// for the sensors while the animation still looks alive.
constexpr uint32_t kPetRefreshMs = 250;

// A pressure change of this many hPa counts as a trend (rising/falling).
constexpr float kPressureTrendThresholdHpa = 0.25f;
constexpr uint32_t kPressureTrendWindowMs = 30000;

// Gives the DHT sensor time before the first read after power-up.
constexpr uint32_t kDhtStartupDelayMs = 1500;

// ---------------------------------------------------------------------------
// Wi-Fi / NTP / web dashboard
// ---------------------------------------------------------------------------

// Captive portal used until a network has been saved in NVS. The AP is WPA2
// protected with a random key per start so the password typed into the setup
// form is not sent over an open network; the key is shown on the OLED.
constexpr const char* kSetupApSsid = "ESP32-Station-AP";
// Address the setup AP hands out: the phone opens it, the captive portal
// redirects to it, and the OLED QR code points at it while the AP is up.
constexpr const char* kSetupApAddress = "192.168.4.1";
constexpr const char* kSetupApUrl = "http://192.168.4.1/";
constexpr size_t kSetupApPasswordLength = 8;
// How long the setup form stays valid and how often the AP rescans for SSIDs.
constexpr uint32_t kSetupSessionMs = 10 * 60 * 1000;
constexpr uint32_t kSetupScanIntervalMs = 20000;

constexpr uint32_t kWifiConnectTimeoutMs = 15000;
constexpr uint32_t kWifiRetryIntervalMs = 30000;
constexpr uint32_t kWifiStatusPollMs = 1000;

constexpr const char* kNtpServer1 = "pool.ntp.org";
constexpr const char* kNtpServer2 = "time.nist.gov";
constexpr uint32_t kNtpSyncIntervalMs = 3600000;
// Japan Standard Time; the CSV timestamp is local time like the OLED clock.
constexpr int32_t kLocalUtcOffsetSeconds = 9 * 3600;

constexpr uint16_t kWebServerPort = 80;
// How long the OLED keeps showing the dashboard QR code.
constexpr uint32_t kQrDisplayDurationMs = 60000;

// ---------------------------------------------------------------------------
// Bluetooth (NimBLE)
// ---------------------------------------------------------------------------

constexpr const char* kBleDeviceName = "ESP32-EnvStation";
constexpr uint32_t kBleNotifyIntervalMs = 2000;
// HID consumer reports are sent as key-down then key-up; the release follows
// after this short delay, and the next press waits for the gap so two quick
// detents stay two separate key presses.
constexpr uint32_t kBleHidReleaseMs = 30;
constexpr uint32_t kBleHidReportGapMs = 15;
// Advertising restarts this long after a client disconnects.
constexpr uint32_t kBleRestartAdvertisingMs = 500;

// ---------------------------------------------------------------------------
// OLED graphs and alerts
// ---------------------------------------------------------------------------

constexpr size_t kGraphCapacity = 64;
constexpr uint32_t kGraphSampleIntervalMs = 10000;

constexpr float kHighTemperatureAlertC = 30.0f;
constexpr float kLowHumidityAlertPct = 35.0f;
constexpr float kPressureJumpAlertHpa = 1.5f;
constexpr uint32_t kAlertBlinkIntervalMs = 250;

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

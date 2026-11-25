// Serial command parsing and status formatting.
//
// This translation unit is free of Arduino/ESP-IDF dependencies so the exact
// same code runs in the `native` PlatformIO test environment.
#pragma once

#include <cstddef>
#include <cstdint>

#include "app_config.h"

namespace fw {

enum class LedMode { kBlink, kOn, kOff };

const char* ledModeName(LedMode mode);

enum class CommandType {
  kNone,
  kHelp,
  kPing,
  kInfo,
  kPinout,
  kLedOn,
  kLedOff,
  kLedBlink,
  // Shared by both firmwares.
  kScan,
  kSensors,
  // Tester firmware.
  kInputs,
  // Station firmware.
  kLogStatus,
  kLogOn,
  kLogOff,
  kLogNow,
  kLogClear,
  kPageNext,
  kPagePrev,
  kPageSelect,
  // Station v0.3.0: Wi-Fi, BLE, timer and mode navigation.
  kWifiStatus,
  kWifiAp,
  kWifiStation,
  kWifiClear,
  kBleStatus,
  kBleHidOn,
  kBleHidOff,
  kTimerStatus,
  kTimerSet,
  kTimerStart,
  kTimerPause,
  kTimerReset,
  kModeStatus,
  kModeSelect,
  kUnknown,
};

struct Command {
  CommandType type = CommandType::kNone;
  bool hasLedMode = false;
  LedMode ledMode = LedMode::kBlink;
  // 1 based page number for kPageSelect.
  uint8_t page = 0;
  // Minutes for kTimerSet (1..60).
  uint8_t timerMinutes = 0;
  // Mode name for kModeSelect, already lower cased.
  char mode[16] = {0};
};

// Accepts '\n' terminated lines, tolerates a trailing '\r', and reports
// over-long lines instead of dropping them silently.
class LineBuffer {
 public:
  LineBuffer(char* storage, size_t capacity);

  // Returns true once a complete line (without the terminator) is available.
  bool feed(char c);

  // NUL terminated line content; empty until the first line completes.
  const char* line() const;

  // True when the pending line did not fit and was truncated.
  bool overflowed() const { return overflowed_; }

  size_t length() const { return length_; }

  void clear();

 private:
  char* storage_;
  size_t capacity_;
  size_t length_ = 0;
  bool overflowed_ = false;
};

// Parses a single command line. Leading/trailing whitespace is ignored and
// matching is case insensitive.
Command parseCommand(const char* line);

// Help text for the firmware flavour that is being built.
const char* commandHelpText(FirmwareTarget target = kFirmwareTarget);

struct DeviceStatus {
  const char* version;
  const char* chipModel;
  uint32_t chipRevision;
  uint64_t chipId;
  uint32_t uptimeMs;
  uint32_t freeHeap;
  LedMode ledMode;
  const char* resetReason;
};

// The format functions below always NUL terminate and return the number of
// characters the full text needs (snprintf semantics), so callers can detect
// truncation.

size_t formatBanner(const DeviceStatus& status, char* out, size_t outSize);
size_t formatInfo(const DeviceStatus& status, char* out, size_t outSize);
size_t formatHeartbeat(const DeviceStatus& status, char* out, size_t outSize);

}  // namespace fw

#include "command_handler.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "app_config.h"

namespace fw {
namespace {

bool isSpace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

char toLower(char c) {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool equalsIgnoreCase(const char* lhs, const char* rhs) {
  while (*lhs != '\0' && *rhs != '\0') {
    if (toLower(*lhs) != toLower(*rhs)) {
      return false;
    }
    ++lhs;
    ++rhs;
  }
  return *lhs == '\0' && *rhs == '\0';
}

}  // namespace

const char* ledModeName(LedMode mode) {
  switch (mode) {
    case LedMode::kOn:
      return "on";
    case LedMode::kOff:
      return "off";
    case LedMode::kBlink:
      break;
  }
  return "blink";
}

LedMode defaultLedMode(FirmwareTarget target) {
  return target == FirmwareTarget::kTester ? LedMode::kBlink : LedMode::kOff;
}

LineBuffer::LineBuffer(char* storage, size_t capacity)
    : storage_(storage), capacity_(capacity) {
  if (storage_ != nullptr && capacity_ > 0) {
    storage_[0] = '\0';
  }
}

bool LineBuffer::feed(char c) {
  if (c == '\r') {
    return false;
  }
  if (c == '\n') {
    return true;
  }
  if (storage_ == nullptr || capacity_ == 0) {
    overflowed_ = true;
    return false;
  }
  if (length_ + 1 < capacity_) {
    storage_[length_++] = c;
    storage_[length_] = '\0';
  } else {
    overflowed_ = true;
  }
  return false;
}

const char* LineBuffer::line() const {
  return storage_ != nullptr && capacity_ > 0 ? storage_ : "";
}

void LineBuffer::clear() {
  length_ = 0;
  overflowed_ = false;
  if (storage_ != nullptr && capacity_ > 0) {
    storage_[0] = '\0';
  }
}

Command parseCommand(const char* line) {
  Command command;
  if (line == nullptr) {
    return command;
  }

  // Normalise into lowercase space separated tokens.
  char normalised[128] = {0};
  size_t out = 0;
  bool pendingSpace = false;
  for (const char* p = line; *p != '\0'; ++p) {
    if (isSpace(*p)) {
      pendingSpace = out > 0;
      continue;
    }
    // Reserve room for a pending separator, the character itself and the NUL.
    const size_t required = pendingSpace ? 2 : 1;
    if (out + required >= sizeof(normalised)) {
      break;
    }
    if (pendingSpace) {
      normalised[out++] = ' ';
      pendingSpace = false;
    }
    normalised[out++] = toLower(*p);
  }
  normalised[out] = '\0';

  if (out == 0) {
    return command;
  }

  char* arg = std::strchr(normalised, ' ');
  size_t verbLength = arg != nullptr ? static_cast<size_t>(arg - normalised) : out;
  if (arg != nullptr) {
    ++arg;
  }
  char* extra = arg != nullptr ? std::strchr(arg, ' ') : nullptr;
  // Split the third token so `arg` is NUL terminated; `extra` stays non-null
  // as the "there is a third token" marker.
  char* extraArg = nullptr;
  if (extra != nullptr) {
    *extra = '\0';
    extraArg = extra + 1;
  }

  char verbText[32] = {0};
  if (verbLength >= sizeof(verbText)) {
    command.type = CommandType::kUnknown;
    return command;
  }
  std::memcpy(verbText, normalised, verbLength);

  if (equalsIgnoreCase(verbText, "help") && arg == nullptr) {
    command.type = CommandType::kHelp;
  } else if (equalsIgnoreCase(verbText, "ping") && arg == nullptr) {
    command.type = CommandType::kPing;
  } else if (equalsIgnoreCase(verbText, "info") && arg == nullptr) {
    command.type = CommandType::kInfo;
  } else if (equalsIgnoreCase(verbText, "pinout") && arg == nullptr) {
    command.type = CommandType::kPinout;
  } else if (equalsIgnoreCase(verbText, "scan") && arg == nullptr) {
    command.type = CommandType::kScan;
  } else if (equalsIgnoreCase(verbText, "sensors") && arg == nullptr) {
    command.type = CommandType::kSensors;
  } else if (equalsIgnoreCase(verbText, "inputs") && arg == nullptr) {
    command.type = CommandType::kInputs;
  } else if (equalsIgnoreCase(verbText, "log") && extra == nullptr) {
    if (arg == nullptr) {
      command.type = CommandType::kLogStatus;
    } else if (equalsIgnoreCase(arg, "on")) {
      command.type = CommandType::kLogOn;
    } else if (equalsIgnoreCase(arg, "off")) {
      command.type = CommandType::kLogOff;
    } else if (equalsIgnoreCase(arg, "now")) {
      command.type = CommandType::kLogNow;
    } else if (equalsIgnoreCase(arg, "clear")) {
      command.type = CommandType::kLogClear;
    } else {
      command.type = CommandType::kUnknown;
    }
  } else if (equalsIgnoreCase(verbText, "page") && extra == nullptr) {
    if (arg == nullptr) {
      command.type = CommandType::kPageNext;
    } else if (arg[1] == '\0' && arg[0] >= '1' && arg[0] <= '7') {
      command.type = CommandType::kPageSelect;
      command.page = static_cast<uint8_t>(arg[0] - '0');
    } else {
      command.type = CommandType::kUnknown;
    }
  } else if (equalsIgnoreCase(verbText, "wifi") && extra == nullptr) {
    if (arg == nullptr || equalsIgnoreCase(arg, "status")) {
      command.type = CommandType::kWifiStatus;
    } else if (equalsIgnoreCase(arg, "ap")) {
      command.type = CommandType::kWifiAp;
    } else if (equalsIgnoreCase(arg, "station") || equalsIgnoreCase(arg, "connect")) {
      command.type = CommandType::kWifiStation;
    } else if (equalsIgnoreCase(arg, "clear")) {
      command.type = CommandType::kWifiClear;
    } else {
      command.type = CommandType::kUnknown;
    }
  } else if (equalsIgnoreCase(verbText, "ble")) {
    if (arg == nullptr || (equalsIgnoreCase(arg, "status") && extra == nullptr)) {
      command.type = CommandType::kBleStatus;
    } else if (equalsIgnoreCase(arg, "hid") && extraArg != nullptr &&
               std::strchr(extraArg, ' ') == nullptr) {
      if (equalsIgnoreCase(extraArg, "on")) {
        command.type = CommandType::kBleHidOn;
      } else if (equalsIgnoreCase(extraArg, "off")) {
        command.type = CommandType::kBleHidOff;
      } else {
        command.type = CommandType::kUnknown;
      }
    } else {
      command.type = CommandType::kUnknown;
    }
  } else if (equalsIgnoreCase(verbText, "timer") && extra == nullptr) {
    if (arg == nullptr) {
      command.type = CommandType::kTimerStatus;
    } else if (equalsIgnoreCase(arg, "start")) {
      command.type = CommandType::kTimerStart;
    } else if (equalsIgnoreCase(arg, "pause") || equalsIgnoreCase(arg, "stop")) {
      command.type = CommandType::kTimerPause;
    } else if (equalsIgnoreCase(arg, "reset")) {
      command.type = CommandType::kTimerReset;
    } else if (arg[0] >= '1' && arg[0] <= '9') {
      char* end = nullptr;
      const long minutes = std::strtol(arg, &end, 10);
      if (end != nullptr && *end == '\0' && minutes >= 1 && minutes <= 60) {
        command.type = CommandType::kTimerSet;
        command.timerMinutes = static_cast<uint8_t>(minutes);
      } else {
        command.type = CommandType::kUnknown;
      }
    } else {
      command.type = CommandType::kUnknown;
    }
  } else if (equalsIgnoreCase(verbText, "mode") && extra == nullptr) {
    if (arg == nullptr) {
      command.type = CommandType::kModeStatus;
    } else if (std::strlen(arg) < sizeof(command.mode)) {
      command.type = CommandType::kModeSelect;
      std::snprintf(command.mode, sizeof(command.mode), "%s", arg);
    } else {
      command.type = CommandType::kUnknown;
    }
  } else if (equalsIgnoreCase(verbText, "oled") && extra == nullptr) {
    if (arg == nullptr || equalsIgnoreCase(arg, "status")) {
      command.type = CommandType::kOledStatus;
    } else if (equalsIgnoreCase(arg, "on")) {
      command.type = CommandType::kOledOn;
    } else if (equalsIgnoreCase(arg, "off")) {
      command.type = CommandType::kOledOff;
    } else {
      command.type = CommandType::kUnknown;
    }
  } else if (equalsIgnoreCase(verbText, "pet") && extra == nullptr) {
    if (arg == nullptr || equalsIgnoreCase(arg, "status")) {
      command.type = CommandType::kPetStatus;
    } else if (equalsIgnoreCase(arg, "clock")) {
      command.type = CommandType::kPetClock;
    } else {
      command.type = CommandType::kUnknown;
    }
  } else if (equalsIgnoreCase(verbText, "led") && arg != nullptr && extra == nullptr) {
    command.hasLedMode = true;
    if (equalsIgnoreCase(arg, "on")) {
      command.type = CommandType::kLedOn;
      command.ledMode = LedMode::kOn;
    } else if (equalsIgnoreCase(arg, "off")) {
      command.type = CommandType::kLedOff;
      command.ledMode = LedMode::kOff;
    } else if (equalsIgnoreCase(arg, "blink")) {
      command.type = CommandType::kLedBlink;
      command.ledMode = LedMode::kBlink;
    } else {
      command.hasLedMode = false;
      command.type = CommandType::kUnknown;
    }
  } else {
    command.type = CommandType::kUnknown;
  }

  return command;
}

const char* commandHelpText(FirmwareTarget target) {
  if (target == FirmwareTarget::kTester) {
    return "commands:\n"
           "  help       show this help\n"
           "  ping       reply with pong\n"
           "  info       show chip and firmware information\n"
           "  pinout     show the GPIO wiring for this firmware\n"
           "  led on     turn the onboard LED on\n"
           "  led off    turn the onboard LED off\n"
           "  led blink  blink the onboard LED (1 s period)\n"
           "  scan       scan the I2C bus and name every device\n"
           "  sensors    show the BMP280 and DHT status\n"
           "  inputs     show the encoder and button counters\n";
  }
  return "commands:\n"
         "  help       show this help\n"
         "  ping       reply with pong\n"
         "  info       show chip and firmware information\n"
         "  pinout     show the GPIO wiring for this firmware\n"
         "  led on     turn the onboard LED on\n"
         "  led off    turn the onboard LED off\n"
         "  led blink  blink the onboard LED (1 s period)\n"
         "  scan       scan the I2C bus and name every device\n"
         "  sensors    show the BMP280 and DHT status\n"
         "  log        show the LittleFS log state\n"
         "  log on     start recording /log.csv\n"
         "  log off    stop recording\n"
         "  log now    write one sample immediately\n"
         "  log clear  delete /log.csv and start over\n"
         "  page       go to the next mode\n"
         "  page 1..7  jump to a mode\n"
         "  mode       show the current screen and mode\n"
         "  mode NAME  open pet/env/logs/wifi/ble/timer/system/menu\n"
         "  oled       show the OLED power state\n"
         "  oled on|off  turn the panel on or off\n"
         "  pet        show the virtual pet state\n"
         "  pet clock  replay the hourly clock chime\n"
         "  wifi       show the Wi-Fi and NTP status\n"
         "  wifi ap    start the setup access point\n"
         "  wifi station  reconnect to the saved network\n"
         "  wifi clear forget the saved network\n"
         "  ble        show the BLE status\n"
         "  ble hid on|off  media remote mode\n"
         "  timer      show the countdown timer\n"
         "  timer N    set 1..60 minutes\n"
         "  timer start|pause|reset\n";
}

namespace {

// snprintf with a real destination even for a zero-sized request, and a
// return value that always reports the untruncated length.
int formatInto(char* out, size_t outSize, const char* format, ...) {
  char scratch;
  va_list args;
  va_start(args, format);
  const int needed =
      std::vsnprintf(outSize > 0 ? out : &scratch, outSize, format, args);
  va_end(args);
  return needed;
}

}  // namespace

size_t formatBanner(const DeviceStatus& status, char* out, size_t outSize) {
  const int needed = formatInto(
      out, outSize,
      "esp32-devkitc-firmware v%s (%s)\n"
      "chip_model=%s chip_revision=%u chip_id=0x%012llx\n"
      "reset_reason=%s\n"
      "type 'help' for commands\n",
      status.version, FW_BOARD, status.chipModel,
      static_cast<unsigned>(status.chipRevision),
      static_cast<unsigned long long>(status.chipId), status.resetReason);
  return needed > 0 ? static_cast<size_t>(needed) : 0;
}

size_t formatInfo(const DeviceStatus& status, char* out, size_t outSize) {
  const int needed = formatInto(
      out, outSize,
      "fw_version=%s\n"
      "board=%s\n"
      "chip_model=%s chip_revision=%u\n"
      "chip_id=0x%012llx\n"
      "reset_reason=%s\n"
      "uptime_ms=%lu\n"
      "free_heap=%lu\n"
      "led_mode=%s\n",
      status.version, FW_BOARD, status.chipModel,
      static_cast<unsigned>(status.chipRevision),
      static_cast<unsigned long long>(status.chipId), status.resetReason,
      static_cast<unsigned long>(status.uptimeMs),
      static_cast<unsigned long>(status.freeHeap), ledModeName(status.ledMode));
  return needed > 0 ? static_cast<size_t>(needed) : 0;
}

size_t formatHeartbeat(const DeviceStatus& status, char* out, size_t outSize) {
  const int needed = formatInto(
      out, outSize, "heartbeat uptime_ms=%lu free_heap=%lu led_mode=%s\n",
      static_cast<unsigned long>(status.uptimeMs),
      static_cast<unsigned long>(status.freeHeap), ledModeName(status.ledMode));
  return needed > 0 ? static_cast<size_t>(needed) : 0;
}

}  // namespace fw

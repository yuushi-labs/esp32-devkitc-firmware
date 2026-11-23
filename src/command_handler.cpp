#include "command_handler.h"

#include <cstdarg>
#include <cstdio>
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

  const char* verb = normalised;
  const char* arg = std::strchr(normalised, ' ');
  size_t verbLength = arg != nullptr ? static_cast<size_t>(arg - normalised) : out;
  if (arg != nullptr) {
    ++arg;
  }
  const char* extra = arg != nullptr ? std::strchr(arg, ' ') : nullptr;

  char verbText[32] = {0};
  if (verbLength >= sizeof(verbText)) {
    command.type = CommandType::kUnknown;
    return command;
  }
  std::memcpy(verbText, verb, verbLength);

  if (equalsIgnoreCase(verbText, "help") && arg == nullptr) {
    command.type = CommandType::kHelp;
  } else if (equalsIgnoreCase(verbText, "ping") && arg == nullptr) {
    command.type = CommandType::kPing;
  } else if (equalsIgnoreCase(verbText, "info") && arg == nullptr) {
    command.type = CommandType::kInfo;
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

const char* commandHelpText() {
  return "commands:\n"
         "  help       show this help\n"
         "  ping       reply with pong\n"
         "  info       show chip and firmware information\n"
         "  led on     turn the onboard LED on\n"
         "  led off    turn the onboard LED off\n"
         "  led blink  blink the onboard LED (1 s period)\n";
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

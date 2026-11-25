#include "ble/ble_protocol.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace fw {
namespace {

constexpr uint16_t kConsumerVolumeUp = 0x00E9;
constexpr uint16_t kConsumerVolumeDown = 0x00EA;
constexpr uint16_t kConsumerPlayPause = 0x00CD;
constexpr uint16_t kConsumerNextTrack = 0x00B5;
constexpr uint16_t kConsumerPreviousTrack = 0x00B6;

int formatInto(char* out, size_t outSize, const char* format, ...) {
  char scratch = '\0';
  va_list args;
  va_start(args, format);
  const int needed =
      std::vsnprintf(outSize > 0 ? out : &scratch, outSize, format, args);
  va_end(args);
  return needed;
}

int32_t roundToInt(float value) {
  return static_cast<int32_t>(value >= 0.0f ? value + 0.5f : value - 0.5f);
}

bool writeLe16(uint8_t* out, size_t capacity, uint16_t value) {
  if (out == nullptr || capacity < 2) {
    return false;
  }
  out[0] = static_cast<uint8_t>(value & 0xFF);
  out[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
  return true;
}

bool writeLe32(uint8_t* out, size_t capacity, uint32_t value) {
  if (out == nullptr || capacity < 4) {
    return false;
  }
  out[0] = static_cast<uint8_t>(value & 0xFF);
  out[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
  out[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
  out[3] = static_cast<uint8_t>((value >> 24) & 0xFF);
  return true;
}

}  // namespace

uint16_t mediaActionUsage(MediaAction action) {
  switch (action) {
    case MediaAction::kVolumeUp:
      return kConsumerVolumeUp;
    case MediaAction::kVolumeDown:
      return kConsumerVolumeDown;
    case MediaAction::kPlayPause:
      return kConsumerPlayPause;
    case MediaAction::kNextTrack:
      return kConsumerNextTrack;
    case MediaAction::kPreviousTrack:
      return kConsumerPreviousTrack;
    case MediaAction::kNone:
      break;
  }
  return 0;
}

const char* mediaActionName(MediaAction action) {
  switch (action) {
    case MediaAction::kVolumeUp:
      return "volume-up";
    case MediaAction::kVolumeDown:
      return "volume-down";
    case MediaAction::kPlayPause:
      return "play-pause";
    case MediaAction::kNextTrack:
      return "next-track";
    case MediaAction::kPreviousTrack:
      return "previous-track";
    case MediaAction::kNone:
      break;
  }
  return "none";
}

MediaAction mediaActionForEncoder(int delta) {
  if (delta > 0) {
    return MediaAction::kVolumeUp;
  }
  if (delta < 0) {
    return MediaAction::kVolumeDown;
  }
  return MediaAction::kNone;
}

MediaAction mediaActionForButtons(bool push, bool confirm, bool back) {
  if (push) {
    return MediaAction::kPlayPause;
  }
  if (confirm) {
    return MediaAction::kNextTrack;
  }
  if (back) {
    return MediaAction::kPreviousTrack;
  }
  return MediaAction::kNone;
}

size_t encodeHidConsumerReport(MediaAction action, uint8_t* out, size_t capacity) {
  if (!writeLe16(out, capacity, mediaActionUsage(action))) {
    return 0;
  }
  return 2;
}

size_t encodeBleTemperature(float celsius, uint8_t* out, size_t capacity) {
  int32_t scaled = roundToInt(celsius * 100.0f);
  if (scaled > 32767) {
    scaled = 32767;
  } else if (scaled < -32768) {
    scaled = -32768;
  }
  if (!writeLe16(out, capacity, static_cast<uint16_t>(static_cast<int16_t>(scaled)))) {
    return 0;
  }
  return 2;
}

size_t encodeBleHumidity(float percent, uint8_t* out, size_t capacity) {
  int32_t scaled = roundToInt(percent * 100.0f);
  if (scaled < 0) {
    scaled = 0;
  } else if (scaled > 10000) {
    scaled = 10000;
  }
  if (!writeLe16(out, capacity, static_cast<uint16_t>(scaled))) {
    return 0;
  }
  return 2;
}

size_t encodeBlePressure(float hectopascal, uint8_t* out, size_t capacity) {
  // 1 hPa = 100 Pa = 1000 units of 0.1 Pa.
  const int64_t scaled = static_cast<int64_t>(std::llround(hectopascal * 1000.0f));
  const uint32_t value = scaled < 0 ? 0u : static_cast<uint32_t>(scaled);
  if (!writeLe32(out, capacity, value)) {
    return 0;
  }
  return 4;
}

size_t formatBleStatus(const SensorSnapshot& sensors, bool alertActive, char* out,
                       size_t outSize) {
  char temperature[8] = "-";
  char humidity[8] = "-";
  char pressure[10] = "-";
  if (sensors.hasTemperature()) {
    std::snprintf(temperature, sizeof(temperature), "%.1f",
                  static_cast<double>(sensors.displayTemperatureC()));
  }
  if (sensors.hasHumidity()) {
    std::snprintf(humidity, sizeof(humidity), "%.1f",
                  static_cast<double>(sensors.humidityPct));
  }
  if (sensors.hasPressure()) {
    std::snprintf(pressure, sizeof(pressure), "%.1f",
                  static_cast<double>(sensors.pressureHpa));
  }
  const int needed = formatInto(out, outSize, "T=%s,H=%s,P=%s,A=%d", temperature,
                                humidity, pressure, alertActive ? 1 : 0);
  return needed > 0 ? static_cast<size_t>(needed) : 0;
}

}  // namespace fw

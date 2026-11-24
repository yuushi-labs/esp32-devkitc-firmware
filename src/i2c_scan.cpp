#include "i2c_scan.h"

#include <cstdarg>
#include <cstdio>

namespace fw {
namespace {

constexpr uint8_t kFirstAddress = 0x01;
constexpr uint8_t kLastAddress = 0x7F;

// BMP280 / BME280 chip id register and the ids worth naming.
constexpr uint8_t kChipIdRegister = 0xD0;
constexpr uint8_t kChipIdBmp280 = 0x58;
constexpr uint8_t kChipIdBme280 = 0x60;

int formatInto(char* out, size_t outSize, const char* format, ...) {
  char scratch = '\0';
  va_list args;
  va_start(args, format);
  const int needed =
      std::vsnprintf(outSize > 0 ? out : &scratch, outSize, format, args);
  va_end(args);
  return needed;
}

}  // namespace

bool I2cScanResult::contains(uint8_t address) const {
  for (size_t index = 0; index < count; ++index) {
    if (entries[index].address == address) {
      return true;
    }
  }
  return false;
}

bool I2cScanResult::containsKind(I2cDeviceKind kind) const {
  for (size_t index = 0; index < count; ++index) {
    if (entries[index].kind == kind) {
      return true;
    }
  }
  return false;
}

const char* i2cDeviceKindName(I2cDeviceKind kind) {
  switch (kind) {
    case I2cDeviceKind::kSsd1306:
      return "SSD1306 OLED";
    case I2cDeviceKind::kBmp280:
      return "BMP280";
    case I2cDeviceKind::kBme280:
      return "BME280";
    case I2cDeviceKind::kOther:
      break;
  }
  return "unknown device";
}

I2cDeviceKind classifyI2cDevice(I2cTransport& bus, uint8_t address) {
  if (address == 0x3C || address == 0x3D) {
    return I2cDeviceKind::kSsd1306;
  }
  if (address == 0x76 || address == 0x77) {
    uint8_t chipId = 0;
    if (readRegister(bus, address, kChipIdRegister, &chipId)) {
      if (chipId == kChipIdBmp280) {
        return I2cDeviceKind::kBmp280;
      }
      if (chipId == kChipIdBme280) {
        return I2cDeviceKind::kBme280;
      }
    }
  }
  return I2cDeviceKind::kOther;
}

I2cScanResult scanI2cBus(I2cTransport& bus) {
  I2cScanResult scan;
  for (uint16_t address = kFirstAddress; address <= kLastAddress; ++address) {
    const uint8_t candidate = static_cast<uint8_t>(address);
    if (!bus.probe(candidate)) {
      continue;
    }
    if (scan.count >= I2cScanResult::kMaxEntries) {
      break;
    }
    scan.entries[scan.count].address = candidate;
    scan.entries[scan.count].kind = classifyI2cDevice(bus, candidate);
    ++scan.count;
  }
  return scan;
}

size_t formatI2cEntry(const I2cScanEntry& entry, char* out, size_t outSize) {
  const int needed = formatInto(out, outSize, "0x%02X %s", entry.address,
                                i2cDeviceKindName(entry.kind));
  return needed > 0 ? static_cast<size_t>(needed) : 0;
}

size_t formatI2cAddressList(const I2cScanResult& scan, char* out, size_t outSize) {
  if (scan.empty()) {
    if (out != nullptr && outSize > 0) {
      std::snprintf(out, outSize, "none");
    }
    return 4;
  }
  if (out == nullptr || outSize == 0) {
    // "0x3C" plus a comma between entries.
    return scan.count * 4 + (scan.count - 1);
  }
  size_t used = 0;
  out[0] = '\0';
  if (scan.empty()) {
    std::snprintf(out, outSize, "none");
    return 4;
  }
  for (size_t index = 0; index < scan.count; ++index) {
    const int written = std::snprintf(used < outSize ? out + used : nullptr,
                                      used < outSize ? outSize - used : 0, "%s0x%02X",
                                      index == 0 ? "" : ",",
                                      scan.entries[index].address);
    if (written <= 0) {
      break;
    }
    used += static_cast<size_t>(written) < outSize - used
                ? static_cast<size_t>(written)
                : outSize - used;
  }
  return used;
}

size_t formatI2cScan(const I2cScanResult& scan, char* out, size_t outSize) {
  size_t used = 0;
  size_t needed = 0;
  auto append = [&](const char* text) {
    if (text == nullptr) {
      return;
    }
    char* destination = (out != nullptr && used < outSize) ? out + used : nullptr;
    const size_t remaining = destination != nullptr ? outSize - used : 0;
    const int written = formatInto(destination, remaining, "%s", text);
    if (written <= 0) {
      return;
    }
    needed += static_cast<size_t>(written);
    if (outSize > used) {
      const size_t available = outSize - used - 1;
      used += static_cast<size_t>(written) < available ? static_cast<size_t>(written)
                                                       : available;
    }
  };

  char line[64];
  if (scan.empty()) {
    append("i2c scan: no device answered\n");
    return needed;
  }

  std::snprintf(line, sizeof(line), "i2c scan: %u device(s)\n",
                static_cast<unsigned>(scan.count));
  append(line);
  for (size_t index = 0; index < scan.count; ++index) {
    const I2cScanEntry& entry = scan.entries[index];
    std::snprintf(line, sizeof(line), "  0x%02X %s\n", entry.address,
                  i2cDeviceKindName(entry.kind));
    append(line);
  }
  return needed;
}

}  // namespace fw

// I2C bus scan shared by the tester firmware and the station's system page.
#pragma once

#include <cstddef>
#include <cstdint>

#include "i2c_transport.h"

namespace fw {

enum class I2cDeviceKind {
  kOther,
  kSsd1306,
  kBmp280,
  kBme280,
};

struct I2cScanEntry {
  uint8_t address = 0;
  I2cDeviceKind kind = I2cDeviceKind::kOther;
};

struct I2cScanResult {
  // The panel, the BMP280 and a handful of surprises fit comfortably.
  static constexpr size_t kMaxEntries = 24;

  I2cScanEntry entries[kMaxEntries];
  size_t count = 0;

  bool empty() const { return count == 0; }
  bool contains(uint8_t address) const;
  bool containsKind(I2cDeviceKind kind) const;
};

// Short human readable kind, e.g. "SSD1306 OLED" or "unknown device".
const char* i2cDeviceKindName(I2cDeviceKind kind);

// Classifies one acknowledged address. 0x76/0x77 are probed for the BMP280 /
// BME280 chip id register so the report can name the part.
I2cDeviceKind classifyI2cDevice(I2cTransport& bus, uint8_t address);

// Probes every address on the bus (0x01..0x7F) and records the ones that answer.
I2cScanResult scanI2cBus(I2cTransport& bus);

// "i2c scan: 2 device(s)\n  0x3c SSD1306 OLED\n  0x76 BMP280\n" or
// "i2c scan: no device answered\n". NUL terminated, snprintf semantics.
size_t formatI2cScan(const I2cScanResult& scan, char* out, size_t outSize);

// One address as a display line: "0x3C SSD1306 OLED".
size_t formatI2cEntry(const I2cScanEntry& entry, char* out, size_t outSize);

// Compact list for a status line: "0x3C,0x76" or "none".
size_t formatI2cAddressList(const I2cScanResult& scan, char* out, size_t outSize);

}  // namespace fw

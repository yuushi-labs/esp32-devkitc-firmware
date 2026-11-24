// Test doubles shared by the native unit tests.
//
// This header is only included from test/ sources: the firmware build never
// references it, and it has no Arduino dependencies.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "i2c_transport.h"
#include "logging/log_record.h"

namespace fw {
namespace test {

// A tiny I2C bus with per address register files, failure injection and a log
// of every write so the panel driver can be checked byte for byte.
class FakeI2cTransport : public I2cTransport {
 public:
  static constexpr size_t kRegisterCount = 256;

  void addDevice(uint8_t address) { devices_[address]; }

  bool hasDevice(uint8_t address) const {
    return devices_.find(address) != devices_.end();
  }

  void setRegister(uint8_t address, uint8_t reg, uint8_t value) {
    devices_[address][reg] = value;
  }

  void setRegisters(uint8_t address, uint8_t first, const uint8_t* values,
                    size_t length) {
    for (size_t index = 0; index < length; ++index) {
      devices_[address][static_cast<uint8_t>(first + index)] = values[index];
    }
  }

  uint8_t registerValue(uint8_t address, uint8_t reg) const {
    const auto device = devices_.find(address);
    if (device == devices_.end()) {
      return 0;
    }
    const auto value = device->second.find(reg);
    return value == device->second.end() ? 0 : value->second;
  }

  bool write(uint8_t address, const uint8_t* data, size_t length) override {
    ++writeCount;
    if (failWrites || data == nullptr || length == 0 || !hasDevice(address)) {
      return false;
    }
    writeLog.push_back(std::vector<uint8_t>(data, data + length));
    // Register write: one register index followed by one value.
    if (length == 2) {
      devices_[address][data[0]] = data[1];
    }
    return true;
  }

  bool writeRead(uint8_t address, const uint8_t* writeData, size_t writeLength,
                 uint8_t* readData, size_t readLength) override {
    ++readCount;
    if (failReads || writeData == nullptr || writeLength == 0 ||
        readData == nullptr || readLength == 0 || !hasDevice(address)) {
      return false;
    }
    const uint8_t firstRegister = writeData[0];
    for (size_t index = 0; index < readLength; ++index) {
      const uint8_t reg = static_cast<uint8_t>(firstRegister + index);
      const auto device = devices_.find(address);
      const auto value = device->second.find(reg);
      readData[index] = value == device->second.end() ? 0 : value->second;
    }
    return true;
  }

  bool probe(uint8_t address) override {
    ++probeCount;
    return hasDevice(address);
  }

  bool failWrites = false;
  bool failReads = false;
  uint32_t writeCount = 0;
  uint32_t readCount = 0;
  uint32_t probeCount = 0;
  std::vector<std::vector<uint8_t>> writeLog;

 private:
  std::map<uint8_t, std::map<uint8_t, uint8_t>> devices_;
};

// In-memory LogStorage with a failure switch.
class FakeLogStorage : public LogStorage {
 public:
  bool begin() override { return mountOk; }

  bool appendLine(const char* line) override {
    if (!mountOk || failWrites || line == nullptr) {
      return false;
    }
    content += line;
    return true;
  }

  uint32_t countDataLines() override {
    uint32_t newlines = 0;
    for (char c : content) {
      if (c == '\n') {
        ++newlines;
      }
    }
    return newlines > 0 ? newlines - 1 : 0;
  }

  uint32_t fileSize() override { return static_cast<uint32_t>(content.size()); }

  bool removeFile() override {
    if (!mountOk) {
      return false;
    }
    content.clear();
    return true;
  }

  bool mountOk = true;
  bool failWrites = false;
  std::string content;
};

}  // namespace test
}  // namespace fw

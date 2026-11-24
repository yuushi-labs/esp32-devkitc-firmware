// Minimal I2C abstraction.
//
// Every driver in this firmware talks to the bus through this interface, so the
// exact same code runs against real hardware (WireI2cTransport) and against the
// fake devices used by the native unit tests.
#pragma once

#include <cstddef>
#include <cstdint>

namespace fw {

class I2cTransport {
 public:
  virtual ~I2cTransport() = default;

  // Write only transfer, ended with a STOP condition.
  virtual bool write(uint8_t address, const uint8_t* data, size_t length) = 0;

  // Write `writeLength` bytes then read `readLength` bytes without releasing the
  // bus in between (repeated START) when the platform can.
  virtual bool writeRead(uint8_t address, const uint8_t* writeData,
                         size_t writeLength, uint8_t* readData,
                         size_t readLength) = 0;

  // True when a device acknowledges `address`.
  virtual bool probe(uint8_t address) = 0;
};

// Register helpers shared by the drivers.
bool readRegisters(I2cTransport& bus, uint8_t address, uint8_t reg, uint8_t* out,
                   size_t length);
bool readRegister(I2cTransport& bus, uint8_t address, uint8_t reg, uint8_t* out);
bool writeRegister(I2cTransport& bus, uint8_t address, uint8_t reg, uint8_t value);

}  // namespace fw

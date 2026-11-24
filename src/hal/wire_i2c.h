// Wire (Arduino I2C) implementation of the I2cTransport interface.
#pragma once

#include <Arduino.h>
#include <Wire.h>

#include <cstddef>
#include <cstdint>

#include "i2c_transport.h"

namespace fw {

class WireI2cTransport : public I2cTransport {
 public:
  explicit WireI2cTransport(TwoWire& wire = Wire) : wire_(wire) {}

  void begin(int sdaPin, int sclPin, uint32_t clockHz);

  bool write(uint8_t address, const uint8_t* data, size_t length) override;
  bool writeRead(uint8_t address, const uint8_t* writeData, size_t writeLength,
                 uint8_t* readData, size_t readLength) override;
  bool probe(uint8_t address) override;

 private:
  TwoWire& wire_;
};

}  // namespace fw

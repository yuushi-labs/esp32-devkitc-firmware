#include "hal/wire_i2c.h"

namespace fw {

bool WireI2cTransport::begin(int sdaPin, int sclPin, uint32_t clockHz) {
  ready_ = wire_.begin(sdaPin, sclPin, clockHz);
  if (!ready_) {
    return false;
  }
  wire_.setTimeOut(50);  // milliseconds; keeps a missing device from blocking
  return true;
}

bool WireI2cTransport::write(uint8_t address, const uint8_t* data, size_t length) {
  if (!ready_ || data == nullptr || length == 0) {
    return false;
  }
  wire_.beginTransmission(address);
  const size_t written = wire_.write(data, length);
  return wire_.endTransmission() == 0 && written == length;
}

bool WireI2cTransport::writeRead(uint8_t address, const uint8_t* writeData,
                                 size_t writeLength, uint8_t* readData,
                                 size_t readLength) {
  if (!ready_ || writeData == nullptr || writeLength == 0 || readData == nullptr ||
      readLength == 0) {
    return false;
  }
  wire_.beginTransmission(address);
  const size_t written = wire_.write(writeData, writeLength);
  if (written != writeLength || wire_.endTransmission(false) != 0) {
    return false;
  }

  const size_t received = wire_.requestFrom(
      static_cast<uint16_t>(address), static_cast<uint8_t>(readLength));
  if (received != readLength) {
    // Drain whatever did arrive so the next transfer starts clean.
    while (wire_.available() > 0) {
      (void)wire_.read();
    }
    return false;
  }
  for (size_t index = 0; index < readLength; ++index) {
    const int value = wire_.read();
    if (value < 0) {
      return false;
    }
    readData[index] = static_cast<uint8_t>(value);
  }
  return true;
}

bool WireI2cTransport::probe(uint8_t address) {
  if (!ready_) {
    return false;
  }
  wire_.beginTransmission(address);
  return wire_.endTransmission() == 0;
}

}  // namespace fw

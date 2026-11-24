#include "i2c_transport.h"

namespace fw {

bool readRegisters(I2cTransport& bus, uint8_t address, uint8_t reg, uint8_t* out,
                   size_t length) {
  if (out == nullptr || length == 0) {
    return false;
  }
  const uint8_t command = reg;
  return bus.writeRead(address, &command, 1, out, length);
}

bool readRegister(I2cTransport& bus, uint8_t address, uint8_t reg, uint8_t* out) {
  return readRegisters(bus, address, reg, out, 1);
}

bool writeRegister(I2cTransport& bus, uint8_t address, uint8_t reg, uint8_t value) {
  const uint8_t payload[2] = {reg, value};
  return bus.write(address, payload, sizeof(payload));
}

}  // namespace fw

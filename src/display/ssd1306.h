// SSD1306 I2C panel driver (128x64, address 0x3C or 0x3D).
//
// Talks to the panel through I2cTransport, so the initialisation sequence and
// the page upload are unit tested against a fake bus.
#pragma once

#include <cstddef>
#include <cstdint>

#include "display/framebuffer.h"
#include "i2c_transport.h"

namespace fw {

class Ssd1306Display {
 public:
  Ssd1306Display(I2cTransport& bus, uint8_t address, Framebuffer& framebuffer);

  // Sends the 128x64 initialisation sequence. False when the panel NACKs.
  bool begin();
  // Same, after switching to the detected address (0x3C/0x3D).
  bool begin(uint8_t address);

  // Uploads the whole framebuffer, page by page.
  bool flush();

  bool present();
  bool initialised() const { return initialised_; }
  uint8_t address() const { return address_; }

  // 0x3C or 0x3D when a panel answers, 0 when neither does.
  static uint8_t detectAddress(I2cTransport& bus);

  // Bytes of panel data sent after the 0x40 control byte per transfer. The
  // ESP32 Wire buffer is 128 bytes, 16 keeps every write small and safe.
  static constexpr size_t kDataChunkSize = 16;

 private:
  bool sendCommands(const uint8_t* commands, size_t length);

  I2cTransport& bus_;
  uint8_t address_;
  Framebuffer& framebuffer_;
  bool initialised_ = false;
};

}  // namespace fw

#include "display/ssd1306.h"

#include "app_config.h"

namespace fw {
namespace {

constexpr uint8_t kControlCommands = 0x00;
constexpr uint8_t kControlData = 0x40;

constexpr uint8_t kCommandDisplayOff = 0xAE;
constexpr uint8_t kCommandDisplayOn = 0xAF;
constexpr uint8_t kCommandSetPageStart = 0xB0;
constexpr uint8_t kCommandSetLowNibble = 0x00;
constexpr uint8_t kCommandSetHighNibble = 0x10;

// 128x64 panel, internal charge pump, horizontal addressing, COM scan C8.
constexpr uint8_t kInitSequence[] = {
    0xAE,        // display off
    0xD5, 0x80,  // clock divide / oscillator
    0xA8, 0x3F,  // multiplex ratio 64
    0xD3, 0x00,  // display offset 0
    0x40,        // start line 0
    0x8D, 0x14,  // charge pump on
    0x20, 0x02,  // page addressing mode (flush() selects the page itself)
    0xA1,        // segment remap (column 127 -> SEG0)
    0xC8,        // COM scan direction remapped
    0xDA, 0x12,  // COM pins configuration for 128x64
    0x81, 0xCF,  // contrast
    0xD9, 0xF1,  // pre charge period
    0xDB, 0x40,  // VCOMH deselect level
    0xA4,        // resume to RAM content
    0xA6,        // normal (not inverted) display
    0x2E,        // deactivate scroll
    0xAF,        // display on
};

constexpr size_t kInitSequenceLength = sizeof(kInitSequence);

}  // namespace

Ssd1306Display::Ssd1306Display(I2cTransport& bus, uint8_t address,
                               Framebuffer& framebuffer)
    : bus_(bus), address_(address), framebuffer_(framebuffer) {}

bool Ssd1306Display::sendCommands(const uint8_t* commands, size_t length) {
  if (commands == nullptr || length == 0) {
    return false;
  }
  uint8_t buffer[32] = {0};
  if (length + 1 > sizeof(buffer)) {
    return false;
  }
  buffer[0] = kControlCommands;
  for (size_t index = 0; index < length; ++index) {
    buffer[index + 1] = commands[index];
  }
  return bus_.write(address_, buffer, length + 1);
}

bool Ssd1306Display::begin() {
  initialised_ = false;
  if (!bus_.probe(address_)) {
    return false;
  }
  if (!sendCommands(kInitSequence, kInitSequenceLength)) {
    return false;
  }
  initialised_ = true;
  power_ = true;
  return true;
}

bool Ssd1306Display::begin(uint8_t address) {
  address_ = address;
  return begin();
}

bool Ssd1306Display::present() {
  if (initialised_) {
    return true;
  }
  return begin();
}

bool Ssd1306Display::setPower(bool on) {
  if (!initialised_) {
    return false;
  }
  const uint8_t command = on ? kCommandDisplayOn : kCommandDisplayOff;
  if (!sendCommands(&command, 1)) {
    return false;
  }
  power_ = on;
  return true;
}

bool Ssd1306Display::flush() {
  if (!initialised_) {
    return false;
  }
  if (!power_) {
    // The panel is dark on purpose; keep the bus free for the sensors.
    return true;
  }

  const uint8_t* pages = framebuffer_.buffer();
  uint8_t data[1 + kDataChunkSize];

  for (uint8_t page = 0; page < Framebuffer::kPageCount; ++page) {
    const uint8_t commands[] = {
        static_cast<uint8_t>(kCommandSetPageStart | page),
        kCommandSetLowNibble,
        kCommandSetHighNibble,
    };
    if (!sendCommands(commands, sizeof(commands))) {
      return false;
    }

    for (size_t offset = 0; offset < Framebuffer::kWidth; offset += kDataChunkSize) {
      const size_t chunk =
          (Framebuffer::kWidth - offset) < kDataChunkSize
              ? (Framebuffer::kWidth - offset)
              : kDataChunkSize;
      data[0] = kControlData;
      for (size_t index = 0; index < chunk; ++index) {
        data[index + 1] = pages[static_cast<size_t>(page) * Framebuffer::kWidth + offset + index];
      }
      if (!bus_.write(address_, data, chunk + 1)) {
        return false;
      }
    }
  }
  return true;
}

uint8_t Ssd1306Display::detectAddress(I2cTransport& bus) {
  if (bus.probe(kOledPrimaryAddress)) {
    return kOledPrimaryAddress;
  }
  if (bus.probe(kOledAlternateAddress)) {
    return kOledAlternateAddress;
  }
  return 0;
}

}  // namespace fw

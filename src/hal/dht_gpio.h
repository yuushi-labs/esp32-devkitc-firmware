// DHT11 / DHT22 bit banging on a single GPIO.
//
// Only the electrical part lives here: sending the start pulse and measuring the
// 40 high pulse widths. Decoding happens in sensors/dht.cpp.
#pragma once

#include <Arduino.h>

#include <cstddef>
#include <cstdint>

namespace fw {

class DhtGpioReader {
 public:
  static constexpr size_t kFrameBits = 40;

  void begin(uint8_t pin);

  // True when a full 40 bit frame was measured. `highMicros` receives the high
  // time of every bit, MSB first. Blocks for roughly 5 ms on success.
  bool readFrame(uint32_t* highMicros, size_t capacity, size_t* bitCount);

  // True when the last readFrame() saw the sensor's response pulses, which
  // separates "nothing connected" from "connected but garbled".
  bool responded() const { return responded_; }

  uint8_t pin() const { return pin_; }

 private:
  uint8_t pin_ = 0;
  bool responded_ = false;
};

}  // namespace fw

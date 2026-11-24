#include "hal/dht_gpio.h"

namespace fw {
namespace {

// The start signal has to be low for at least 18 ms on a DHT11; 20 ms keeps the
// DHT22 happy as well.
constexpr uint32_t kStartSignalMs = 20;
// Response pulse (80 us low, 80 us high) and the longest bit pulse (~70 us).
constexpr uint32_t kResponseTimeoutUs = 300;
constexpr uint32_t kBitTimeoutUs = 150;

// PulseIn() first waits for a pulse that is already in progress to end, which
// would silently skip the response high and the first data bit. Poll the pin
// against micros() instead so every edge is seen exactly once.
bool waitForLevel(uint8_t pin, uint8_t level, uint32_t timeoutUs) {
  const uint32_t start = micros();
  while (digitalRead(pin) != level) {
    if ((micros() - start) >= timeoutUs) {
      return false;
    }
  }
  return true;
}

}  // namespace

void DhtGpioReader::begin(uint8_t pin) {
  pin_ = pin;
  pinMode(pin_, INPUT_PULLUP);
}

bool DhtGpioReader::readFrame(uint32_t* highMicros, size_t capacity,
                             size_t* bitCount) {
  if (highMicros == nullptr || bitCount == nullptr || capacity < kFrameBits) {
    return false;
  }
  *bitCount = 0;
  responded_ = false;

  pinMode(pin_, OUTPUT);
  digitalWrite(pin_, LOW);
  delay(kStartSignalMs);
  pinMode(pin_, INPUT_PULLUP);

  // The sensor pulls the line low for ~80 us and then high for ~80 us; after
  // that the 40 data bits follow, each starting with a high pulse.
  if (!waitForLevel(pin_, LOW, kResponseTimeoutUs) ||
      !waitForLevel(pin_, HIGH, kResponseTimeoutUs) ||
      !waitForLevel(pin_, LOW, kResponseTimeoutUs)) {
    return false;
  }
  responded_ = true;

  for (size_t bit = 0; bit < kFrameBits; ++bit) {
    if (!waitForLevel(pin_, HIGH, kBitTimeoutUs)) {
      return false;
    }
    const uint32_t start = micros();
    if (!waitForLevel(pin_, LOW, kBitTimeoutUs)) {
      return false;
    }
    highMicros[bit] = micros() - start;
  }

  *bitCount = kFrameBits;
  return true;
}

}  // namespace fw

#include "hal/dht_gpio.h"

namespace fw {
namespace {

// The start signal has to be low for at least 18 ms on a DHT11; 20 ms keeps the
// DHT22 happy as well.
constexpr uint32_t kStartSignalMs = 20;
// Response pulse (80 us low, 80 us high) and the longest bit pulse (~70 us).
constexpr uint32_t kResponseTimeoutUs = 250;
constexpr uint32_t kBitTimeoutUs = 150;

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

  // The sensor pulls the line low for ~80 us and then high for ~80 us.
  const uint32_t responseLow = pulseIn(pin_, LOW, kResponseTimeoutUs);
  const uint32_t responseHigh = pulseIn(pin_, HIGH, kResponseTimeoutUs);
  if (responseLow == 0 || responseHigh == 0) {
    return false;
  }
  responded_ = true;

  for (size_t bit = 0; bit < kFrameBits; ++bit) {
    const uint32_t high = pulseIn(pin_, HIGH, kBitTimeoutUs);
    if (high == 0) {
      return false;
    }
    highMicros[bit] = high;
    // Skip the following low period before the next bit's high pulse.
    (void)pulseIn(pin_, LOW, kBitTimeoutUs);
  }

  *bitCount = kFrameBits;
  return true;
}

}  // namespace fw

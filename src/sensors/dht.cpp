#include "sensors/dht.h"

namespace fw {
namespace {

constexpr size_t kDhtFrameBits = 40;
constexpr size_t kDhtFrameBytes = kDhtFrameBits / 8;

struct RawFrame {
  uint8_t bytes[kDhtFrameBytes] = {0};
  bool valid = false;
};

RawFrame assembleBytes(const uint32_t* highMicros, size_t bitCount) {
  RawFrame frame;
  if (highMicros == nullptr || bitCount != kDhtFrameBits) {
    return frame;
  }
  for (size_t bit = 0; bit < kDhtFrameBits; ++bit) {
    if (highMicros[bit] >= kDhtOneBitThresholdUs) {
      frame.bytes[bit / 8] |= static_cast<uint8_t>(0x80u >> (bit % 8));
    }
  }
  frame.valid = true;
  return frame;
}

bool checksumMatches(const uint8_t* bytes) {
  uint8_t sum = 0;
  for (size_t index = 0; index < kDhtFrameBytes - 1; ++index) {
    sum = static_cast<uint8_t>(sum + bytes[index]);
  }
  return sum == bytes[kDhtFrameBytes - 1];
}

}  // namespace

const char* dhtTypeName(DhtType type) {
  switch (type) {
    case DhtType::kDht11:
      return "DHT11";
    case DhtType::kDht22:
      return "DHT22";
    case DhtType::kUnknown:
      break;
  }
  return "unknown";
}

bool dhtReadingPlausible(DhtType type, const DhtReading& reading) {
  if (!reading.valid) {
    return false;
  }
  if (reading.humidityPct < 0.0f || reading.humidityPct > 100.0f) {
    return false;
  }
  if (type == DhtType::kDht11) {
    return reading.temperatureC >= -10.0f && reading.temperatureC <= 60.0f;
  }
  if (type == DhtType::kDht22) {
    return reading.temperatureC >= -40.0f && reading.temperatureC <= 85.0f;
  }
  return false;
}

bool decodeDhtFrame(DhtType type, const uint32_t* highMicros, size_t bitCount,
                    DhtReading* out) {
  if (out != nullptr) {
    *out = DhtReading{};
  }
  if (type != DhtType::kDht11 && type != DhtType::kDht22) {
    return false;
  }

  const RawFrame frame = assembleBytes(highMicros, bitCount);
  if (!frame.valid || !checksumMatches(frame.bytes)) {
    return false;
  }

  DhtReading reading;
  if (type == DhtType::kDht11) {
    // 8 bit humidity, 8 bit temperature, decimal bytes are usually zero: the
    // integer bytes are the whole value.
    reading.humidityPct = static_cast<float>(frame.bytes[0]) +
                          static_cast<float>(frame.bytes[1]) / 10.0f;
    reading.temperatureC = static_cast<float>(frame.bytes[2]) +
                           static_cast<float>(frame.bytes[3]) / 10.0f;
  } else {
    const uint16_t humidityRaw =
        static_cast<uint16_t>((frame.bytes[0] << 8) | frame.bytes[1]);
    uint16_t temperatureRaw =
        static_cast<uint16_t>((frame.bytes[2] << 8) | frame.bytes[3]);
    reading.humidityPct = static_cast<float>(humidityRaw) / 10.0f;
    if ((temperatureRaw & 0x8000u) != 0) {
      temperatureRaw = static_cast<uint16_t>(temperatureRaw & 0x7FFFu);
      reading.temperatureC = -static_cast<float>(temperatureRaw) / 10.0f;
    } else {
      reading.temperatureC = static_cast<float>(temperatureRaw) / 10.0f;
    }
  }

  reading.valid = true;
  if (!dhtReadingPlausible(type, reading)) {
    if (out != nullptr) {
      *out = DhtReading{};
    }
    return false;
  }

  if (out != nullptr) {
    *out = reading;
  }
  return true;
}

DhtFrame decodeDhtFrameAuto(const uint32_t* highMicros, size_t bitCount) {
  DhtFrame result;
  // A DHT11 frame read as DHT22 gives a humidity above 100 %, and a real DHT22
  // frame read as DHT11 is usually out of range as well, so trying DHT22 first
  // resolves the family without extra configuration.
  DhtReading reading;
  if (decodeDhtFrame(DhtType::kDht22, highMicros, bitCount, &reading)) {
    result.type = DhtType::kDht22;
    result.reading = reading;
    return result;
  }
  if (decodeDhtFrame(DhtType::kDht11, highMicros, bitCount, &reading)) {
    result.type = DhtType::kDht11;
    result.reading = reading;
    return result;
  }
  return result;
}

}  // namespace fw

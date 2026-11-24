// DHT11 / DHT22 (and AM2302) temperature/humidity decoder.
//
// The timing sensitive part (reading 40 pulse widths from the data line) lives
// in the Arduino layer; decoding the frame, the checksum, the range checks and
// the DHT11/DHT22 auto detection are pure functions tested on the host.
#pragma once

#include <cstddef>
#include <cstdint>

namespace fw {

enum class DhtType {
  kUnknown,
  kDht11,
  kDht22,
};

const char* dhtTypeName(DhtType type);

// A pulse longer than this many microseconds is a "1" bit for both families.
constexpr uint32_t kDhtOneBitThresholdUs = 50;

// One decoded frame.
struct DhtReading {
  bool valid = false;
  float temperatureC = 0.0f;
  float humidityPct = 0.0f;
};

struct DhtFrame {
  DhtType type = DhtType::kUnknown;
  DhtReading reading{};
};

// Decodes 40 high pulse widths. `highMicros[i]` is how long the line stayed
// high for bit i (MSB of the humidity first).
bool decodeDhtFrame(DhtType type, const uint32_t* highMicros, size_t bitCount,
                    DhtReading* out);

// Tries DHT22 first, then DHT11, so a sensor swap needs no rebuild.
DhtFrame decodeDhtFrameAuto(const uint32_t* highMicros, size_t bitCount);

// Plausibility window, shared by the decoders and the station.
bool dhtReadingPlausible(DhtType type, const DhtReading& reading);

}  // namespace fw

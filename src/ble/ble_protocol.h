// BLE payloads: Environmental Sensing values and the HID consumer reports used
// by the media remote mode.
//
// Pure C++ so the encodings (little endian, BLE units) and the button -> media
// action mapping are unit tested on the host. NimBLE only has to send bytes.
#pragma once

#include <cstddef>
#include <cstdint>

#include "sensors/sensor_snapshot.h"

namespace fw {

// 16-bit standard UUIDs for the Environmental Sensing service.
constexpr const char* kBleEnvServiceUuid = "181a";
constexpr const char* kBleTemperatureUuid = "2a6e";
constexpr const char* kBleHumidityUuid = "2a6f";
constexpr const char* kBlePressureUuid = "2a6d";
// Vendor specific status string ("T=..,H=..,P=..,A=..") plus notify.
constexpr const char* kBleStatusUuid = "b7a10001-9c3e-4a5e-9f0e-2f6c9d3b8a41";

enum class MediaAction : uint8_t {
  kNone = 0,
  kVolumeUp,
  kVolumeDown,
  kPlayPause,
  kNextTrack,
  kPreviousTrack,
};

// HID Consumer Page (0x0C) usage id for the action; 0 for kNone.
uint16_t mediaActionUsage(MediaAction action);
const char* mediaActionName(MediaAction action);

// +1 detent -> volume up, -1 detent -> volume down.
MediaAction mediaActionForEncoder(int delta);

// PSH -> play/pause, CON -> next track, BAK -> previous track. Only one button
// is expected per call; PSH wins when several arrive together.
MediaAction mediaActionForButtons(bool push, bool confirm, bool back);

// Two byte little endian consumer report (report id is part of the report map).
size_t encodeHidConsumerReport(MediaAction action, uint8_t* out, size_t capacity);

// BLE Environmental Sensing units: temperature 0.01 C (int16), humidity 0.01 %
// (uint16), pressure 0.1 Pa (uint32). All little endian.
size_t encodeBleTemperature(float celsius, uint8_t* out, size_t capacity);
size_t encodeBleHumidity(float percent, uint8_t* out, size_t capacity);
size_t encodeBlePressure(float hectopascal, uint8_t* out, size_t capacity);

// "T=23.4,H=45.0,P=1006.5,A=1" for the notify characteristic. Missing values
// use '-'. Returns the untruncated length.
size_t formatBleStatus(const SensorSnapshot& sensors, bool alertActive, char* out,
                       size_t outSize);

}  // namespace fw

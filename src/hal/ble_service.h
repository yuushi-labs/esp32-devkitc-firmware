// NimBLE GATT server: Environmental Sensing notifications plus a HID consumer
// control report used as a media remote.
#pragma once

#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "ble/ble_protocol.h"
#include "sensors/sensor_snapshot.h"

namespace fw {

class BleService {
 public:
  // Initialises NimBLE and publishes both services. False when the stack could
  // not start; the rest of the firmware keeps running.
  bool begin();
  void loop(uint32_t nowMs, const SensorSnapshot& sensors, bool alertActive);

  // The media remote only sends HID reports while this is on.
  void setHidMode(bool enabled);
  bool hidMode() const { return hidMode_; }

  bool enabled() const { return enabled_; }
  // True while at least one client is connected. Written from the NimBLE host
  // task, read from the application task, so it is an atomic counter.
  bool connected() const { return connections_.load() > 0; }

  // Queues one consumer report; each queued action is sent as its own
  // press/release pair, in order, from loop().
  bool sendMediaAction(MediaAction action);

  // Called from the NimBLE server callbacks.
  void notifyConnected(bool connected);

 private:
  static constexpr size_t kMediaQueueSize = 4;

  void startAdvertising();
  void updateEnvironment(const SensorSnapshot& sensors, bool alertActive);
  void pumpMediaQueue(uint32_t nowMs);
  void sendReport(uint16_t usage);
  bool queuePush(MediaAction action);
  MediaAction queuePop();
  bool queueEmpty() const { return mediaHead_ == mediaTail_; }
  void clearQueue() { mediaHead_ = 0; mediaTail_ = 0; }

  NimBLEServer* server_ = nullptr;
  NimBLEHIDDevice* hid_ = nullptr;
  NimBLECharacteristic* temperatureChar_ = nullptr;
  NimBLECharacteristic* humidityChar_ = nullptr;
  NimBLECharacteristic* pressureChar_ = nullptr;
  NimBLECharacteristic* statusChar_ = nullptr;
  NimBLECharacteristic* inputReport_ = nullptr;

  bool enabled_ = false;
  bool hidMode_ = false;
  std::atomic<uint8_t> connections_{0};
  bool pressPending_ = false;
  bool reportSent_ = false;
  uint32_t releaseAtMs_ = 0;
  uint32_t lastReportMs_ = 0;
  uint32_t lastNotifyMs_ = 0;

  MediaAction mediaQueue_[kMediaQueueSize] = {};
  size_t mediaHead_ = 0;
  size_t mediaTail_ = 0;
};

}  // namespace fw

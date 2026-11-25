// NimBLE GATT server: Environmental Sensing notifications plus a HID consumer
// control report used as a media remote.
#pragma once

#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>

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
  bool connected() const { return connected_; }

  // Queues one consumer report; the key release follows after a short delay.
  bool sendMediaAction(MediaAction action);

  // Called from the NimBLE server callbacks.
  void notifyConnected(bool connected);

 private:
  void startAdvertising();
  void updateEnvironment(const SensorSnapshot& sensors, bool alertActive);

  NimBLEServer* server_ = nullptr;
  NimBLEHIDDevice* hid_ = nullptr;
  NimBLECharacteristic* temperatureChar_ = nullptr;
  NimBLECharacteristic* humidityChar_ = nullptr;
  NimBLECharacteristic* pressureChar_ = nullptr;
  NimBLECharacteristic* statusChar_ = nullptr;
  NimBLECharacteristic* inputReport_ = nullptr;

  bool enabled_ = false;
  bool hidMode_ = false;
  bool connected_ = false;
  bool releasePending_ = false;
  uint32_t releaseAtMs_ = 0;
  uint32_t lastNotifyMs_ = 0;
};

}  // namespace fw

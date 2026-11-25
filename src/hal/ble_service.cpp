#include "hal/ble_service.h"

#include <Arduino.h>
#include <NimBLE2904.h>

#include <cstring>

#include "app_config.h"

namespace fw {
namespace {

// HID consumer control: report id 1, one 16 bit usage from the Consumer page.
uint8_t kReportMap[] = {
    0x05, 0x0C,        // Usage Page (Consumer)
    0x09, 0x01,        // Usage (Consumer Control)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        //   Report ID (1)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x03,  //   Logical Maximum (1023)
    0x19, 0x00,        //   Usage Minimum (0)
    0x2A, 0xFF, 0x03,  //   Usage Maximum (1023)
    0x75, 0x10,        //   Report Size (16)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x00,        //   Input (Data, Array, Absolute)
    0xC0,              // End Collection
};

constexpr uint16_t kAppearanceGenericHid = 0x03C0;
constexpr uint16_t kUnitCelsius = 0x272F;
constexpr uint16_t kUnitPercent = 0x27AD;
constexpr uint16_t kUnitPascal = 0x2724;

class BleServerCallbacks : public NimBLEServerCallbacks {
 public:
  explicit BleServerCallbacks(BleService* owner) : owner_(owner) {}

  void onConnect(NimBLEServer* server, NimBLEConnInfo& info) override {
    (void)server;
    (void)info;
    owner_->notifyConnected(true);
  }

  void onDisconnect(NimBLEServer* server, NimBLEConnInfo& info, int reason) override {
    (void)server;
    (void)info;
    (void)reason;
    owner_->notifyConnected(false);
  }

 private:
  BleService* owner_;
};

}  // namespace

bool BleService::begin() {
  if (enabled_) {
    return true;
  }
  if (!NimBLEDevice::init(kBleDeviceName)) {
    Serial.println("ble: init failed");
    return false;
  }
  NimBLEDevice::setSecurityAuth(true, false, true);

  server_ = NimBLEDevice::createServer();
  if (server_ == nullptr) {
    Serial.println("ble: createServer failed");
    return false;
  }
  server_->setCallbacks(new BleServerCallbacks(this));
  server_->advertiseOnDisconnect(true);

  NimBLEService* envService = server_->createService(kBleEnvServiceUuid);
  if (envService == nullptr) {
    Serial.println("ble: env service failed");
    return false;
  }
  temperatureChar_ = envService->createCharacteristic(
      kBleTemperatureUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  humidityChar_ = envService->createCharacteristic(
      kBleHumidityUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  pressureChar_ = envService->createCharacteristic(
      kBlePressureUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  statusChar_ = envService->createCharacteristic(
      kBleStatusUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);

  // The descriptor exponent must match the encoded payload: 0.01 C, 0.01 % and
  // 0.1 Pa. Without it a client would read 23.4 C as 2340 C.
  NimBLE2904* temperatureDescriptor = temperatureChar_->create2904();
  temperatureDescriptor->setFormat(NimBLE2904::FORMAT_SINT16);
  temperatureDescriptor->setExponent(-2);
  temperatureDescriptor->setUnit(kUnitCelsius);
  NimBLE2904* humidityDescriptor = humidityChar_->create2904();
  humidityDescriptor->setFormat(NimBLE2904::FORMAT_UINT16);
  humidityDescriptor->setExponent(-2);
  humidityDescriptor->setUnit(kUnitPercent);
  NimBLE2904* pressureDescriptor = pressureChar_->create2904();
  pressureDescriptor->setFormat(NimBLE2904::FORMAT_UINT32);
  pressureDescriptor->setExponent(-1);
  pressureDescriptor->setUnit(kUnitPascal);

  hid_ = new NimBLEHIDDevice(server_);
  hid_->setReportMap(kReportMap, sizeof(kReportMap));
  hid_->setManufacturer("yuushi-labs");
  hid_->setPnp(0x02, 0x0A12, 0x0001, 0x0100);
  hid_->setHidInfo(0x00, 0x02);
  hid_->setBatteryLevel(100);
  inputReport_ = hid_->getInputReport(1);

  if (!server_->start()) {
    Serial.println("ble: server start failed");
    return false;
  }
  enabled_ = true;
  startAdvertising();
  Serial.printf("ble: %s ready (%s)\n", kBleDeviceName,
                inputReport_ != nullptr ? "hid ok" : "hid missing");
  return true;
}

void BleService::startAdvertising() {
  if (!enabled_) {
    return;
  }
  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  if (advertising == nullptr) {
    return;
  }
  advertising->stop();
  advertising->reset();
  advertising->addServiceUUID(kBleEnvServiceUuid);
  if (hidMode_) {
    advertising->addServiceUUID("1812");
  }
  advertising->setName(kBleDeviceName);
  advertising->setAppearance(kAppearanceGenericHid);
  advertising->enableScanResponse(true);
  advertising->start();
  Serial.printf("ble: advertising%s\n", hidMode_ ? " (hid)" : "");
}

void BleService::setHidMode(bool enabled) {
  if (!enabled_ || hidMode_ == enabled) {
    return;
  }
  if (!enabled) {
    // Never leave a key held on the host when the remote is switched off.
    if (pressPending_) {
      sendReport(0);
      pressPending_ = false;
    }
    clearQueue();
  }
  hidMode_ = enabled;
  startAdvertising();
  Serial.printf("ble: hid mode %s\n", hidMode_ ? "on" : "off");
}

void BleService::notifyConnected(bool connected) {
  if (connected) {
    const uint8_t current = connections_.load();
    if (current < 0xFF) {
      connections_.store(static_cast<uint8_t>(current + 1));
    }
  } else {
    const uint8_t current = connections_.load();
    connections_.store(current > 0 ? static_cast<uint8_t>(current - 1) : 0);
    if (connections_.load() == 0) {
      // The link is gone, so nothing can be held down any more. The queue is
      // owned by the application task, so only ask it to drop the state.
      linkLost_.store(true);
    }
  }
  Serial.printf("ble: %s (%u link(s))\n", connected ? "client connected" : "client disconnected",
                static_cast<unsigned>(connections_.load()));
}

void BleService::updateEnvironment(const SensorSnapshot& sensors, bool alertActive) {
  uint8_t payload[4];
  if (sensors.hasTemperature()) {
    const size_t length =
        encodeBleTemperature(sensors.displayTemperatureC(), payload, sizeof(payload));
    if (length > 0) {
      temperatureChar_->setValue(payload, length);
      temperatureChar_->notify();
    }
  }
  if (sensors.hasHumidity()) {
    const size_t length = encodeBleHumidity(sensors.humidityPct, payload, sizeof(payload));
    if (length > 0) {
      humidityChar_->setValue(payload, length);
      humidityChar_->notify();
    }
  }
  if (sensors.hasPressure()) {
    const size_t length = encodeBlePressure(sensors.pressureHpa, payload, sizeof(payload));
    if (length > 0) {
      pressureChar_->setValue(payload, length);
      pressureChar_->notify();
    }
  }
  char status[48];
  formatBleStatus(sensors, alertActive, status, sizeof(status));
  statusChar_->setValue(reinterpret_cast<const uint8_t*>(status), std::strlen(status));
  statusChar_->notify();
}

void BleService::sendReport(uint16_t usage) {
  if (inputReport_ == nullptr || !connected()) {
    return;
  }
  // Little endian consumer usage, report id 1 (see the report map).
  const uint8_t report[2] = {static_cast<uint8_t>(usage & 0xFF),
                             static_cast<uint8_t>((usage >> 8) & 0xFF)};
  inputReport_->setValue(report, sizeof(report));
  inputReport_->notify();
}

bool BleService::queuePush(MediaAction action) {
  const size_t next = (mediaTail_ + 1) % kMediaQueueSize;
  if (next == mediaHead_) {
    return false;
  }
  mediaQueue_[mediaTail_] = action;
  mediaTail_ = next;
  return true;
}

MediaAction BleService::queuePop() {
  if (queueEmpty()) {
    return MediaAction::kNone;
  }
  const MediaAction action = mediaQueue_[mediaHead_];
  mediaHead_ = (mediaHead_ + 1) % kMediaQueueSize;
  return action;
}

void BleService::pumpMediaQueue(uint32_t nowMs) {
  if (!enabled_ || inputReport_ == nullptr || !connected()) {
    return;
  }
  if (pressPending_) {
    if (static_cast<int32_t>(nowMs - releaseAtMs_) < 0) {
      return;
    }
    sendReport(0);
    pressPending_ = false;
    reportSent_ = true;
    lastReportMs_ = nowMs;
    return;
  }
  if (queueEmpty()) {
    return;
  }
  if (reportSent_ && (nowMs - lastReportMs_) < kBleHidReportGapMs) {
    return;
  }
  const MediaAction action = queuePop();
  uint8_t report[2] = {0, 0};
  if (encodeHidConsumerReport(action, report, sizeof(report)) != sizeof(report)) {
    return;
  }
  sendReport(static_cast<uint16_t>(report[0] | (report[1] << 8)));
  pressPending_ = true;
  reportSent_ = true;
  lastReportMs_ = nowMs;
  releaseAtMs_ = nowMs + kBleHidReleaseMs;
  Serial.printf("ble: hid %s\n", mediaActionName(action));
}

void BleService::loop(uint32_t nowMs, const SensorSnapshot& sensors, bool alertActive) {
  if (!enabled_) {
    return;
  }
  if (linkLost_.exchange(false)) {
    pressPending_ = false;
    clearQueue();
  }
  pumpMediaQueue(nowMs);
  if (!connected()) {
    return;
  }
  if ((nowMs - lastNotifyMs_) >= kBleNotifyIntervalMs) {
    lastNotifyMs_ = nowMs;
    updateEnvironment(sensors, alertActive);
  }
}

bool BleService::sendMediaAction(MediaAction action) {
  if (!enabled_ || !connected() || inputReport_ == nullptr ||
      action == MediaAction::kNone) {
    return false;
  }
  if (!queuePush(action)) {
    Serial.println("ble: hid queue full");
    return false;
  }
  // Send immediately when the line is idle; otherwise loop() drains the queue.
  pumpMediaQueue(millis());
  return true;
}

}  // namespace fw

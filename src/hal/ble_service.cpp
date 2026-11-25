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

  NimBLE2904* temperatureDescriptor = temperatureChar_->create2904();
  temperatureDescriptor->setFormat(NimBLE2904::FORMAT_SINT16);
  temperatureDescriptor->setUnit(kUnitCelsius);
  NimBLE2904* humidityDescriptor = humidityChar_->create2904();
  humidityDescriptor->setFormat(NimBLE2904::FORMAT_UINT16);
  humidityDescriptor->setUnit(kUnitPercent);
  NimBLE2904* pressureDescriptor = pressureChar_->create2904();
  pressureDescriptor->setFormat(NimBLE2904::FORMAT_UINT32);
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
  hidMode_ = enabled;
  startAdvertising();
  Serial.printf("ble: hid mode %s\n", hidMode_ ? "on" : "off");
}

void BleService::notifyConnected(bool connected) {
  connected_ = connected;
  Serial.printf("ble: %s\n", connected ? "client connected" : "client disconnected");
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

void BleService::loop(uint32_t nowMs, const SensorSnapshot& sensors, bool alertActive) {
  if (!enabled_) {
    return;
  }
  if (releasePending_ && nowMs >= releaseAtMs_) {
    releasePending_ = false;
    if (inputReport_ != nullptr) {
      const uint8_t release[2] = {0, 0};
      inputReport_->setValue(release, sizeof(release));
      inputReport_->notify();
    }
  }
  if (!connected_) {
    return;
  }
  if ((nowMs - lastNotifyMs_) >= kBleNotifyIntervalMs) {
    lastNotifyMs_ = nowMs;
    updateEnvironment(sensors, alertActive);
  }
}

bool BleService::sendMediaAction(MediaAction action) {
  if (!enabled_ || !connected_ || inputReport_ == nullptr ||
      action == MediaAction::kNone) {
    return false;
  }
  uint8_t report[2] = {0, 0};
  if (encodeHidConsumerReport(action, report, sizeof(report)) != sizeof(report)) {
    return false;
  }
  inputReport_->setValue(report, sizeof(report));
  inputReport_->notify();
  releasePending_ = true;
  releaseAtMs_ = millis() + kBleHidReleaseMs;
  Serial.printf("ble: hid %s\n", mediaActionName(action));
  return true;
}

}  // namespace fw

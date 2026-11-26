// Wi-Fi station / setup access point, stored credentials and NTP.
//
// The manager never blocks: a missing network leaves the station firmware
// running offline and retries in the background, exactly like SPEC.md §6 asks.
// SSID scans for the setup page run asynchronously and land in a small cache.
//
// The saved network lives in /wifi.json on LittleFS; the older NVS copy is
// still read and migrated to the file on the first boot after the upgrade, so
// a station set up with v0.4.0 keeps its network.
#pragma once

#include <Arduino.h>
#include <DNSServer.h>
#include <WiFi.h>

#include <cstddef>
#include <cstdint>

#include "app_config.h"
#include "net/ntp_clock.h"
#include "net/wifi_credentials.h"
#include "net/wifi_store.h"

namespace fw {

class WifiManager {
 public:
  void begin();
  void loop(uint32_t nowMs);

  bool loadCredentials(WifiCredentials* out) const;
  bool saveCredentials(const WifiCredentials& credentials);
  void clearCredentials();
  bool hasStoredCredentials() const { return credentials_.valid; }

  // Where the saved network is kept and whether the file system is mounted.
  WifiConfigSource configSource() const { return configSource_; }
  const char* configPath() const { return kWifiConfigPath; }
  bool littlefsReady() const { return littlefsReady_; }

  void startAccessPoint();
  void startStation();

  WifiMode mode() const { return mode_; }
  bool connected() const;
  bool apActive() const { return mode_ == WifiMode::kAccessPoint; }
  const char* ip() const { return ip_; }
  const char* ssid() const;
  int8_t rssi() const;

  // WPA2 key for the setup AP, regenerated on every start and shown on the OLED.
  const char* setupApPassword() const { return setupApPassword_; }

  bool ntpSynced() const { return clock_.synced(); }
  int64_t epochSeconds() const;

  // Starts a background scan (AP mode only) and returns immediately. The setup
  // page reads whatever the last completed scan found.
  void requestScan(uint32_t nowMs);
  // Copies up to `maxCount` unique SSIDs (each at most 32 chars) from the last
  // completed scan. Returns the number written and never blocks.
  size_t cachedNetworks(char (*ssids)[kWifiSsidMaxLength + 1], size_t maxCount) const;
  bool scanInProgress() const { return scanning_; }

  const NtpClock& clock() const { return clock_; }

 private:
  // Reads /wifi.json first and falls back to NVS, without writing anything.
  bool loadStored(WifiCredentials* out, WifiConfigSource* source,
                  bool* migrate) const;
  bool readConfigFile(char* out, size_t outSize) const;
  bool writeConfigFile(const char* text);
  void restoreConfigFile(const char* previous, bool hadFile);
  bool readNvsConfig(char* ssid, size_t ssidSize, char* password,
                     size_t passwordSize) const;
  bool writeNvsConfig(const WifiCredentials& credentials);
  void updateIp();
  void startNtp();
  void generateSetupPassword();
  void collectScanResults(int16_t found);
  void rebaseClock(uint32_t nowMs);

  WifiMode mode_ = WifiMode::kOff;
  WifiCredentials credentials_;
  WifiConfigSource configSource_ = WifiConfigSource::kNone;
  DNSServer dns_;
  NtpClock clock_;
  char ip_[16] = "0.0.0.0";
  char setupApPassword_[kSetupApPasswordLength + 1] = {0};
  char networks_[12][kWifiSsidMaxLength + 1] = {};
  uint8_t networkCount_ = 0;
  bool scanning_ = false;
  bool dnsStarted_ = false;
  bool ntpStarted_ = false;
  bool wasConnected_ = false;
  bool littlefsReady_ = false;
  uint32_t connectStartedMs_ = 0;
  uint32_t lastRetryMs_ = 0;
  uint32_t lastScanMs_ = 0;
  uint32_t lastRebaseMs_ = 0;
};

}  // namespace fw

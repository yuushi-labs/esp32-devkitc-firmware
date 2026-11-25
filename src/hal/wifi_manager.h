// Wi-Fi station / setup access point, NVS credentials and NTP.
//
// The manager never blocks: a missing network leaves the station firmware
// running offline and retries in the background, exactly like SPEC.md §6 asks.
#pragma once

#include <Arduino.h>
#include <DNSServer.h>
#include <WiFi.h>

#include <cstddef>
#include <cstdint>

#include "app_config.h"
#include "net/ntp_clock.h"
#include "net/wifi_credentials.h"

namespace fw {

class WifiManager {
 public:
  void begin();
  void loop(uint32_t nowMs);

  bool loadCredentials(WifiCredentials* out) const;
  bool saveCredentials(const WifiCredentials& credentials);
  void clearCredentials();

  void startAccessPoint();
  void startStation();

  WifiMode mode() const { return mode_; }
  bool connected() const;
  bool apActive() const { return mode_ == WifiMode::kAccessPoint; }
  const char* ip() const { return ip_; }
  const char* ssid() const;
  int8_t rssi() const;

  bool ntpSynced() const { return clock_.synced(); }
  int64_t epochSeconds() const;

  // Fills up to `maxCount` unique SSIDs (each at most 32 chars). Returns the
  // number written. Blocking for roughly two seconds.
  size_t scanNetworks(char (*ssids)[kWifiSsidMaxLength + 1], size_t maxCount);

  const NtpClock& clock() const { return clock_; }

 private:
  void updateIp();
  void startNtp();

  WifiMode mode_ = WifiMode::kOff;
  WifiCredentials credentials_;
  DNSServer dns_;
  NtpClock clock_;
  char ip_[16] = "0.0.0.0";
  bool dnsStarted_ = false;
  bool ntpStarted_ = false;
  bool wasConnected_ = false;
  uint32_t connectStartedMs_ = 0;
  uint32_t lastRetryMs_ = 0;
};

}  // namespace fw

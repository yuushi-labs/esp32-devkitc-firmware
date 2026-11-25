#include "hal/wifi_manager.h"

#include <Preferences.h>
#include <esp_random.h>
#include <time.h>

#include <cstdio>
#include <cstring>

namespace fw {
namespace {

constexpr const char* kPreferencesNamespace = "wifi";
constexpr const char* kSsidKey = "ssid";
constexpr const char* kPasswordKey = "password";
// Sanity floor for "the NTP reply is real" (2020-09-13).
constexpr time_t kPlausibleEpoch = 1600000000;
// Re-base the clock base well before the millisecond counter can wrap between
// two syncs (2^32 ms == 49.7 days).
constexpr uint32_t kClockRebaseIntervalMs = 3600000;

void copyString(char* out, size_t outSize, const char* value) {
  if (out == nullptr || outSize == 0) {
    return;
  }
  std::snprintf(out, outSize, "%s", value != nullptr ? value : "");
}

// Puts a key back exactly as it was, or removes it when it did not exist: a
// failed write must not leave a new SSID next to an old password.
void restoreKey(Preferences& preferences, const char* key, const String& previous) {
  if (previous.length() == 0) {
    preferences.remove(key);
  } else {
    preferences.putString(key, previous);
  }
}

}  // namespace

void WifiManager::begin() {
  WifiCredentials stored;
  if (loadCredentials(&stored) && stored.valid) {
    credentials_ = stored;
    startStation();
  } else {
    startAccessPoint();
  }
}

bool WifiManager::loadCredentials(WifiCredentials* out) const {
  if (out == nullptr) {
    return false;
  }
  *out = WifiCredentials{};
  Preferences preferences;
  if (!preferences.begin(kPreferencesNamespace, true)) {
    return false;
  }
  const String ssid = preferences.getString(kSsidKey, "");
  const String password = preferences.getString(kPasswordKey, "");
  preferences.end();
  return makeWifiCredentials(ssid.c_str(), password.c_str(), out) ==
         WifiValidation::kOk;
}

bool WifiManager::saveCredentials(const WifiCredentials& credentials) {
  if (!credentials.valid) {
    return false;
  }
  Preferences preferences;
  if (!preferences.begin(kPreferencesNamespace, false)) {
    return false;
  }
  // Each putString() commits on its own, so remember the previous pair and put
  // it back when the second write fails instead of leaving a split record.
  const String previousSsid = preferences.getString(kSsidKey, "");
  const String previousPassword = preferences.getString(kPasswordKey, "");
  const size_t ssidWritten = preferences.putString(kSsidKey, credentials.ssid);
  const size_t passwordWritten =
      preferences.putString(kPasswordKey, credentials.password);
  const bool ok = ssidWritten == std::strlen(credentials.ssid) &&
                  passwordWritten == std::strlen(credentials.password);
  if (!ok) {
    restoreKey(preferences, kSsidKey, previousSsid);
    restoreKey(preferences, kPasswordKey, previousPassword);
  }
  preferences.end();
  if (!ok) {
    return false;
  }
  credentials_ = credentials;
  startStation();
  return true;
}

void WifiManager::clearCredentials() {
  Preferences preferences;
  if (preferences.begin(kPreferencesNamespace, false)) {
    preferences.remove(kSsidKey);
    preferences.remove(kPasswordKey);
    preferences.end();
  }
  credentials_ = WifiCredentials{};
  startAccessPoint();
}

void WifiManager::generateSetupPassword() {
  // 8 characters from the hardware RNG: WPA2 accepts it and it is short enough
  // to type from the panel. Reading it off the OLED also proves the person
  // setting the station up is holding the device.
  const uint32_t value = esp_random();
  std::snprintf(setupApPassword_, sizeof(setupApPassword_), "%08lx",
                static_cast<unsigned long>(value));
}

void WifiManager::startAccessPoint() {
  if (dnsStarted_) {
    dns_.stop();
    dnsStarted_ = false;
  }
  WiFi.disconnect(true);
  // AP+STA keeps the radio able to scan nearby networks for the setup form.
  WiFi.mode(WIFI_AP_STA);
  generateSetupPassword();
  WiFi.softAP(kSetupApSsid, setupApPassword_);
  mode_ = WifiMode::kAccessPoint;
  ntpStarted_ = false;
  wasConnected_ = false;
  scanning_ = false;
  networkCount_ = 0;
  dns_.setErrorReplyCode(DNSReplyCode::NoError);
  dnsStarted_ = dns_.start(53, "*", WiFi.softAPIP());
  updateIp();
  Serial.printf("wifi: setup AP '%s' ip=%s pass=%s dns=%s\n", kSetupApSsid, ip_,
                setupApPassword_, dnsStarted_ ? "on" : "failed");
}

void WifiManager::startStation() {
  if (dnsStarted_) {
    dns_.stop();
    dnsStarted_ = false;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  if (credentials_.valid) {
    WiFi.begin(credentials_.ssid, credentials_.password);
    connectStartedMs_ = millis();
    lastRetryMs_ = millis();
  }
  mode_ = WifiMode::kStation;
  ntpStarted_ = false;
  wasConnected_ = false;
  scanning_ = false;
  updateIp();
  Serial.printf("wifi: connecting to '%s'\n", credentials_.ssid);
}

void WifiManager::startNtp() {
  configTime(kLocalUtcOffsetSeconds, 0, kNtpServer1, kNtpServer2);
  ntpStarted_ = true;
  Serial.printf("wifi: NTP sync started (%s, %s)\n", kNtpServer1, kNtpServer2);
}

void WifiManager::rebaseClock(uint32_t nowMs) {
  if (!clock_.synced() || (nowMs - lastRebaseMs_) < kClockRebaseIntervalMs) {
    return;
  }
  // Keep the millisecond base recent while the network is away, so epochAt()
  // never has to span a 32 bit wrap.
  clock_.markSynced(clock_.epochAt(nowMs), nowMs);
  lastRebaseMs_ = nowMs;
}

void WifiManager::loop(uint32_t nowMs) {
  rebaseClock(nowMs);

  if (mode_ == WifiMode::kAccessPoint) {
    if (dnsStarted_) {
      dns_.processNextRequest();
    }
    if (scanning_) {
      const int16_t found = WiFi.scanComplete();
      if (found >= 0) {
        collectScanResults(found);
        scanning_ = false;
      } else if (found == WIFI_SCAN_FAILED) {
        scanning_ = false;
        Serial.println("wifi: scan failed");
      }
    } else if ((nowMs - lastScanMs_) >= kSetupScanIntervalMs) {
      requestScan(nowMs);
    }
    return;
  }
  if (mode_ != WifiMode::kStation) {
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (!wasConnected_) {
      wasConnected_ = true;
      updateIp();
      Serial.printf("wifi: connected ssid=%s ip=%s rssi=%d\n", WiFi.SSID().c_str(), ip_,
                    WiFi.RSSI());
      startNtp();
    }
    if (!ntpStarted_) {
      startNtp();
    }
    const time_t now = time(nullptr);
    if (now >= kPlausibleEpoch) {
      clock_.markSynced(static_cast<int64_t>(now), nowMs);
    }
    return;
  }

  wasConnected_ = false;
  updateIp();
  if (credentials_.valid &&
      (nowMs - connectStartedMs_) >= kWifiConnectTimeoutMs &&
      (nowMs - lastRetryMs_) >= kWifiRetryIntervalMs) {
    lastRetryMs_ = nowMs;
    Serial.printf("wifi: reconnect to '%s'\n", credentials_.ssid);
    WiFi.disconnect();
    WiFi.begin(credentials_.ssid, credentials_.password);
  }
}

bool WifiManager::connected() const {
  return mode_ == WifiMode::kStation && WiFi.status() == WL_CONNECTED;
}

const char* WifiManager::ssid() const {
  return credentials_.ssid[0] != '\0' ? credentials_.ssid : "";
}

int8_t WifiManager::rssi() const {
  return connected() ? static_cast<int8_t>(WiFi.RSSI()) : 0;
}

int64_t WifiManager::epochSeconds() const {
  if (clock_.synced()) {
    return clock_.epochAt(millis());
  }
  const time_t now = time(nullptr);
  return now >= kPlausibleEpoch ? static_cast<int64_t>(now) : 0;
}

void WifiManager::updateIp() {
  IPAddress address;
  if (mode_ == WifiMode::kAccessPoint) {
    address = WiFi.softAPIP();
  } else {
    address = WiFi.localIP();
  }
  char text[16];
  std::snprintf(text, sizeof(text), "%u.%u.%u.%u", address[0], address[1], address[2],
                address[3]);
  copyString(ip_, sizeof(ip_), text);
}

void WifiManager::requestScan(uint32_t nowMs) {
  if (mode_ != WifiMode::kAccessPoint || scanning_) {
    return;
  }
  lastScanMs_ = nowMs;
  // Asynchronous: the caller (the HTTP handler) returns at once and the results
  // are picked up by loop().
  const int16_t started = WiFi.scanNetworks(true);
  scanning_ = started == WIFI_SCAN_RUNNING;
  if (!scanning_) {
    Serial.printf("wifi: scan did not start (%d)\n", static_cast<int>(started));
  }
}

void WifiManager::collectScanResults(int16_t found) {
  networkCount_ = 0;
  for (int16_t index = 0; index < found && networkCount_ < 12; ++index) {
    const String name = WiFi.SSID(index);
    if (name.length() == 0 || name.length() > kWifiSsidMaxLength) {
      continue;
    }
    bool duplicate = false;
    for (uint8_t existing = 0; existing < networkCount_; ++existing) {
      if (name.equals(networks_[existing])) {
        duplicate = true;
        break;
      }
    }
    if (duplicate) {
      continue;
    }
    copyString(networks_[networkCount_], kWifiSsidMaxLength + 1, name.c_str());
    ++networkCount_;
  }
  WiFi.scanDelete();
  Serial.printf("wifi: %u network(s) found\n", static_cast<unsigned>(networkCount_));
}

size_t WifiManager::cachedNetworks(char (*ssids)[kWifiSsidMaxLength + 1],
                                   size_t maxCount) const {
  if (ssids == nullptr || maxCount == 0) {
    return 0;
  }
  size_t count = 0;
  for (uint8_t index = 0; index < networkCount_ && count < maxCount; ++index) {
    copyString(ssids[count], kWifiSsidMaxLength + 1, networks_[index]);
    ++count;
  }
  return count;
}

}  // namespace fw

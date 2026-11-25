#include "hal/wifi_manager.h"

#include <Preferences.h>
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

void copyString(char* out, size_t outSize, const char* value) {
  if (out == nullptr || outSize == 0) {
    return;
  }
  std::snprintf(out, outSize, "%s", value != nullptr ? value : "");
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
  const size_t ssidWritten = preferences.putString(kSsidKey, credentials.ssid);
  const size_t passwordWritten =
      preferences.putString(kPasswordKey, credentials.password);
  preferences.end();
  const bool ok = ssidWritten == std::strlen(credentials.ssid) &&
                  passwordWritten == std::strlen(credentials.password);
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

void WifiManager::startAccessPoint() {
  if (dnsStarted_) {
    dns_.stop();
    dnsStarted_ = false;
  }
  WiFi.disconnect(true);
  // AP+STA keeps the radio able to scan nearby networks for the setup form.
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(kSetupApSsid, kSetupApPassword[0] != '\0' ? kSetupApPassword : nullptr);
  mode_ = WifiMode::kAccessPoint;
  ntpStarted_ = false;
  wasConnected_ = false;
  dns_.setErrorReplyCode(DNSReplyCode::NoError);
  dnsStarted_ = dns_.start(53, "*", WiFi.softAPIP());
  updateIp();
  Serial.printf("wifi: setup AP '%s' ip=%s dns=%s\n", kSetupApSsid, ip_,
                dnsStarted_ ? "on" : "failed");
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
  updateIp();
  Serial.printf("wifi: connecting to '%s'\n", credentials_.ssid);
}

void WifiManager::startNtp() {
  configTime(kLocalUtcOffsetSeconds, 0, kNtpServer1, kNtpServer2);
  ntpStarted_ = true;
  Serial.printf("wifi: NTP sync started (%s, %s)\n", kNtpServer1, kNtpServer2);
}

void WifiManager::loop(uint32_t nowMs) {
  if (mode_ == WifiMode::kAccessPoint) {
    if (dnsStarted_) {
      dns_.processNextRequest();
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

size_t WifiManager::scanNetworks(char (*ssids)[kWifiSsidMaxLength + 1],
                                 size_t maxCount) {
  if (ssids == nullptr || maxCount == 0) {
    return 0;
  }
  const int found = WiFi.scanNetworks();
  size_t count = 0;
  for (int index = 0; index < found && count < maxCount; ++index) {
    const String name = WiFi.SSID(index);
    if (name.length() == 0 || name.length() > kWifiSsidMaxLength) {
      continue;
    }
    bool duplicate = false;
    for (size_t existing = 0; existing < count; ++existing) {
      if (name.equals(ssids[existing])) {
        duplicate = true;
        break;
      }
    }
    if (duplicate) {
      continue;
    }
    copyString(ssids[count], kWifiSsidMaxLength + 1, name.c_str());
    ++count;
  }
  WiFi.scanDelete();
  return count;
}

}  // namespace fw

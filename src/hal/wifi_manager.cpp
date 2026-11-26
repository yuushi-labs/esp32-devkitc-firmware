#include "hal/wifi_manager.h"

#include <LittleFS.h>
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

// Mounts LittleFS, formatting a brand new board when `format` is set. The log
// recorder usually mounts it first; this keeps the Wi-Fi store independent.
bool ensureFileSystem(bool format) {
  if (LittleFS.begin(false)) {
    return true;
  }
  return format && LittleFS.begin(true);
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
  littlefsReady_ = ensureFileSystem(true);

  WifiCredentials stored;
  WifiConfigSource source = WifiConfigSource::kNone;
  bool migrate = false;
  const bool haveStored = loadStored(&stored, &source, &migrate);
  configSource_ = source;

  if (migrate && stored.valid) {
    // v0.4.0 and older saved the pair in NVS only. Copy it to /wifi.json once
    // so every later boot reads the file.
    char json[kWifiConfigMaxBytes];
    const size_t needed = formatWifiConfigJson(stored, json, sizeof(json));
    if (needed > 0 && needed < sizeof(json) && writeConfigFile(json)) {
      configSource_ = WifiConfigSource::kLittleFs;
      Serial.printf("wifi: migrated the saved network from nvs to %s\n",
                    kWifiConfigPath);
    } else {
      Serial.println(
          "wifi: could not write the saved network to littlefs (keeping nvs)");
    }
  }

  if (haveStored && stored.valid) {
    credentials_ = stored;
    startStation();
  } else {
    startAccessPoint();
  }
}

bool WifiManager::loadStored(WifiCredentials* out, WifiConfigSource* source,
                             bool* migrate) const {
  if (out != nullptr) {
    *out = WifiCredentials{};
  }
  char text[kWifiConfigMaxBytes] = {0};
  const bool fileRead = readConfigFile(text, sizeof(text));

  char nvsSsid[kWifiSsidMaxLength + 1] = {0};
  char nvsPassword[kWifiPasswordMaxLength + 1] = {0};
  const bool nvsPresent =
      readNvsConfig(nvsSsid, sizeof(nvsSsid), nvsPassword, sizeof(nvsPassword));

  const WifiConfigChoice choice =
      chooseWifiConfig(fileRead && text[0] != '\0' ? text : nullptr, nvsPresent,
                       nvsSsid, nvsPassword);
  if (choice.fileRejected) {
    Serial.println("wifi: /wifi.json could not be read, ignoring it");
  }
  if (out != nullptr) {
    *out = choice.credentials;
  }
  if (source != nullptr) {
    *source = choice.source;
  }
  if (migrate != nullptr) {
    *migrate = choice.migrate;
  }
  return choice.credentials.valid;
}

bool WifiManager::loadCredentials(WifiCredentials* out) const {
  if (out == nullptr) {
    return false;
  }
  return loadStored(out, nullptr, nullptr);
}

bool WifiManager::readFileInto(const char* path, char* out, size_t outSize) const {
  if (path == nullptr || out == nullptr || outSize == 0) {
    return false;
  }
  out[0] = '\0';
  File file = LittleFS.open(path, FILE_READ);
  if (!file) {
    return false;
  }
  const size_t size = file.size();
  // A file larger than the buffer is rejected instead of accepted by prefix:
  // the rest could hold anything, and a truncated pair is not the saved pair.
  if (size == 0 || size >= outSize) {
    file.close();
    return false;
  }
  const size_t read = file.readBytes(out, size);
  file.close();
  if (read != size) {
    out[0] = '\0';
    return false;
  }
  out[size] = '\0';
  if (std::strlen(out) != size) {
    // An embedded NUL would make the parser stop in the middle of the file.
    out[0] = '\0';
    return false;
  }
  return true;
}

bool WifiManager::readConfigFile(char* out, size_t outSize) const {
  if (out == nullptr || outSize == 0) {
    return false;
  }
  out[0] = '\0';
  if (!ensureFileSystem(false)) {
    return false;
  }
  if (readFileInto(kWifiConfigPath, out, outSize)) {
    return true;
  }
  // A reset between writing /wifi.json.tmp and renaming it leaves a complete
  // new pair behind. Adopt it when it parses instead of falling back to NVS.
  char staged[kWifiConfigMaxBytes] = {0};
  if (!readFileInto(kWifiConfigTempPath, staged, sizeof(staged))) {
    return false;
  }
  WifiCredentials check;
  if (!parseWifiConfigJson(staged, &check)) {
    return false;
  }
  if (!LittleFS.exists(kWifiConfigPath)) {
    LittleFS.rename(kWifiConfigTempPath, kWifiConfigPath);
  }
  copyString(out, outSize, staged);
  return true;
}

bool WifiManager::writeRawFile(const char* path, const char* text) {
  if (path == nullptr || text == nullptr) {
    return false;
  }
  if (!ensureFileSystem(true)) {
    return false;
  }
  File file = LittleFS.open(path, FILE_WRITE);
  if (!file) {
    littlefsReady_ = false;
    return false;
  }
  const size_t written = file.print(text);
  const bool writeFailed = file.getWriteError();
  file.close();
  return !writeFailed && written == std::strlen(text);
}

bool WifiManager::writeConfigFile(const char* text) {
  if (text == nullptr) {
    return false;
  }
  // Write a temporary file first: the pair is complete on flash before the old
  // /wifi.json is replaced.
  if (!writeRawFile(kWifiConfigTempPath, text)) {
    LittleFS.remove(kWifiConfigTempPath);
    return false;
  }
  // Check the staged copy before it replaces the live file: a short flash write
  // must fail while the previous /wifi.json still exists.
  char staged[kWifiConfigMaxBytes] = {0};
  if (!readFileInto(kWifiConfigTempPath, staged, sizeof(staged)) ||
      std::strcmp(staged, text) != 0) {
    LittleFS.remove(kWifiConfigTempPath);
    return false;
  }
  if (!LittleFS.rename(kWifiConfigTempPath, kWifiConfigPath)) {
    // Filesystems that refuse to rename over an existing name fall back to
    // deleting the old file, but only now that the new one is complete.
    if (!LittleFS.exists(kWifiConfigPath) ||
        !LittleFS.remove(kWifiConfigPath) ||
        !LittleFS.rename(kWifiConfigTempPath, kWifiConfigPath)) {
      LittleFS.remove(kWifiConfigTempPath);
      return false;
    }
  }
  littlefsReady_ = true;
  return true;
}

bool WifiManager::restoreConfigFile(const char* previous, bool hadFile) {
  if (!ensureFileSystem(false)) {
    return false;
  }
  if (!hadFile || previous == nullptr || previous[0] == '\0') {
    return !LittleFS.exists(kWifiConfigPath) || LittleFS.remove(kWifiConfigPath);
  }
  return writeConfigFile(previous);
}

bool WifiManager::readNvsConfig(char* ssid, size_t ssidSize, char* password,
                                size_t passwordSize) const {
  if (ssid == nullptr || password == nullptr) {
    return false;
  }
  ssid[0] = '\0';
  password[0] = '\0';
  Preferences preferences;
  if (!preferences.begin(kPreferencesNamespace, true)) {
    return false;
  }
  const bool present = preferences.isKey(kSsidKey);
  const String storedSsid = present ? preferences.getString(kSsidKey, "") : String();
  const String storedPassword =
      present ? preferences.getString(kPasswordKey, "") : String();
  preferences.end();
  if (!present) {
    return false;
  }
  // Validate the stored length before copying: a truncated SSID would point at
  // a different network instead of being rejected like v0.4.0 did.
  if (storedSsid.length() > kWifiSsidMaxLength ||
      storedPassword.length() > kWifiPasswordMaxLength ||
      storedSsid.length() + 1 > ssidSize ||
      storedPassword.length() + 1 > passwordSize) {
    Serial.println("wifi: nvs credentials are too long, ignoring them");
    return false;
  }
  copyString(ssid, ssidSize, storedSsid.c_str());
  copyString(password, passwordSize, storedPassword.c_str());
  return true;
}

bool WifiManager::writeNvsConfig(const WifiCredentials& credentials) {
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
  return ok;
}

bool WifiManager::saveCredentials(const WifiCredentials& credentials) {
  if (!credentials.valid) {
    return false;
  }
  char json[kWifiConfigMaxBytes];
  const size_t needed = formatWifiConfigJson(credentials, json, sizeof(json));
  if (needed == 0 || needed >= sizeof(json)) {
    return false;
  }

  const bool fsReady = littlefsReady_ || ensureFileSystem(true);
  if (!fsReady) {
    // Without LittleFS neither the old file nor the new one can be checked.
    // Writing only to NVS would be undone by a /wifi.json that a later mount
    // finds, so this save fails instead of reporting success.
    Serial.println("wifi: littlefs unavailable, cannot save the network");
    return false;
  }

  char previous[kWifiConfigMaxBytes] = {0};
  const bool hadFile = readFileInto(kWifiConfigPath, previous, sizeof(previous));

  // /wifi.json is the copy the next boot prefers, so the save only succeeds
  // when the new pair is really in it. Reporting success with an older file
  // left behind would reconnect to the old network after a reset.
  if (!writeConfigFile(json)) {
    Serial.println("wifi: could not write /wifi.json, save failed");
    return false;
  }
  if (!writeNvsConfig(credentials)) {
    // Keep the two stores on the pair the user had before this attempt.
    const bool restored = restoreConfigFile(previous, hadFile);
    bool removed = false;
    if (!restored) {
      removed = !LittleFS.exists(kWifiConfigPath) || LittleFS.remove(kWifiConfigPath);
    }
    if (restored) {
      Serial.println("wifi: nvs write failed, previous settings restored");
    } else if (removed) {
      Serial.println("wifi: nvs write failed, old file removed (nvs keeps the old settings)");
    } else {
      Serial.println("wifi: nvs write failed, /wifi.json could not be restored or removed");
    }
    return false;
  }
  configSource_ = WifiConfigSource::kLittleFs;

  credentials_ = credentials;
  startStation();
  return true;
}

bool WifiManager::clearCredentials() {
  bool nvsOk = true;
  Preferences preferences;
  if (preferences.begin(kPreferencesNamespace, false)) {
    // A key that is already gone counts as cleared; Preferences::remove()
    // reports false for a missing key.
    if (preferences.isKey(kSsidKey)) {
      nvsOk = preferences.remove(kSsidKey) && nvsOk;
    }
    if (preferences.isKey(kPasswordKey)) {
      nvsOk = preferences.remove(kPasswordKey) && nvsOk;
    }
    preferences.end();
  }

  // A file that cannot be deleted would bring the old network back on the next
  // boot; replace it with content that cannot be used instead. The staged
  // temporary file matters too: readConfigFile() adopts it when /wifi.json is
  // gone, so it has to be neutralised first. If that is impossible, /wifi.json
  // is left in place so the two stores keep telling the same story.
  const char* kUnusableConfig = "{\"version\":1}\n";
  bool fileOk = true;
  if (ensureFileSystem(false)) {
    if (LittleFS.exists(kWifiConfigTempPath) &&
        !LittleFS.remove(kWifiConfigTempPath) &&
        !writeRawFile(kWifiConfigTempPath, kUnusableConfig)) {
      Serial.println("wifi: could not delete or blank /wifi.json.tmp");
      fileOk = false;
    }
    if (fileOk && LittleFS.exists(kWifiConfigPath) &&
        !LittleFS.remove(kWifiConfigPath)) {
      fileOk = writeConfigFile(kUnusableConfig);
      if (!fileOk) {
        Serial.println("wifi: could not delete or blank /wifi.json");
      }
    }
  }

  if (!nvsOk || !fileOk) {
    // Nothing is reported as cleared unless both stores really are.
    Serial.println("wifi: stored credentials could not be cleared completely");
    return false;
  }
  configSource_ = WifiConfigSource::kNone;
  credentials_ = WifiCredentials{};
  startAccessPoint();
  return true;
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
  Serial.printf("wifi: connecting to '%s' (saved in %s)\n", credentials_.ssid,
                wifiConfigSourceName(configSource_));
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
  // never has to span a 32 bit wrap. rebase() carries the sub-second part over.
  clock_.rebase(nowMs);
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

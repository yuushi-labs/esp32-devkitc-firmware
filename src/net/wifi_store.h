// Stored Wi-Fi settings: the /wifi.json file on LittleFS and the migration
// rule that keeps the older NVS (Preferences "wifi") copy usable.
//
// Everything here is pure C++: the JSON writer, the parser and the decision
// which stored copy wins are unit tested on the host. WifiManager only does
// the file and NVS I/O around them, so a bad or missing file can never leave
// the station without a network.
#pragma once

#include <cstddef>
#include <cstdint>

#include "net/wifi_credentials.h"

namespace fw {

// LittleFS keeps /log.csv and the log backup files; the credentials live in
// their own file so reading one never disturbs the other.
constexpr const char* kWifiConfigPath = "/wifi.json";
// Written first and renamed over the real file, so a power cut during a save
// leaves the previous /wifi.json in place.
constexpr const char* kWifiConfigTempPath = "/wifi.json.tmp";
constexpr const char* kWifiConfigVersionKey = "version";
constexpr uint32_t kWifiConfigVersion = 1;
// Worst case escape of a 32 byte SSID and a 63 byte password plus the keys.
constexpr size_t kWifiConfigMaxBytes = 768;

enum class WifiConfigSource {
  kNone = 0,
  kLittleFs,
  kNvs,
};

// "littlefs" / "nvs" / "none".
const char* wifiConfigSourceName(WifiConfigSource source);

// One line JSON object: {"version":1,"ssid":"...","password":"..."} and a
// trailing newline. Returns the number of bytes the whole text needs, in
// snprintf style, so a too small buffer is reported instead of silently
// cutting the password. 0 when the pair is invalid.
size_t formatWifiConfigJson(const WifiCredentials& credentials, char* out,
                            size_t outSize);

// Parses the object. Keys may come in any order and unknown keys are skipped;
// `version` must be 1 when it is present. Strings are unescaped, and a value
// that does not fit or decodes to an embedded NUL is rejected instead of
// being shortened. `error` (optional) explains a rejected pair.
bool parseWifiConfigJson(const char* text, WifiCredentials* out,
                         WifiValidation* error = nullptr);

// Result of looking at both stores at boot.
struct WifiConfigChoice {
  WifiCredentials credentials;
  WifiConfigSource source = WifiConfigSource::kNone;
  // The LittleFS copy is missing or unreadable and the NVS copy was used:
  // write it to /wifi.json so the next boot reads the file.
  bool migrate = false;
  // A file existed but did not parse (or held invalid values). Worth a log
  // line, and the migration above already replaces it.
  bool fileRejected = false;
};

// LittleFS wins when it holds a usable pair; otherwise a valid NVS pair is
// used and flagged for migration. `fileText` is nullptr or "" when there is no
// file. `nvsPresent` is false when the NVS namespace has no SSID key.
WifiConfigChoice chooseWifiConfig(const char* fileText, bool nvsPresent,
                                  const char* nvsSsid, const char* nvsPassword);

}  // namespace fw

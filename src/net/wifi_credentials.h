// Wi-Fi configuration: validation and the captive portal form parser.
//
// Everything here is pure C++ so the AP setup flow (form decoding, SSID and
// password rules, status text) is unit tested on the host. The Arduino side
// only stores the accepted values in NVS and talks to the radio.
#pragma once

#include <cstddef>
#include <cstdint>

namespace fw {

constexpr size_t kWifiSsidMaxLength = 32;
constexpr size_t kWifiPasswordMaxLength = 63;
constexpr size_t kWifiPasswordMinLength = 8;

enum class WifiValidation {
  kOk,
  kEmptySsid,
  kSsidTooLong,
  kPasswordTooShort,
  kPasswordTooLong,
  // The decoded value contained a NUL, so it cannot be a C string.
  kEmbeddedNul,
};

const char* wifiValidationMessage(WifiValidation validation);

struct WifiCredentials {
  char ssid[kWifiSsidMaxLength + 1] = {0};
  char password[kWifiPasswordMaxLength + 1] = {0};
  bool valid = false;
};

// Validates the pair. An empty password means an open network and is allowed;
// a non-empty password must be 8..63 characters (WPA2 rule).
WifiValidation validateWifiCredentials(const char* ssid, const char* password);

// Copies the strings when they are valid; leaves `out` invalid otherwise.
WifiValidation makeWifiCredentials(const char* ssid, const char* password,
                                   WifiCredentials* out);

// Decodes an `application/x-www-form-urlencoded` body such as
// "ssid=My+Network&password=p%40ss". Returns false when a required field is
// missing; `error` then explains which rule failed.
bool parseWifiSetupForm(const char* body, WifiCredentials* out,
                        WifiValidation* error);

// Percent-decodes `length` bytes into `out` (NUL terminated). Returns the
// number of characters written, excluding the terminator. `truncated` (when
// given) is set when the decoded value did not fit, so the caller can reject
// the input instead of saving a silently shortened value.
size_t urlDecode(const char* input, size_t length, char* out, size_t outSize,
                 bool* truncated = nullptr);

enum class WifiMode { kOff, kStation, kAccessPoint };

struct WifiStatus {
  WifiMode mode = WifiMode::kOff;
  bool connected = false;
  bool ntpSynced = false;
  char ssid[kWifiSsidMaxLength + 1] = {0};
  char ip[16] = "0.0.0.0";
  int8_t rssi = 0;
  int64_t epochSeconds = 0;
  // Local offset used to render `epochSeconds` in the status report.
  int32_t utcOffsetSeconds = 0;
  uint32_t uptimeMs = 0;
};

const char* wifiModeName(WifiMode mode);

// Multi line `wifi status` report. NUL terminated, snprintf semantics.
size_t formatWifiStatus(const WifiStatus& status, char* out, size_t outSize);

// "http://192.168.4.1/" for the OLED QR code and the dashboard link.
size_t formatStationUrl(const char* ip, char* out, size_t outSize);

}  // namespace fw

#include "net/wifi_credentials.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "net/ntp_clock.h"

namespace fw {
namespace {

int formatInto(char* out, size_t outSize, const char* format, ...) {
  char scratch = '\0';
  va_list args;
  va_start(args, format);
  const int needed =
      std::vsnprintf(outSize > 0 ? out : &scratch, outSize, format, args);
  va_end(args);
  return needed;
}

bool isHexDigit(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

uint8_t hexValue(char c) {
  if (c >= '0' && c <= '9') {
    return static_cast<uint8_t>(c - '0');
  }
  if (c >= 'a' && c <= 'f') {
    return static_cast<uint8_t>(c - 'a' + 10);
  }
  return static_cast<uint8_t>(c - 'A' + 10);
}

bool findField(const char* body, const char* name, const char** valueStart,
               size_t* valueLength) {
  const size_t nameLength = std::strlen(name);
  const char* cursor = body;
  while (cursor != nullptr && *cursor != '\0') {
    const char* end = std::strchr(cursor, '&');
    const size_t pairLength =
        end != nullptr ? static_cast<size_t>(end - cursor) : std::strlen(cursor);
    const char* equals =
        static_cast<const char*>(std::memchr(cursor, '=', pairLength));
    if (equals != nullptr && static_cast<size_t>(equals - cursor) == nameLength &&
        std::strncmp(cursor, name, nameLength) == 0) {
      *valueStart = equals + 1;
      *valueLength = static_cast<size_t>(cursor + pairLength - (equals + 1));
      return true;
    }
    cursor = end != nullptr ? end + 1 : nullptr;
  }
  return false;
}

}  // namespace

const char* wifiValidationMessage(WifiValidation validation) {
  switch (validation) {
    case WifiValidation::kOk:
      return "ok";
    case WifiValidation::kEmptySsid:
      return "SSID must not be empty";
    case WifiValidation::kSsidTooLong:
      return "SSID must be 32 characters or fewer";
    case WifiValidation::kPasswordTooShort:
      return "password must be at least 8 characters (or empty for an open network)";
    case WifiValidation::kPasswordTooLong:
      return "password must be 63 characters or fewer";
  }
  return "invalid Wi-Fi settings";
}

WifiValidation validateWifiCredentials(const char* ssid, const char* password) {
  if (ssid == nullptr || ssid[0] == '\0') {
    return WifiValidation::kEmptySsid;
  }
  if (std::strlen(ssid) > kWifiSsidMaxLength) {
    return WifiValidation::kSsidTooLong;
  }
  const size_t passwordLength = password != nullptr ? std::strlen(password) : 0;
  if (passwordLength == 0) {
    return WifiValidation::kOk;
  }
  if (passwordLength < kWifiPasswordMinLength) {
    return WifiValidation::kPasswordTooShort;
  }
  if (passwordLength > kWifiPasswordMaxLength) {
    return WifiValidation::kPasswordTooLong;
  }
  return WifiValidation::kOk;
}

WifiValidation makeWifiCredentials(const char* ssid, const char* password,
                                   WifiCredentials* out) {
  if (out == nullptr) {
    return WifiValidation::kEmptySsid;
  }
  *out = WifiCredentials{};
  const WifiValidation validation = validateWifiCredentials(ssid, password);
  if (validation != WifiValidation::kOk) {
    return validation;
  }
  std::snprintf(out->ssid, sizeof(out->ssid), "%s", ssid);
  std::snprintf(out->password, sizeof(out->password), "%s",
                password != nullptr ? password : "");
  out->valid = true;
  return WifiValidation::kOk;
}

size_t urlDecode(const char* input, size_t length, char* out, size_t outSize,
                 bool* truncated) {
  if (truncated != nullptr) {
    *truncated = false;
  }
  if (input == nullptr || out == nullptr || outSize == 0) {
    return 0;
  }
  size_t written = 0;
  for (size_t index = 0; index < length; ++index) {
    char c = input[index];
    if (c == '+') {
      c = ' ';
    } else if (c == '%' && index + 2 < length && isHexDigit(input[index + 1]) &&
               isHexDigit(input[index + 2])) {
      c = static_cast<char>(hexValue(input[index + 1]) * 16 +
                            hexValue(input[index + 2]));
      index += 2;
    }
    if (written + 1 < outSize) {
      out[written++] = c;
    } else if (truncated != nullptr) {
      *truncated = true;
    }
  }
  out[written] = '\0';
  return written;
}

bool parseWifiSetupForm(const char* body, WifiCredentials* out,
                        WifiValidation* error) {
  if (error != nullptr) {
    *error = WifiValidation::kEmptySsid;
  }
  if (body == nullptr || out == nullptr) {
    return false;
  }

  const char* value = nullptr;
  size_t valueLength = 0;
  if (!findField(body, "ssid", &value, &valueLength)) {
    return false;
  }
  char ssid[kWifiSsidMaxLength + 1] = {0};
  bool ssidTruncated = false;
  urlDecode(value, valueLength, ssid, sizeof(ssid), &ssidTruncated);
  if (ssidTruncated) {
    // A shortened SSID would silently save a different network.
    if (error != nullptr) {
      *error = WifiValidation::kSsidTooLong;
    }
    return false;
  }

  char password[kWifiPasswordMaxLength + 1] = {0};
  if (findField(body, "password", &value, &valueLength)) {
    bool passwordTruncated = false;
    urlDecode(value, valueLength, password, sizeof(password), &passwordTruncated);
    if (passwordTruncated) {
      if (error != nullptr) {
        *error = WifiValidation::kPasswordTooLong;
      }
      return false;
    }
  }

  const WifiValidation validation = makeWifiCredentials(ssid, password, out);
  if (error != nullptr) {
    *error = validation;
  }
  return validation == WifiValidation::kOk;
}

const char* wifiModeName(WifiMode mode) {
  switch (mode) {
    case WifiMode::kStation:
      return "station";
    case WifiMode::kAccessPoint:
      return "access-point";
    case WifiMode::kOff:
      break;
  }
  return "off";
}

size_t formatWifiStatus(const WifiStatus& status, char* out, size_t outSize) {
  char timestamp[24] = "not synced";
  if (status.ntpSynced && status.epochSeconds > 0) {
    formatTimestamp(status.epochSeconds, status.utcOffsetSeconds, timestamp,
                    sizeof(timestamp));
  }
  const int needed = formatInto(
      out, outSize,
      "wifi: %s%s\n"
      "  ssid: %s\n"
      "  ip: %s\n"
      "  rssi: %d dBm\n"
      "  ntp: %s\n",
      wifiModeName(status.mode), status.connected ? " (connected)" : " (not connected)",
      status.ssid[0] != '\0' ? status.ssid : "-", status.ip,
      static_cast<int>(status.rssi), timestamp);
  return needed > 0 ? static_cast<size_t>(needed) : 0;
}

size_t formatStationUrl(const char* ip, char* out, size_t outSize) {
  const int needed = formatInto(out, outSize, "http://%s/",
                                ip != nullptr && ip[0] != '\0' ? ip : "0.0.0.0");
  return needed > 0 ? static_cast<size_t>(needed) : 0;
}

}  // namespace fw

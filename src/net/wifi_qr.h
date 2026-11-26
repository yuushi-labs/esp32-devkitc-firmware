// Wi-Fi QR payload for the setup access point.
//
// Phones understand "WIFI:T:WPA;S:<ssid>;P:<password>;;" (the format ZXing and
// every camera app implement), so the OLED code hands the network over without
// typing the key. The five characters that separate the fields are escaped with
// a backslash, as the format specifies.
#pragma once

#include <cstddef>

namespace fw {

// Escapes `\`, `;`, `,` `:` and `"` for a Wi-Fi QR field. Always NUL terminates
// and returns the untruncated length (snprintf semantics).
size_t escapeWifiQrField(const char* value, char* out, size_t outSize);

// "WIFI:T:WPA;S:<ssid>;P:<password>;;", or "WIFI:T:nopass;S:<ssid>;;" for an
// open network. Returns the untruncated length.
size_t formatWifiQrPayload(const char* ssid, const char* password, char* out,
                           size_t outSize);

// True when `payload` starts with the Wi-Fi scheme, so the caller can tell a
// join code from an `http://` dashboard link.
bool isWifiQrPayload(const char* payload);

}  // namespace fw

#include "net/wifi_qr.h"

#include <cstdio>
#include <cstring>

namespace fw {
namespace {

// Appends `text` to `out` and tracks both the written and the needed length, so
// a small buffer still reports what the full payload takes.
struct Builder {
  // The ESP32 toolchain builds with gnu++11, where a class with default member
  // initialisers is not an aggregate, so the two fields come in a constructor.
  Builder(char* buffer, size_t bufferSize) : out(buffer), size(bufferSize) {}

  char* out;
  size_t size;
  size_t used = 0;
  size_t needed = 0;

  void append(const char* text) {
    for (const char* p = text; p != nullptr && *p != '\0'; ++p) {
      appendChar(*p);
    }
  }

  void appendChar(char c) {
    ++needed;
    if (out != nullptr && used + 1 < size) {
      out[used++] = c;
      out[used] = '\0';
    }
  }

  void appendEscaped(const char* value) {
    if (value == nullptr) {
      return;
    }
    for (const char* p = value; *p != '\0'; ++p) {
      switch (*p) {
        case '\\':
        case ';':
        case ',':
        case ':':
        case '"':
          appendChar('\\');
          appendChar(*p);
          break;
        default:
          appendChar(*p);
          break;
      }
    }
  }

  void finish() {
    if (out != nullptr && size > 0) {
      out[used] = '\0';
    }
  }
};

}  // namespace

size_t escapeWifiQrField(const char* value, char* out, size_t outSize) {
  Builder builder(out, outSize);
  builder.appendEscaped(value);
  builder.finish();
  return builder.needed;
}

size_t formatWifiQrPayload(const char* ssid, const char* password, char* out,
                           size_t outSize) {
  Builder builder(out, outSize);
  const bool hasPassword = password != nullptr && password[0] != '\0';
  builder.append(hasPassword ? "WIFI:T:WPA;S:" : "WIFI:T:nopass;S:");
  builder.appendEscaped(ssid);
  if (hasPassword) {
    builder.append(";P:");
    builder.appendEscaped(password);
  }
  builder.append(";;");
  builder.finish();
  return builder.needed;
}

bool isWifiQrPayload(const char* payload) {
  return payload != nullptr && std::strncmp(payload, "WIFI:", 5) == 0;
}

}  // namespace fw

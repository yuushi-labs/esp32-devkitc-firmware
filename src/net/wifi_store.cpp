#include "net/wifi_store.h"

#include <cstdio>
#include <cstring>

namespace fw {
namespace {

// Accumulates a printf style result: `needed` is the full length, `used` what
// fits into the caller's buffer (always keeping room for the NUL).
struct Sink {
  Sink(char* buffer, size_t bufferSize) : out(buffer), size(bufferSize) {
    if (out != nullptr && size > 0) {
      out[0] = '\0';
    }
  }

  char* out;
  size_t size;
  size_t used = 0;
  size_t needed = 0;

  void putChar(char c) {
    ++needed;
    if (out != nullptr && used + 1 < size) {
      out[used++] = c;
      out[used] = '\0';
    }
  }

  void putText(const char* text) {
    while (text != nullptr && *text != '\0') {
      putChar(*text++);
    }
  }

  void putEscaped(const char* text) {
    putChar('"');
    for (const char* cursor = text; cursor != nullptr && *cursor != '\0'; ++cursor) {
      const unsigned char c = static_cast<unsigned char>(*cursor);
      switch (c) {
        case '"':
          putChar('\\');
          putChar('"');
          break;
        case '\\':
          putChar('\\');
          putChar('\\');
          break;
        case '\b':
          putText("\\b");
          break;
        case '\f':
          putText("\\f");
          break;
        case '\n':
          putText("\\n");
          break;
        case '\r':
          putText("\\r");
          break;
        case '\t':
          putText("\\t");
          break;
        default:
          if (c < 0x20 || c == 0x7F) {
            char escape[8];
            std::snprintf(escape, sizeof(escape), "\\u%04X", static_cast<unsigned>(c));
            putText(escape);
          } else {
            // UTF-8 bytes of a network name pass through unchanged: the file
            // stays readable and the parser hands them back untouched.
            putChar(static_cast<char>(c));
          }
          break;
      }
    }
    putChar('"');
  }
};

bool isSpace(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

int hexDigit(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

class Parser {
 public:
  explicit Parser(const char* text) : cursor_(text) {}

  void skipSpace() {
    while (*cursor_ != '\0' && isSpace(*cursor_)) {
      ++cursor_;
    }
  }

  char peek() const { return *cursor_; }

  bool consume(char expected) {
    skipSpace();
    if (*cursor_ != expected) {
      return false;
    }
    ++cursor_;
    return true;
  }

  bool done() {
    skipSpace();
    return *cursor_ == '\0';
  }

  // Parses a quoted JSON string into `out` (NUL terminated). Unknown escapes,
  // embedded NULs and values that do not fit are rejected. `overflowError`
  // names the rule a too long value breaks (SSID or password).
  bool parseString(char* out, size_t outSize, WifiValidation overflowError,
                   WifiValidation* error) {
    skipSpace();
    if (*cursor_ != '"') {
      return false;
    }
    ++cursor_;
    size_t written = 0;
    while (*cursor_ != '\0') {
      const unsigned char c = static_cast<unsigned char>(*cursor_);
      if (c == '"') {
        ++cursor_;
        if (out != nullptr && outSize > 0) {
          out[written < outSize ? written : outSize - 1] = '\0';
        }
        return true;
      }
      char decoded = '\0';
      if (c == '\\') {
        ++cursor_;
        const char escape = *cursor_;
        if (escape == '\0') {
          return false;
        }
        ++cursor_;
        switch (escape) {
          case '"':
            decoded = '"';
            break;
          case '\\':
            decoded = '\\';
            break;
          case '/':
            decoded = '/';
            break;
          case 'b':
            decoded = '\b';
            break;
          case 'f':
            decoded = '\f';
            break;
          case 'n':
            decoded = '\n';
            break;
          case 'r':
            decoded = '\r';
            break;
          case 't':
            decoded = '\t';
            break;
          case 'u': {
            int value = 0;
            for (int index = 0; index < 4; ++index) {
              const int digit = hexDigit(*cursor_);
              if (digit < 0) {
                return false;
              }
              value = value * 16 + digit;
              ++cursor_;
            }
            if (value == 0) {
              if (error != nullptr) {
                *error = WifiValidation::kEmbeddedNul;
              }
              return false;
            }
            if (value > 0xFF) {
              // The writer only emits \u00XX for control characters; anything
              // wider would need a code point conversion this parser does not
              // do, so reject it rather than store a mangled name.
              return false;
            }
            decoded = static_cast<char>(value);
            break;
          }
          default:
            return false;
        }
      } else if (c < 0x20) {
        return false;
      } else {
        decoded = static_cast<char>(c);
        ++cursor_;
      }
      if (decoded == '\0') {
        if (error != nullptr) {
          *error = WifiValidation::kEmbeddedNul;
        }
        return false;
      }
      if (out != nullptr && written + 1 < outSize) {
        out[written] = decoded;
      } else {
        // A shortened name or password must never be saved as if it were the
        // value the user typed.
        if (error != nullptr) {
          *error = overflowError;
        }
        return false;
      }
      ++written;
    }
    return false;
  }

  // Moves past a string without storing it (unknown keys).
  bool skipString() {
    skipSpace();
    if (*cursor_ != '"') {
      return false;
    }
    ++cursor_;
    while (*cursor_ != '\0') {
      if (*cursor_ == '\\') {
        ++cursor_;
        if (*cursor_ == '\0') {
          return false;
        }
      } else if (*cursor_ == '"') {
        ++cursor_;
        return true;
      }
      ++cursor_;
    }
    return false;
  }

  // Parses a non-negative integer. Used only for "version".
  bool parseUnsigned(uint32_t* value) {
    skipSpace();
    if (*cursor_ < '0' || *cursor_ > '9') {
      return false;
    }
    uint32_t result = 0;
    while (*cursor_ >= '0' && *cursor_ <= '9') {
      result = result * 10 + static_cast<uint32_t>(*cursor_ - '0');
      ++cursor_;
    }
    if (value != nullptr) {
      *value = result;
    }
    return true;
  }

  // Skips a value whose content this version does not use, keeping balanced
  // objects and arrays intact so a future key cannot break the parser.
  bool skipValue() {
    skipSpace();
    if (*cursor_ == '"') {
      return skipString();
    }
    if (*cursor_ == '{' || *cursor_ == '[') {
      int depth = 0;
      while (*cursor_ != '\0') {
        if (*cursor_ == '"') {
          if (!skipString()) {
            return false;
          }
          continue;
        }
        if (*cursor_ == '{' || *cursor_ == '[') {
          ++depth;
        } else if (*cursor_ == '}' || *cursor_ == ']') {
          --depth;
          if (depth == 0) {
            ++cursor_;
            return true;
          }
        }
        ++cursor_;
      }
      return false;
    }
    // Number, true, false or null.
    while (*cursor_ != '\0' && !isSpace(*cursor_) && *cursor_ != ',' &&
           *cursor_ != '}') {
      ++cursor_;
    }
    return true;
  }

 private:
  const char* cursor_;
};

}  // namespace

const char* wifiConfigSourceName(WifiConfigSource source) {
  switch (source) {
    case WifiConfigSource::kLittleFs:
      return "littlefs";
    case WifiConfigSource::kNvs:
      return "nvs";
    case WifiConfigSource::kNone:
      break;
  }
  return "none";
}

size_t formatWifiConfigJson(const WifiCredentials& credentials, char* out,
                            size_t outSize) {
  Sink sink(out, outSize);
  if (!credentials.valid) {
    return 0;
  }
  sink.putText("{\"version\":");
  char version[8];
  std::snprintf(version, sizeof(version), "%lu",
                static_cast<unsigned long>(kWifiConfigVersion));
  sink.putText(version);
  sink.putText(",\"ssid\":");
  sink.putEscaped(credentials.ssid);
  sink.putText(",\"password\":");
  sink.putEscaped(credentials.password);
  sink.putText("}\n");
  return sink.needed;
}

bool parseWifiConfigJson(const char* text, WifiCredentials* out,
                         WifiValidation* error) {
  if (error != nullptr) {
    *error = WifiValidation::kEmptySsid;
  }
  if (text == nullptr || out == nullptr) {
    return false;
  }
  *out = WifiCredentials{};

  Parser parser(text);
  if (!parser.consume('{')) {
    return false;
  }

  char ssid[kWifiSsidMaxLength + 1] = {0};
  char password[kWifiPasswordMaxLength + 1] = {0};
  bool ssidFound = false;

  parser.skipSpace();
  if (parser.peek() == '}') {
    return false;
  }
  while (true) {
    char key[16] = {0};
    if (!parser.parseString(key, sizeof(key), WifiValidation::kSsidTooLong, error)) {
      return false;
    }
    if (!parser.consume(':')) {
      return false;
    }
    if (std::strcmp(key, "ssid") == 0) {
      if (!parser.parseString(ssid, sizeof(ssid), WifiValidation::kSsidTooLong,
                              error)) {
        return false;
      }
      ssidFound = true;
    } else if (std::strcmp(key, "password") == 0) {
      if (!parser.parseString(password, sizeof(password),
                              WifiValidation::kPasswordTooLong, error)) {
        return false;
      }
    } else if (std::strcmp(key, "version") == 0) {
      uint32_t version = 0;
      if (!parser.parseUnsigned(&version) || version != kWifiConfigVersion) {
        return false;
      }
    } else if (!parser.skipValue()) {
      return false;
    }

    parser.skipSpace();
    if (parser.peek() == ',') {
      parser.consume(',');
      continue;
    }
    if (parser.consume('}')) {
      break;
    }
    return false;
  }
  if (!parser.done()) {
    return false;
  }
  if (!ssidFound) {
    return false;
  }

  const WifiValidation validation = makeWifiCredentials(ssid, password, out);
  if (error != nullptr) {
    *error = validation;
  }
  return validation == WifiValidation::kOk;
}

WifiConfigChoice chooseWifiConfig(const char* fileText, bool nvsPresent,
                                  const char* nvsSsid, const char* nvsPassword) {
  WifiConfigChoice choice;

  if (fileText != nullptr && fileText[0] != '\0') {
    WifiValidation error = WifiValidation::kOk;
    if (parseWifiConfigJson(fileText, &choice.credentials, &error) &&
        choice.credentials.valid) {
      choice.source = WifiConfigSource::kLittleFs;
      return choice;
    }
    choice.fileRejected = true;
    choice.credentials = WifiCredentials{};
  }

  if (nvsPresent) {
    WifiCredentials nvs;
    if (makeWifiCredentials(nvsSsid, nvsPassword, &nvs) == WifiValidation::kOk) {
      choice.credentials = nvs;
      choice.source = WifiConfigSource::kNvs;
      // No usable file: write the NVS pair to LittleFS so the file becomes the
      // single source from then on. A corrupt file is replaced the same way.
      choice.migrate = true;
      return choice;
    }
  }

  // Nothing usable.
  choice.source = WifiConfigSource::kNone;
  choice.migrate = false;
  return choice;
}

}  // namespace fw

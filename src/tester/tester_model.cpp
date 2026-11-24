#include "tester/tester_model.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "app_config.h"
#include "sensors/dht.h"

namespace fw {
namespace {

constexpr const char* kDegreeC = "\xC2\xB0" "C";

struct Appender {
  Appender(char* outBuffer, size_t bufferSize) : out(outBuffer), size(bufferSize) {}

  char* out;
  size_t size;
  size_t used = 0;
  size_t needed = 0;

  void append(const char* format, ...) {
    char scratch = '\0';
    char* destination = (out != nullptr && used < size) ? out + used : &scratch;
    const size_t remaining = (out != nullptr && used < size) ? size - used : 0;
    va_list args;
    va_start(args, format);
    const int written = std::vsnprintf(destination, remaining, format, args);
    va_end(args);
    if (written <= 0) {
      return;
    }
    needed += static_cast<size_t>(written);
    if (remaining > 0) {
      const size_t available = remaining - 1;
      used += static_cast<size_t>(written) < available ? static_cast<size_t>(written)
                                                       : available;
    }
  }
};

const char* levelText(bool down) { return down ? "ON " : "-- "; }

const char* shortBmpStatus(Bmp280Status status) {
  switch (status) {
    case Bmp280Status::kNotPresent:
      return "missing";
    case Bmp280Status::kBadChipId:
      return "bad id";
    case Bmp280Status::kBusError:
      return "bus error";
    case Bmp280Status::kOk:
      break;
  }
  return "ok";
}

void addf(PageLines& page, uint8_t scale, const char* format, ...) {
  char text[PageLines::kLineLength];
  va_list args;
  va_start(args, format);
  std::vsnprintf(text, sizeof(text), format, args);
  va_end(args);
  page.add(text, scale);
}

}  // namespace

const char* testerPageName(TesterPage page) {
  switch (page) {
    case TesterPage::kInputs:
      return "inputs";
    case TesterPage::kScan:
      return "i2c";
    case TesterPage::kSensors:
      return "sensors";
  }
  return "inputs";
}

TesterPage nextTesterPage(TesterPage page) {
  const size_t next = (static_cast<size_t>(page) + 1) % kTesterPageCount;
  return static_cast<TesterPage>(next);
}

const char* testerButtonName(size_t index) {
  switch (index) {
    case 0:
      return "PSH";
    case 1:
      return "BAK";
    case 2:
      return "CON";
    default:
      break;
  }
  return "?";
}

void buildTesterPage(TesterPage page, const TesterSensors& sensors,
                     const TesterInputs& inputs, PageLines* out) {
  if (out == nullptr) {
    return;
  }
  out->clear();

  switch (page) {
    case TesterPage::kInputs: {
      addf(*out, 1, "TESTER v%s", FW_VERSION);
      addf(*out, 1, "ENC %+ld", static_cast<long>(inputs.encoderPosition));
      addf(*out, 1, "PSH %s%lu", levelText(inputs.pushDown),
           static_cast<unsigned long>(inputs.pushPresses));
      addf(*out, 1, "BAK %s%lu", levelText(inputs.backDown),
           static_cast<unsigned long>(inputs.backPresses));
      addf(*out, 1, "CON %s%lu", levelText(inputs.confirmDown),
           static_cast<unsigned long>(inputs.confirmPresses));
      if (sensors.bmp280ReadingValid) {
        addf(*out, 1, "BMP %.1f %s", static_cast<double>(sensors.temperatureC), kDegreeC);
      } else {
        out->add(sensors.bmp280Status == Bmp280Status::kOk ? "BMP no reading"
                                                           : "BMP not found");
      }
      if (sensors.dhtReadingValid) {
        addf(*out, 1, "%s %.1f %%", dhtTypeName(sensors.dhtType),
             static_cast<double>(sensors.humidityPct));
      } else {
        out->add("DHT no answer");
      }
      addf(*out, 1, "I2C %lu dev", static_cast<unsigned long>(sensors.scan.count));
      break;
    }

    case TesterPage::kScan: {
      addf(*out, 1, "I2C SCAN %lu", static_cast<unsigned long>(sensors.scan.count));
      if (sensors.scan.empty()) {
        out->add("no device answered");
        out->add("check SDA21/SCL22");
        out->add("and 3.3V power");
      } else {
        const size_t maxRows = PageLines::kMaxLines - 1;
        for (size_t index = 0; index < sensors.scan.count && index < maxRows; ++index) {
          const I2cScanEntry& entry = sensors.scan.entries[index];
          addf(*out, 1, "0x%02X %s", static_cast<unsigned>(entry.address),
               i2cDeviceKindName(entry.kind));
        }
      }
      break;
    }

    case TesterPage::kSensors: {
      if (sensors.bmp280Status == Bmp280Status::kOk) {
        addf(*out, 1, "%s 0x%02X", sensors.bmp280Chip,
             static_cast<unsigned>(sensors.bmp280Address));
      } else {
        addf(*out, 1, "BMP280 %s", shortBmpStatus(sensors.bmp280Status));
      }
      if (sensors.bmp280ReadingValid) {
        addf(*out, 1, "T %.2f %s", static_cast<double>(sensors.temperatureC), kDegreeC);
        addf(*out, 1, "P %.2f hPa", static_cast<double>(sensors.pressureHpa));
      } else {
        out->add("T --.--");
        out->add("P ------");
      }
      if (sensors.dhtReadingValid) {
        addf(*out, 1, "%s ok", dhtTypeName(sensors.dhtType));
        addf(*out, 1, "RH %.1f %%", static_cast<double>(sensors.humidityPct));
        addf(*out, 1, "T %.1f %s", static_cast<double>(sensors.dhtTemperatureC), kDegreeC);
      } else {
        out->add(sensors.dhtPresent ? "DHT no answer" : "DHT not found");
      }
      break;
    }
  }
}

size_t formatTesterSensorReport(const TesterSensors& sensors, char* out, size_t outSize) {
  Appender text{out, outSize};
  text.append("sensors:\n");

  if (sensors.bmp280Status == Bmp280Status::kOk) {
    text.append("  bmp280: %s at 0x%02X\n", sensors.bmp280Chip,
                static_cast<unsigned>(sensors.bmp280Address));
    if (sensors.bmp280ReadingValid) {
      text.append("  bmp280: T=%.2f C P=%.2f hPa\n",
                  static_cast<double>(sensors.temperatureC),
                  static_cast<double>(sensors.pressureHpa));
    } else {
      text.append("  bmp280: no measurement yet\n");
    }
  } else {
    text.append("  bmp280: %s\n", bmp280StatusName(sensors.bmp280Status));
  }

  if (sensors.dhtReadingValid) {
    text.append("  dht: %s RH=%.1f %% T=%.1f C\n", dhtTypeName(sensors.dhtType),
                static_cast<double>(sensors.humidityPct),
                static_cast<double>(sensors.dhtTemperatureC));
  } else if (sensors.dhtPresent) {
    text.append("  dht: connected but no valid frame\n");
  } else {
    text.append("  dht: not found\n");
  }

  text.append("  oled: %s\n", sensors.oledPresent ? "found" : "not found");
  return text.needed;
}

size_t formatTesterInputReport(const TesterInputs& inputs, char* out, size_t outSize) {
  Appender text{out, outSize};
  text.append("inputs:\n");
  text.append("  encoder: %+ld\n", static_cast<long>(inputs.encoderPosition));
  text.append("  PSH: %s presses=%lu\n", levelText(inputs.pushDown),
              static_cast<unsigned long>(inputs.pushPresses));
  text.append("  BAK: %s presses=%lu\n", levelText(inputs.backDown),
              static_cast<unsigned long>(inputs.backPresses));
  text.append("  CON: %s presses=%lu\n", levelText(inputs.confirmDown),
              static_cast<unsigned long>(inputs.confirmPresses));
  return text.needed;
}

}  // namespace fw

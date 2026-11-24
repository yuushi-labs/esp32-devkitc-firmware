#include "station/station_model.h"

#include <cstdarg>
#include <cstdio>

#include "app_config.h"
#include "logging/log_record.h"

namespace fw {
namespace {

constexpr const char* kDegreeC = "\xC2\xB0" "C";

void addf(PageLines& page, uint8_t scale, const char* format, ...) {
  char text[PageLines::kLineLength];
  va_list args;
  va_start(args, format);
  std::vsnprintf(text, sizeof(text), format, args);
  va_end(args);
  page.add(text, scale);
}

const char* trendName(int8_t trend) {
  if (trend > 0) {
    return "rising";
  }
  if (trend < 0) {
    return "falling";
  }
  return "steady";
}

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

}  // namespace

const char* stationPageName(StationPage page) {
  switch (page) {
    case StationPage::kSummary:
      return "summary";
    case StationPage::kSensorDetail:
      return "sensors";
    case StationPage::kLogs:
      return "logs";
    case StationPage::kSystem:
      return "system";
  }
  return "summary";
}

StationPage nextStationPage(StationPage page) {
  const size_t next = (static_cast<size_t>(page) + 1) % kStationPageCount;
  return static_cast<StationPage>(next);
}

StationPage previousStationPage(StationPage page) {
  const size_t current = static_cast<size_t>(page);
  const size_t previous = (current + kStationPageCount - 1) % kStationPageCount;
  return static_cast<StationPage>(previous);
}

StationPage stationPageFromIndex(size_t index) {
  return static_cast<StationPage>(index % kStationPageCount);
}

void buildStationPage(StationPage page, const StationView& view, PageLines* out) {
  if (out == nullptr) {
    return;
  }
  out->clear();
  const SensorSnapshot& sensors = view.sensors;

  switch (page) {
    case StationPage::kSummary: {
      out->add("ENV STATION");
      if (sensors.hasTemperature()) {
        addf(*out, 2, "%.1f %s", static_cast<double>(sensors.displayTemperatureC()),
             kDegreeC);
      } else {
        addf(*out, 2, "--.- %s", kDegreeC);
      }
      if (sensors.hasPressure()) {
        addf(*out, 1, "%.1f hPa", static_cast<double>(sensors.pressureHpa));
      } else {
        out->add("---.- hPa");
      }
      if (sensors.hasHumidity()) {
        addf(*out, 1, "%.1f %% %s", static_cast<double>(sensors.humidityPct),
             dhtTypeName(sensors.dhtType));
      } else if (sensors.dhtPresent) {
        out->add("humidity: no answer");
      } else {
        out->add("humidity: none");
      }
      if (view.loggingEnabled) {
        addf(*out, 1, "REC %lu n%lus", static_cast<unsigned long>(view.logRecordCount),
             static_cast<unsigned long>((view.logIntervalRemainingMs + 999u) / 1000u));
      } else {
        addf(*out, 1, "PAUSED %lu rec", static_cast<unsigned long>(view.logRecordCount));
      }
      break;
    }

    case StationPage::kSensorDetail: {
      if (sensors.bmp280Present) {
        addf(*out, 1, "%s 0x%02X", sensors.bmp280Chip,
             static_cast<unsigned>(sensors.bmp280Address));
      } else {
        out->add("BMP280 not found");
      }
      if (sensors.temperatureValid) {
        addf(*out, 1, "T %.2f %s", static_cast<double>(sensors.temperatureC), kDegreeC);
      } else {
        out->add("T --.--");
      }
      if (sensors.pressureValid) {
        addf(*out, 1, "P %.2f hPa", static_cast<double>(sensors.pressureHpa));
      } else {
        out->add("P ------ hPa");
      }
      addf(*out, 1, "trend %s", trendName(sensors.pressureTrend));
      if (sensors.hasHumidity()) {
        addf(*out, 1, "%s %.1f %% %.1f %s", dhtTypeName(sensors.dhtType),
             static_cast<double>(sensors.humidityPct),
             static_cast<double>(sensors.dhtTemperatureC), kDegreeC);
      } else if (sensors.dhtPresent) {
        out->add("DHT no answer");
      } else {
        out->add("DHT not found");
      }
      addf(*out, 1, "poll %.1f s", kSensorPollIntervalMs / 1000.0);
      break;
    }

    case StationPage::kLogs: {
      out->add("LOG littlefs");
      out->add(view.loggingEnabled ? "state REC" : "state PAUSED");
      addf(*out, 1, "records %lu", static_cast<unsigned long>(view.logRecordCount));
      char interval[16];
      formatIntervalLabel(view.logIntervalMs, interval, sizeof(interval));
      addf(*out, 1, "every %s", interval);
      if (view.loggingEnabled) {
        addf(*out, 1, "next %lu s",
             static_cast<unsigned long>((view.logIntervalRemainingMs + 999u) / 1000u));
      } else {
        out->add("next -- s");
      }
      addf(*out, 1, "size %lu B", static_cast<unsigned long>(view.logFileBytes));
      out->add(view.logStorageReady ? "/log.csv" : "fs not ready");
      break;
    }

    case StationPage::kSystem: {
      addf(*out, 1, "SYSTEM v%s", view.version);
      char uptime[24];
      formatUptime(view.uptimeMs, uptime, sizeof(uptime));
      addf(*out, 1, "up %s", uptime);
      addf(*out, 1, "heap %lu B", static_cast<unsigned long>(view.freeHeapBytes));
      addf(*out, 1, "i2c %s", view.i2cAddresses);
      if (view.oledAddress != 0) {
        addf(*out, 1, "oled 0x%02X", static_cast<unsigned>(view.oledAddress));
      } else {
        out->add("oled not found");
      }
      addf(*out, 1, "sensor %s", sensors.bmp280Present ? sensors.bmp280Chip : "none");
      addf(*out, 1, "log %s", view.logStorageReady ? "ready" : "unavailable");
      break;
    }
  }
}

void PressureTrendTracker::update(float pressureHpa, uint32_t nowMs) {
  if (!hasReference_) {
    hasReference_ = true;
    referenceHpa_ = pressureHpa;
    referenceMs_ = nowMs;
    return;
  }
  if ((nowMs - referenceMs_) < kPressureTrendWindowMs) {
    return;
  }

  const float delta = pressureHpa - referenceHpa_;
  if (delta >= kPressureTrendThresholdHpa) {
    trend_ = 1;
  } else if (delta <= -kPressureTrendThresholdHpa) {
    trend_ = -1;
  } else {
    trend_ = 0;
  }
  referenceHpa_ = pressureHpa;
  referenceMs_ = nowMs;
}

void PressureTrendTracker::reset() {
  hasReference_ = false;
  referenceHpa_ = 0.0f;
  referenceMs_ = 0;
  trend_ = 0;
}

StationPage StationController::nextPage() {
  page_ = nextStationPage(page_);
  return page_;
}

StationPage StationController::previousPage() {
  page_ = previousStationPage(page_);
  return page_;
}

bool StationController::selectPage(size_t oneBased) {
  if (oneBased < 1 || oneBased > kStationPageCount) {
    return false;
  }
  page_ = stationPageFromIndex(oneBased - 1);
  return true;
}

size_t formatStationSensorReport(const StationView& view, char* out, size_t outSize) {
  Appender text{out, outSize};
  const SensorSnapshot& sensors = view.sensors;

  text.append("sensors:\n");
  if (sensors.bmp280Present) {
    text.append("  bmp280: %s at 0x%02X\n", sensors.bmp280Chip,
                static_cast<unsigned>(sensors.bmp280Address));
  } else {
    text.append("  bmp280: not found on 0x76/0x77\n");
  }
  if (sensors.temperatureValid) {
    text.append("  temperature: %.2f C\n", static_cast<double>(sensors.temperatureC));
  } else {
    text.append("  temperature: unavailable\n");
  }
  if (sensors.pressureValid) {
    text.append("  pressure: %.2f hPa (%s)\n", static_cast<double>(sensors.pressureHpa),
                trendName(sensors.pressureTrend));
  } else {
    text.append("  pressure: unavailable\n");
  }
  if (sensors.hasHumidity()) {
    text.append("  humidity: %.1f %% (%s, %.1f C)\n",
                static_cast<double>(sensors.humidityPct),
                dhtTypeName(sensors.dhtType),
                static_cast<double>(sensors.dhtTemperatureC));
  } else if (sensors.dhtPresent) {
    text.append("  humidity: sensor answered but no valid frame\n");
  } else {
    text.append("  humidity: no sensor on GPIO%u\n",
                static_cast<unsigned>(view.dhtPin));
  }
  return text.needed;
}

size_t formatStationLogStatus(const StationView& view, char* out, size_t outSize) {
  Appender text{out, outSize};
  char interval[16] = "?";
  formatIntervalLabel(view.logIntervalMs, interval, sizeof(interval));

  text.append("log: %s\n", view.logStorageReady ? "littlefs ready" : "storage unavailable");
  text.append("  state: %s\n", view.loggingEnabled ? "recording" : "paused");
  text.append("  interval: %s\n", interval);
  text.append("  records: %lu\n", static_cast<unsigned long>(view.logRecordCount));
  text.append("  size: %lu B\n", static_cast<unsigned long>(view.logFileBytes));
  text.append("  file: %s\n", kLogPath);
  return text.needed;
}

}  // namespace fw

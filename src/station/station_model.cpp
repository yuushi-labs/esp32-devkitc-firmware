#include "station/station_model.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "app_config.h"
#include "graphics/mini_graph.h"
#include "logging/log_record.h"
#include "sensors/comfort.h"
#include "station/alerts.h"

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

bool equalsIgnoreCase(const char* lhs, const char* rhs) {
  if (lhs == nullptr || rhs == nullptr) {
    return false;
  }
  while (*lhs != '\0' && *rhs != '\0') {
    const char a = (*lhs >= 'A' && *lhs <= 'Z') ? static_cast<char>(*lhs - 'A' + 'a') : *lhs;
    const char b = (*rhs >= 'A' && *rhs <= 'Z') ? static_cast<char>(*rhs - 'A' + 'a') : *rhs;
    if (a != b) {
      return false;
    }
    ++lhs;
    ++rhs;
  }
  return *lhs == '\0' && *rhs == '\0';
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

void appendProgressBar(uint8_t progress, char* out, size_t outSize) {
  char bar[11];
  const int filled = progress / 10;
  for (int i = 0; i < 10; ++i) {
    bar[i] = i < filled ? '#' : '-';
  }
  bar[10] = '\0';
  std::snprintf(out, outSize, "[%s] %u%%", bar, static_cast<unsigned>(progress));
}

}  // namespace

const char* stationModeName(StationMode mode) {
  switch (mode) {
    case StationMode::kEnvironment:
      return "environment";
    case StationMode::kLogs:
      return "logs";
    case StationMode::kWifi:
      return "wifi";
    case StationMode::kBle:
      return "ble";
    case StationMode::kTimer:
      return "timer";
    case StationMode::kSystem:
      return "system";
  }
  return "environment";
}

StationMode nextStationMode(StationMode mode) {
  return stationModeFromIndex((static_cast<size_t>(mode) + 1) % kStationModeCount);
}

StationMode previousStationMode(StationMode mode) {
  const size_t current = static_cast<size_t>(mode);
  return stationModeFromIndex((current + kStationModeCount - 1) % kStationModeCount);
}

StationMode stationModeFromIndex(size_t index) {
  return static_cast<StationMode>(index % kStationModeCount);
}

bool stationModeFromName(const char* name, StationMode* out) {
  if (name == nullptr || out == nullptr) {
    return false;
  }
  if (equalsIgnoreCase(name, "env") || equalsIgnoreCase(name, "environment") ||
      equalsIgnoreCase(name, "monitor")) {
    *out = StationMode::kEnvironment;
    return true;
  }
  if (equalsIgnoreCase(name, "log") || equalsIgnoreCase(name, "logs")) {
    *out = StationMode::kLogs;
    return true;
  }
  if (equalsIgnoreCase(name, "wifi") || equalsIgnoreCase(name, "web")) {
    *out = StationMode::kWifi;
    return true;
  }
  if (equalsIgnoreCase(name, "ble") || equalsIgnoreCase(name, "bluetooth")) {
    *out = StationMode::kBle;
    return true;
  }
  if (equalsIgnoreCase(name, "timer")) {
    *out = StationMode::kTimer;
    return true;
  }
  if (equalsIgnoreCase(name, "system") || equalsIgnoreCase(name, "sys")) {
    *out = StationMode::kSystem;
    return true;
  }
  if (name[0] >= '1' && name[0] <= '6' && name[1] == '\0') {
    *out = stationModeFromIndex(static_cast<size_t>(name[0] - '1'));
    return true;
  }
  return false;
}

const char* envViewName(EnvView view) {
  switch (view) {
    case EnvView::kSummary:
      return "summary";
    case EnvView::kGraph:
      return "graph";
    case EnvView::kDetail:
      return "detail";
  }
  return "summary";
}

EnvView nextEnvView(EnvView view) {
  return static_cast<EnvView>((static_cast<size_t>(view) + 1) % kEnvViewCount);
}

EnvView previousEnvView(EnvView view) {
  const size_t current = static_cast<size_t>(view);
  return static_cast<EnvView>((current + kEnvViewCount - 1) % kEnvViewCount);
}

const char* stationScreenName(StationScreen screen) {
  return screen == StationScreen::kMenu ? "menu" : "mode";
}

void buildStationMenu(const StationView& view, PageLines* out) {
  if (out == nullptr) {
    return;
  }
  out->clear();
  out->add("STATION MENU");
  for (size_t index = 0; index < kStationModeCount; ++index) {
    const StationMode mode = stationModeFromIndex(index);
    const char* label = "";
    switch (mode) {
      case StationMode::kEnvironment:
        label = "ENV MONITOR";
        break;
      case StationMode::kLogs:
        label = "LOG LittleFS";
        break;
      case StationMode::kWifi:
        label = "WIFI & WEB";
        break;
      case StationMode::kBle:
        label = "BLUETOOTH";
        break;
      case StationMode::kTimer:
        label = "TIMER";
        break;
      case StationMode::kSystem:
        label = "SYSTEM";
        break;
    }
    addf(*out, 1, "%c%u %s", mode == view.menuSelection ? '>' : ' ',
         static_cast<unsigned>(index + 1), label);
  }
  if (view.wifiConnected) {
    addf(*out, 1, "%s %s", view.clock, view.ip);
  } else {
    addf(*out, 1, "%s wifi off", view.clock);
  }
}

void buildStationPage(StationMode mode, const StationView& view, PageLines* out) {
  if (out == nullptr) {
    return;
  }
  out->clear();
  const SensorSnapshot& sensors = view.sensors;
  const AlertState alerts = evaluateAlerts(sensors, view.pressureDeltaHpa);

  switch (mode) {
    case StationMode::kEnvironment: {
      if (view.envView == EnvView::kDetail) {
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
        addf(*out, 1, "trend %s %+.1f", trendName(sensors.pressureTrend),
             static_cast<double>(view.pressureDeltaHpa));
        if (sensors.hasHumidity()) {
          addf(*out, 1, "%s %.1f %% %.1f %s", dhtTypeName(sensors.dhtType),
               static_cast<double>(sensors.humidityPct),
               static_cast<double>(sensors.dhtTemperatureC), kDegreeC);
        } else if (sensors.dhtPresent) {
          out->add("DHT no answer");
        } else {
          out->add("DHT not found");
        }
        if (sensors.hasTemperature() && sensors.hasHumidity()) {
          const float index = discomfortIndex(sensors.displayTemperatureC(), sensors.humidityPct);
          addf(*out, 1, "DI %.1f %s", static_cast<double>(index), comfortLabel(index));
        } else {
          out->add("DI --");
        }
        addf(*out, 1, "poll %.1f s", kSensorPollIntervalMs / 1000.0);
        break;
      }

      out->add("ENV STATION");
      if (sensors.hasTemperature()) {
        addf(*out, 2, "%.1f %s", static_cast<double>(sensors.displayTemperatureC()), kDegreeC);
      } else {
        addf(*out, 2, "--.- %s", kDegreeC);
      }
      if (sensors.hasPressure()) {
        addf(*out, 1, "%.1f hPa %s", static_cast<double>(sensors.pressureHpa),
             trendName(sensors.pressureTrend));
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
      if (sensors.hasTemperature() && sensors.hasHumidity()) {
        const float index = discomfortIndex(sensors.displayTemperatureC(), sensors.humidityPct);
        addf(*out, 1, "%s/%s", comfortLabel(index), weatherTrendLabel(sensors.pressureTrend));
      } else {
        addf(*out, 1, "weather %s", weatherTrendLabel(sensors.pressureTrend));
      }
      if (view.loggingEnabled) {
        addf(*out, 1, "REC %lu n%lus", static_cast<unsigned long>(view.logRecordCount),
             static_cast<unsigned long>((view.logIntervalRemainingMs + 999u) / 1000u));
      } else {
        addf(*out, 1, "PAUSED %lu rec", static_cast<unsigned long>(view.logRecordCount));
      }
      if (alerts.any()) {
        // The panel fits 21 characters per line at scale 1, so keep this
        // compact; `sensors` prints the full text.
        char alertText[24] = {0};
        size_t used = 0;
        if (alerts.highTemperature) {
          used += static_cast<size_t>(std::snprintf(
              alertText + used, sizeof(alertText) - used, "HOT%.1f ",
              static_cast<double>(sensors.displayTemperatureC())));
        }
        if (alerts.lowHumidity && used < sizeof(alertText)) {
          used += static_cast<size_t>(std::snprintf(
              alertText + used, sizeof(alertText) - used, "DRY%.0f ",
              static_cast<double>(sensors.humidityPct)));
        }
        if (alerts.pressureJump && used < sizeof(alertText)) {
          std::snprintf(alertText + used, sizeof(alertText) - used, "PJMP");
        }
        addf(*out, 1, "! %s", alertText);
      } else {
        out->add("alert none");
      }
      break;
    }

    case StationMode::kLogs: {
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
      out->add(view.ntpSynced ? "time ntp" : "time not synced");
      out->add(view.logStorageReady ? "/log.csv" : "fs not ready");
      break;
    }

    case StationMode::kWifi: {
      out->add(view.wifiMode == WifiMode::kAccessPoint ? "WIFI SETUP (AP)" : "WIFI STATION");
      if (view.wifiMode == WifiMode::kAccessPoint) {
        addf(*out, 1, "ap %s", kSetupApSsid);
        addf(*out, 1, "pass %s",
             view.apPassword[0] != '\0' ? view.apPassword : "-");
        out->add("join, then open");
        out->add("http://192.168.4.1/");
      } else if (view.wifiConnected) {
        addf(*out, 1, "ssid %s", view.wifiSsid[0] != '\0' ? view.wifiSsid : "-");
        addf(*out, 1, "ip %s", view.ip);
        addf(*out, 1, "rssi %d dBm", static_cast<int>(view.rssi));
        addf(*out, 1, "time %s", view.ntpSynced ? view.clock : "no ntp");
      } else {
        addf(*out, 1, "ssid %s", view.wifiSsid[0] != '\0' ? view.wifiSsid : "not set");
        out->add("connecting...");
        out->add("PSH for AP setup");
      }
      out->add("CON show QR");
      out->add("PSH AP/STA  BAK back");
      break;
    }

    case StationMode::kBle: {
      addf(*out, 1, "BLE %s", view.bleEnabled ? "ON" : "OFF");
      addf(*out, 1, "name %s", kBleDeviceName);
      out->add("gatt env service");
      addf(*out, 1, "hid %s", view.bleHidMode ? "media remote" : "off");
      addf(*out, 1, "link %s", view.bleConnected ? "connected" : "advertising");
      if (view.bleHidMode) {
        out->add("PSH play  CON next");
        out->add("BAK prev  hold menu");
      } else {
        out->add("PSH HID on");
        out->add("BAK hold menu");
      }
      break;
    }

    case StationMode::kTimer: {
      addf(*out, 1, "TIMER %s", view.timerPomodoro ? "POMODORO" : "COUNTDOWN");
      addf(*out, 2, "%s", view.timerRemaining);
      addf(*out, 1, "%s %s", view.timerPhase, view.timerRunning ? "running" : "stopped");
      char bar[20];
      appendProgressBar(view.timerProgress, bar, sizeof(bar));
      out->add(bar);
      out->add("CON start/pause");
      out->add("PSH pomodoro BAK reset");
      break;
    }

    case StationMode::kSystem: {
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
      addf(*out, 1, "wifi %s", view.wifiConnected ? view.ip : "off");
      addf(*out, 1, "ble %s", view.bleHidMode ? "hid" : (view.bleEnabled ? "on" : "off"));
      addf(*out, 1, "log %s", view.logStorageReady ? "ready" : "unavailable");
      break;
    }
  }
}

void buildStationGraphScreen(const StationView& view, Framebuffer* out) {
  if (out == nullptr) {
    return;
  }
  out->clear();
  char title[32];
  std::snprintf(title, sizeof(title), "T %.1f  P %.1f",
                static_cast<double>(view.sensors.hasTemperature()
                                        ? view.sensors.displayTemperatureC()
                                        : 0.0f),
                static_cast<double>(view.sensors.hasPressure() ? view.sensors.pressureHpa
                                                               : 0.0f));
  out->drawText(0, 0, title);
  if (view.temperatureGraph == nullptr || view.pressureGraph == nullptr) {
    out->drawText(0, 24, "no samples yet");
    return;
  }
  drawMiniGraph(*out, *view.temperatureGraph, 0, 9, Framebuffer::kWidth, 25);
  drawMiniGraph(*out, *view.pressureGraph, 0, 36, Framebuffer::kWidth, 27);
}

void PressureTrendTracker::update(float pressureHpa, uint32_t nowMs) {
  if (!hasReference_) {
    hasReference_ = true;
    referenceHpa_ = pressureHpa;
    referenceMs_ = nowMs;
    deltaHpa_ = 0.0f;
    return;
  }

  deltaHpa_ = pressureHpa - referenceHpa_;
  if ((nowMs - referenceMs_) < kPressureTrendWindowMs) {
    return;
  }

  if (deltaHpa_ >= kPressureTrendThresholdHpa) {
    trend_ = 1;
  } else if (deltaHpa_ <= -kPressureTrendThresholdHpa) {
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
  deltaHpa_ = 0.0f;
}

void StationController::menuMove(int delta) {
  int index = static_cast<int>(menuSelection_);
  index = (index + delta) % static_cast<int>(kStationModeCount);
  if (index < 0) {
    index += static_cast<int>(kStationModeCount);
  }
  menuSelection_ = stationModeFromIndex(static_cast<size_t>(index));
}

bool StationController::enter() {
  if (screen_ == StationScreen::kMode) {
    return false;
  }
  mode_ = menuSelection_;
  screen_ = StationScreen::kMode;
  return true;
}

bool StationController::back() {
  if (screen_ == StationScreen::kMenu) {
    return false;
  }
  screen_ = StationScreen::kMenu;
  menuSelection_ = mode_;
  return true;
}

bool StationController::selectMode(StationMode mode) {
  mode_ = mode;
  menuSelection_ = mode;
  screen_ = StationScreen::kMode;
  return true;
}

bool StationController::selectModeIndex(size_t oneBased) {
  if (oneBased < 1 || oneBased > kStationModeCount) {
    return false;
  }
  return selectMode(stationModeFromIndex(oneBased - 1));
}

bool StationController::selectModeByName(const char* name) {
  if (equalsIgnoreCase(name, "menu") || equalsIgnoreCase(name, "home")) {
    screen_ = StationScreen::kMenu;
    return true;
  }
  StationMode mode = StationMode::kEnvironment;
  if (!stationModeFromName(name, &mode)) {
    return false;
  }
  return selectMode(mode);
}

void StationController::cycleEnvView(int delta) {
  if (delta == 0) {
    return;
  }
  size_t index = static_cast<size_t>(envView_);
  if (delta > 0) {
    index = (index + static_cast<size_t>(delta)) % kEnvViewCount;
  } else {
    const size_t step = static_cast<size_t>(-delta) % kEnvViewCount;
    index = (index + kEnvViewCount - step) % kEnvViewCount;
  }
  envView_ = static_cast<EnvView>(index);
}

size_t formatStationSensorReport(const StationView& view, char* out, size_t outSize) {
  Appender text{out, outSize};
  const SensorSnapshot& sensors = view.sensors;
  const AlertState alerts = evaluateAlerts(sensors, view.pressureDeltaHpa);

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
  if (sensors.hasTemperature() && sensors.hasHumidity()) {
    const float index = discomfortIndex(sensors.displayTemperatureC(), sensors.humidityPct);
    text.append("  comfort: %.1f (%s)\n", static_cast<double>(index), comfortLabel(index));
  }
  text.append("  weather: %s\n", weatherTrendLabel(sensors.pressureTrend));
  char alertText[40];
  formatAlertLine(alerts, sensors, alertText, sizeof(alertText));
  text.append("  alert: %s\n", alertText);
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
  text.append("  timestamp: %s\n", view.ntpSynced ? "ntp (local time)" : "not synced");
  text.append("  file: %s\n", kLogPath);
  return text.needed;
}

size_t formatStationBleReport(const StationView& view, char* out, size_t outSize) {
  Appender text{out, outSize};
  text.append("ble: %s\n", view.bleEnabled ? "enabled" : "disabled");
  text.append("  name: %s\n", kBleDeviceName);
  text.append("  link: %s\n", view.bleConnected ? "connected" : "advertising");
  text.append("  gatt: environmental sensing (0x181A)\n");
  text.append("  hid: %s\n", view.bleHidMode ? "media remote on" : "off");
  text.append("  hint: encoder=volume, PSH=play/pause, CON=next, BAK=prev\n");
  return text.needed;
}

size_t formatStationNavigation(const StationController& controller, char* out,
                               size_t outSize) {
  Appender text{out, outSize};
  text.append("screen=%s mode=%s selection=%s view=%s\n",
              stationScreenName(controller.screen()),
              stationModeName(controller.mode()),
              stationModeName(controller.menuSelection()),
              envViewName(controller.envView()));
  return text.needed;
}

}  // namespace fw

#include "station/station_app.h"

#include <cstdio>
#include <cstring>

#include "graphics/qr_render.h"
#include "net/ntp_clock.h"
#include "net/web_dashboard.h"
#include "station/alerts.h"

namespace fw {
namespace {

constexpr uint32_t kBmpRetryIntervalMs = 10000;

void printLine(const char* text) {
  if (text != nullptr) {
    Serial.print(text);
  }
}

// FNV-1a over the panel bytes: cheap, and enough to skip sending a frame that is
// identical to the one on the panel. A full 128x64 frame costs about 90 ms on
// the 100 kHz I2C bus, so the Wi-Fi and BLE stacks must not pay for it twice.
uint32_t frameHash(const Framebuffer& framebuffer) {
  uint32_t hash = 2166136261u;
  const uint8_t* bytes = framebuffer.buffer();
  for (size_t index = 0; index < Framebuffer::kWidth * Framebuffer::kPageCount;
       ++index) {
    hash = (hash ^ bytes[index]) * 16777619u;
  }
  return hash;
}

}  // namespace

StationApp::StationApp()
    : bus_(),
      framebuffer_(),
      display_(bus_, kOledPrimaryAddress, framebuffer_),
      bmp280_(bus_, kBmp280PrimaryAddress),
      dht_(),
      storage_(kLogPath),
      recorder_(storage_, kLogIntervalsMs, kLogIntervalCount),
      inputs_(),
      controller_(),
      trend_(),
      snapshot_(),
      view_(),
      page_(),
      scan_(),
      wifi_(),
      web_(),
      ble_(),
      timer_() {}

void StationApp::begin() {
  startedMs_ = millis();

  bus_.begin(kI2cSdaPin, kI2cSclPin, kI2cClockHz);
  dht_.begin(kDhtPin);
  inputs_.begin();

  oledAddress_ = Ssd1306Display::detectAddress(bus_);
  displayReady_ = oledAddress_ != 0 && display_.begin(oledAddress_);
  Serial.printf("oled: %s\n",
                displayReady_ ? "SSD1306 ready" : "not found (serial only)");

  scan_ = scanI2cBus(bus_);
  Serial.print("i2c scan: ");
  for (size_t index = 0; index < scan_.count; ++index) {
    Serial.printf("0x%02X ", static_cast<unsigned>(scan_.entries[index].address));
  }
  if (scan_.empty()) {
    Serial.print("none");
  }
  Serial.println();

  const uint8_t bmpAddress = findBmp280Address(bus_);
  const Bmp280Status status =
      bmp280_.begin(bmpAddress != 0 ? bmpAddress : kBmp280PrimaryAddress);
  snapshot_.bmp280Present = status == Bmp280Status::kOk;
  snapshot_.bmp280Chip = bmp280_.chipName();
  snapshot_.bmp280Address = bmp280_.address();
  Serial.printf("bmp280: %s%s\n", bmp280StatusName(status),
                status == Bmp280Status::kOk ? "" : " (continuing without it)");

  const bool storageReady = recorder_.begin(millis());
  Serial.printf("log: %s records=%lu\n", storageReady ? "littlefs ready" : "unavailable",
                static_cast<unsigned long>(recorder_.recordCount()));

  wifi_.begin();
  web_.begin(wifi_, *this);
  ble_.begin();

  lastSensorPollMs_ = millis() - kSensorPollIntervalMs;  // poll right away
  lastDhtPollMs_ = millis() - kDhtPollIntervalMs;
  lastBmpRetryMs_ = millis();
  lastDisplayMs_ = millis();
  lastGraphSampleMs_ = millis() - kGraphSampleIntervalMs;

  updateView(millis());
  redraw();
}

void StationApp::loop() {
  const uint32_t nowMs = millis();

  const InputEvent event = inputs_.poll(nowMs);
  if (event.any()) {
    handleInputs(event, nowMs);
  }

  pollSensors(nowMs);
  pollDht(nowMs);
  sampleGraphs(nowMs);

  timer_.update(nowMs);
  if (timer_.consumeFinished()) {
    alarmUntilMs_ = nowMs + 10000;
    alarmActive_ = true;
    Serial.printf("timer: finished (%s)\n", timerPhaseName(timer_.phase()));
    dirty_ = true;
  }
  // The alarm is a separate flag: comparing an unset deadline (0) with millis()
  // would fire after half a millisecond-counter wrap (~24.9 days).
  if (alarmActive_ && static_cast<int32_t>(nowMs - alarmUntilMs_) >= 0) {
    alarmActive_ = false;
  }

  LogSample sample = makeLogSample(snapshot_, nowMs);
  fillTimestamp(&sample);
  if (recorder_.poll(sample, nowMs)) {
    Serial.printf("log: recorded uptime_ms=%lu temp=%s pressure=%s humidity=%s\n",
                  static_cast<unsigned long>(sample.uptimeMs),
                  sample.hasTemperature ? "yes" : "-",
                  sample.hasPressure ? "yes" : "-",
                  sample.hasHumidity ? "yes" : "-");
    dirty_ = true;
  }

  wifi_.loop(nowMs);
  web_.loop(nowMs);
  ble_.loop(nowMs, snapshot_, currentAlerts().any());

  if (qrVisible_ && static_cast<int32_t>(nowMs - qrUntilMs_) >= 0) {
    qrVisible_ = false;
    dirty_ = true;
  }

  if (dirty_ || (nowMs - lastDisplayMs_) >= kDisplayRefreshMs) {
    updateView(nowMs);
    redraw();
    lastDisplayMs_ = nowMs;
  }
}

bool StationApp::alertActive() const {
  if (alarmActive_) {
    return true;
  }
  return currentAlerts().any();
}

AlertState StationApp::currentAlerts() const {
  return evaluateAlerts(snapshot_, trend_.deltaHpa());
}

void StationApp::pollSensors(uint32_t nowMs) {
  if ((nowMs - lastSensorPollMs_) < kSensorPollIntervalMs) {
    return;
  }
  lastSensorPollMs_ = nowMs;

  if (!bmp280_.present() && (nowMs - lastBmpRetryMs_) >= kBmpRetryIntervalMs) {
    lastBmpRetryMs_ = nowMs;
    const uint8_t address = findBmp280Address(bus_);
    if (address != 0) {
      const Bmp280Status status = bmp280_.begin(address);
      Serial.printf("bmp280: %s at 0x%02X\n", bmp280StatusName(status),
                    static_cast<unsigned>(address));
    }
    snapshot_.bmp280Present = bmp280_.present();
    snapshot_.bmp280Chip = bmp280_.chipName();
    snapshot_.bmp280Address = bmp280_.address();
    dirty_ = true;
  }

  if (!bmp280_.present()) {
    return;
  }

  Bmp280Reading reading;
  if (!bmp280_.read(&reading)) {
    snapshot_.temperatureValid = false;
    snapshot_.pressureValid = false;
    dirty_ = true;
    return;
  }

  snapshot_.temperatureValid = true;
  snapshot_.temperatureC = reading.temperatureC;
  snapshot_.pressureValid = true;
  snapshot_.pressureHpa = reading.pressureHpa;
  trend_.update(reading.pressureHpa, nowMs);
  snapshot_.pressureTrend = trend_.trend();
  dirty_ = true;
}

void StationApp::pollDht(uint32_t nowMs) {
  if ((nowMs - startedMs_) < kDhtStartupDelayMs) {
    return;
  }
  if ((nowMs - lastDhtPollMs_) < kDhtPollIntervalMs) {
    return;
  }
  lastDhtPollMs_ = nowMs;

  uint32_t pulses[DhtGpioReader::kFrameBits] = {0};
  size_t bitCount = 0;
  if (!dht_.readFrame(pulses, DhtGpioReader::kFrameBits, &bitCount)) {
    snapshot_.dhtPresent = dht_.responded();
    snapshot_.humidityValid = false;
    if (!snapshot_.dhtPresent) {
      snapshot_.dhtType = DhtType::kUnknown;
    }
    dirty_ = true;
    return;
  }

  const DhtFrame frame = decodeDhtFrameAuto(pulses, bitCount);
  if (frame.reading.valid) {
    snapshot_.dhtPresent = true;
    snapshot_.dhtType = frame.type;
    snapshot_.humidityValid = true;
    snapshot_.humidityPct = frame.reading.humidityPct;
    snapshot_.dhtTemperatureC = frame.reading.temperatureC;
  } else {
    snapshot_.dhtPresent = true;
    snapshot_.humidityValid = false;
  }
  dirty_ = true;
}

void StationApp::sampleGraphs(uint32_t nowMs) {
  if ((nowMs - lastGraphSampleMs_) < kGraphSampleIntervalMs) {
    return;
  }
  lastGraphSampleMs_ = nowMs;
  if (snapshot_.hasTemperature()) {
    temperatureGraph_.push(snapshot_.displayTemperatureC());
  }
  if (snapshot_.hasPressure()) {
    pressureGraph_.push(snapshot_.pressureHpa);
  }
}

void StationApp::updateQrPayload() {
  if (!wifi_.connected()) {
    stationUrl_[0] = '\0';
    return;
  }
  formatStationUrl(wifi_.ip(), stationUrl_, sizeof(stationUrl_));
}

void StationApp::updateView(uint32_t nowMs) {
  view_.version = FW_VERSION;
  view_.sensors = snapshot_;
  view_.pressureDeltaHpa = trend_.deltaHpa();
  view_.loggingEnabled = recorder_.enabled();
  view_.logStorageReady = recorder_.storageReady();
  view_.logRecordCount = recorder_.recordCount();
  view_.logIntervalMs = recorder_.intervalMs();
  view_.logIntervalRemainingMs = recorder_.intervalRemainingMs(nowMs);
  view_.logFileBytes = recorder_.fileSize();
  view_.oledAddress = oledAddress_;
  view_.uptimeMs = nowMs;
  view_.freeHeapBytes = ESP.getFreeHeap();
  view_.dhtPin = kDhtPin;
  formatI2cAddressList(scan_, view_.i2cAddresses, sizeof(view_.i2cAddresses));

  view_.wifiMode = wifi_.mode();
  view_.wifiConnected = wifi_.connected();
  view_.ntpSynced = wifi_.ntpSynced();
  std::snprintf(view_.wifiSsid, sizeof(view_.wifiSsid), "%s", wifi_.ssid());
  std::snprintf(view_.ip, sizeof(view_.ip), "%s", wifi_.ip());
  std::snprintf(view_.apPassword, sizeof(view_.apPassword), "%s",
                wifi_.setupApPassword());
  view_.rssi = wifi_.rssi();
  const int64_t epoch = wifi_.epochSeconds();
  if (epoch > 0) {
    formatClockTime(epoch, kLocalUtcOffsetSeconds, view_.clock, sizeof(view_.clock));
    updateQrPayload();
  } else {
    std::snprintf(view_.clock, sizeof(view_.clock), "--:--");
  }

  view_.bleEnabled = ble_.enabled();
  view_.bleHidMode = ble_.hidMode();
  view_.bleConnected = ble_.connected();

  timer_.formatRemaining(view_.timerRemaining, sizeof(view_.timerRemaining));
  view_.timerRunning = timer_.running();
  view_.timerProgress = timer_.progressPercent();
  view_.timerPomodoro = timer_.pomodoro();
  view_.timerPhase = timerPhaseName(timer_.phase());

  view_.envView = controller_.envView();
  view_.menuSelection = controller_.menuSelection();
  view_.temperatureGraph = &temperatureGraph_;
  view_.pressureGraph = &pressureGraph_;
  view_.qrVisible = qrVisible_;
  view_.qrCode = &qr_;
}

void StationApp::redraw() {
  bool drawLines = true;
  if (controller_.screen() == StationScreen::kMenu) {
    buildStationMenu(view_, &page_);
  } else if (controller_.mode() == StationMode::kEnvironment &&
             controller_.envView() == EnvView::kGraph) {
    buildStationGraphScreen(view_, &framebuffer_);
    drawLines = false;
  } else if (controller_.mode() == StationMode::kWifi && qrVisible_ && qr_.valid()) {
    framebuffer_.clear();
    drawLines = false;
    if (drawQrCode(framebuffer_, qr_, Framebuffer::kWidth, Framebuffer::kHeight) == 0) {
      // The panel cannot hold this code at a readable size, so show the address
      // instead of a QR that no camera can read.
      page_.clear();
      page_.add("QR too large");
      page_.add(stationUrl_);
      drawPageLines(framebuffer_, page_);
    }
  } else {
    buildStationPage(controller_.mode(), view_, &page_);
  }

  if (drawLines) {
    drawPageLines(framebuffer_, page_);
  }
  if (displayReady_) {
    const uint32_t hash = frameHash(framebuffer_);
    if (!frameSent_ || hash != lastFrameHash_) {
      if (display_.flush()) {
        lastFrameHash_ = hash;
        frameSent_ = true;
      } else {
        displayReady_ = false;
        frameSent_ = false;
        Serial.println("oled: flush failed, continuing without the panel");
      }
    }
  }
  dirty_ = false;
}

void StationApp::enterMode(StationMode mode) {
  controller_.selectMode(mode);
  if (mode == StationMode::kWifi) {
    updateQrPayload();
  }
  // A button press that started in another screen must not be mistaken for one
  // that began here.
  confirmPressedInLogs_ = false;
  confirmLongFired_ = false;
  Serial.printf("mode: %s\n", stationModeName(mode));
  dirty_ = true;
}

void StationApp::switchMode(StationMode mode) {
  if (controller_.screen() == StationScreen::kMode) {
    leaveMode(controller_.mode());
  }
  enterMode(mode);
}

void StationApp::leaveMode(StationMode mode) {
  if (mode == StationMode::kBle && ble_.hidMode()) {
    ble_.setHidMode(false);
  }
  if (mode == StationMode::kWifi) {
    qrVisible_ = false;
  }
  confirmPressedInLogs_ = false;
  confirmLongFired_ = false;
}

void StationApp::handleInputs(const InputEvent& event, uint32_t nowMs) {
  const bool push = event.press[static_cast<size_t>(InputButton::kPush)];
  const bool back = event.press[static_cast<size_t>(InputButton::kBack)];
  const bool confirm = event.press[static_cast<size_t>(InputButton::kConfirm)];
  const bool backLong = event.longPress[static_cast<size_t>(InputButton::kBack)];

  if (backLong && controller_.screen() == StationScreen::kMode) {
    leaveMode(controller_.mode());
    controller_.back();
    Serial.printf("input: BAK long -> menu (selection %s)\n",
                  stationModeName(controller_.menuSelection()));
    dirty_ = true;
    return;
  }

  if (controller_.screen() == StationScreen::kMenu) {
    if (event.encoderDelta != 0) {
      controller_.menuMove(event.encoderDelta);
      Serial.printf("input: menu -> %s\n", stationModeName(controller_.menuSelection()));
      dirty_ = true;
    }
    if (push || confirm) {
      enterMode(controller_.menuSelection());
    }
    return;
  }

  if (event.encoderDelta != 0) {
    switch (controller_.mode()) {
      case StationMode::kEnvironment:
        controller_.cycleEnvView(event.encoderDelta);
        Serial.printf("input: env view %s\n", envViewName(controller_.envView()));
        dirty_ = true;
        break;
      case StationMode::kLogs:
        if (event.encoderDelta > 0) {
          recorder_.nextInterval();
        } else {
          recorder_.previousInterval();
        }
        dirty_ = true;
        break;
      case StationMode::kBle:
        if (ble_.hidMode()) {
          for (int8_t step = 0; step != event.encoderDelta;
               step += (event.encoderDelta > 0 ? 1 : -1)) {
            ble_.sendMediaAction(mediaActionForEncoder(event.encoderDelta > 0 ? 1 : -1));
          }
        }
        break;
      case StationMode::kTimer:
        timer_.adjust(event.encoderDelta);
        Serial.printf("input: timer %u min\n", static_cast<unsigned>(timer_.minutes()));
        dirty_ = true;
        break;
      case StationMode::kWifi:
      case StationMode::kSystem:
        break;
    }
  }

  (void)back;
  handleModeInputs(event, nowMs);
}

void StationApp::handleModeInputs(const InputEvent& event, uint32_t nowMs) {
  const bool push = event.press[static_cast<size_t>(InputButton::kPush)];
  const bool back = event.press[static_cast<size_t>(InputButton::kBack)];
  const bool confirm = event.press[static_cast<size_t>(InputButton::kConfirm)];
  const bool confirmLong = event.longPress[static_cast<size_t>(InputButton::kConfirm)];
  const bool confirmRelease = event.release[static_cast<size_t>(InputButton::kConfirm)];

  switch (controller_.mode()) {
    case StationMode::kEnvironment:
      if (push || confirm) {
        controller_.cycleEnvView(1);
        dirty_ = true;
      }
      if (back) {
        controller_.back();
        dirty_ = true;
      }
      break;

    case StationMode::kLogs:
      if (push) {
        recorder_.nextInterval();
        char interval[16];
        formatIntervalLabel(recorder_.intervalMs(), interval, sizeof(interval));
        Serial.printf("input: PSH -> interval %s\n", interval);
        dirty_ = true;
      }
      if (confirm) {
        confirmPressedInLogs_ = true;
        confirmLongFired_ = false;
      }
      if (confirmLong && confirmPressedInLogs_) {
        confirmLongFired_ = true;
        const bool enabled = recorder_.toggleEnabled();
        Serial.printf("input: CON long -> recording %s\n", enabled ? "on" : "off");
        dirty_ = true;
      }
      if (confirmRelease) {
        // Only a press that began on this screen is a manual record; the release
        // of the press that opened the screen must not add a row.
        if (confirmPressedInLogs_ && !confirmLongFired_) {
          recordNow(nowMs, true);
        }
        confirmPressedInLogs_ = false;
      }
      if (back) {
        controller_.back();
        confirmPressedInLogs_ = false;
        dirty_ = true;
      }
      break;

    case StationMode::kWifi:
      if (confirm) {
        showQr();
      }
      if (push) {
        // Toggle between the setup AP and the stored network so a stray press
        // cannot leave the station stranded in setup mode.
        if (wifi_.apActive() && wifi_.hasStoredCredentials()) {
          wifi_.startStation();
          Serial.println("wifi: back to station mode");
        } else {
          wifi_.startAccessPoint();
          Serial.printf("wifi: setup AP '%s' password %s\n", kSetupApSsid,
                        wifi_.setupApPassword());
        }
        dirty_ = true;
      }
      if (back) {
        controller_.back();
        dirty_ = true;
      }
      break;

    case StationMode::kBle:
      if (ble_.hidMode()) {
        const MediaAction action = mediaActionForButtons(push, confirm, back);
        if (action != MediaAction::kNone) {
          ble_.sendMediaAction(action);
        }
      } else {
        if (push) {
          ble_.setHidMode(true);
          dirty_ = true;
        }
        if (confirm) {
          Serial.printf("ble: %s\n", ble_.connected() ? "connected" : "advertising");
        }
        if (back) {
          controller_.back();
          dirty_ = true;
        }
      }
      break;

    case StationMode::kTimer:
      if (confirm) {
        timer_.toggle(nowMs);
        dirty_ = true;
      }
      if (push) {
        timer_.togglePomodoro();
        dirty_ = true;
      }
      if (back) {
        timer_.reset();
        Serial.println("input: timer reset");
        dirty_ = true;
      }
      break;

    case StationMode::kSystem:
      if (confirm) {
        printScan();
        dirty_ = true;
      }
      if (back) {
        controller_.back();
        dirty_ = true;
      }
      break;
  }
}

void StationApp::recordNow(uint32_t nowMs, bool manual) {
  LogSample sample = makeLogSample(snapshot_, nowMs);
  fillTimestamp(&sample);
  if (!recorder_.record(sample, nowMs)) {
    Serial.println("log: write failed");
    return;
  }
  Serial.printf("log: %s record #%lu\n", manual ? "manual" : "auto",
                static_cast<unsigned long>(recorder_.recordCount()));
  dirty_ = true;
}

void StationApp::fillTimestamp(LogSample* sample) const {
  if (sample == nullptr) {
    return;
  }
  const int64_t epoch = wifi_.epochSeconds();
  if (epoch > 0) {
    formatTimestamp(epoch, kLocalUtcOffsetSeconds, sample->timestamp,
                    sizeof(sample->timestamp));
  }
}

void StationApp::printSensorReport() {
  updateView(millis());
  char buffer[512];
  formatStationSensorReport(view_, buffer, sizeof(buffer));
  printLine(buffer);
}

void StationApp::printLogStatus() {
  updateView(millis());
  char buffer[320];
  formatStationLogStatus(view_, buffer, sizeof(buffer));
  printLine(buffer);
}

void StationApp::printScan() {
  scan_ = scanI2cBus(bus_);
  char buffer[384];
  formatI2cScan(scan_, buffer, sizeof(buffer));
  printLine(buffer);
  dirty_ = true;
}

void StationApp::printWifiStatus() {
  WifiStatus status;
  status.mode = wifi_.mode();
  status.connected = wifi_.connected();
  status.ntpSynced = wifi_.ntpSynced();
  std::snprintf(status.ssid, sizeof(status.ssid), "%s", wifi_.ssid());
  std::snprintf(status.ip, sizeof(status.ip), "%s", wifi_.ip());
  status.rssi = wifi_.rssi();
  status.epochSeconds = wifi_.epochSeconds();
  status.utcOffsetSeconds = kLocalUtcOffsetSeconds;
  status.uptimeMs = millis();
  char buffer[320];
  formatWifiStatus(status, buffer, sizeof(buffer));
  printLine(buffer);
  if (stationUrl_[0] != '\0') {
    Serial.printf("  dashboard: %s\n", stationUrl_);
  }
}

void StationApp::printBleStatus() {
  updateView(millis());
  char buffer[384];
  formatStationBleReport(view_, buffer, sizeof(buffer));
  printLine(buffer);
}

void StationApp::printTimerStatus() {
  char buffer[256];
  timer_.formatStatus(buffer, sizeof(buffer));
  printLine(buffer);
}

void StationApp::printNavigation() {
  char buffer[160];
  formatStationNavigation(controller_, buffer, sizeof(buffer));
  printLine(buffer);
}

void StationApp::announceMode() {
  if (controller_.screen() == StationScreen::kMenu) {
    Serial.printf("menu: selection %s\n", stationModeName(controller_.menuSelection()));
  } else {
    Serial.printf("mode: %s\n", stationModeName(controller_.mode()));
  }
}

void StationApp::showQr() {
  if (!wifi_.connected()) {
    Serial.println("qr: wifi is not connected");
    return;
  }
  updateQrPayload();
  if (!qr_.encode(stationUrl_)) {
    Serial.println("qr: encode failed");
    return;
  }
  // Switch first: leaving a mode hides the QR, so setting the flag before the
  // switch would immediately clear it again.
  switchMode(StationMode::kWifi);
  qrVisible_ = true;
  qrUntilMs_ = millis() + kQrDisplayDurationMs;
  dirty_ = true;
  Serial.printf("qr: %s\n", stationUrl_);
}

size_t StationApp::buildStatusJson(char* out, size_t outSize) {
  updateView(millis());
  DashboardState state;
  state.version = view_.version;
  state.wifiMode = view_.wifiMode;
  state.wifiConnected = view_.wifiConnected;
  std::snprintf(state.ssid, sizeof(state.ssid), "%s", view_.wifiSsid);
  std::snprintf(state.ip, sizeof(state.ip), "%s", view_.ip);
  state.rssi = view_.rssi;
  state.ntpSynced = view_.ntpSynced;
  const int64_t epoch = wifi_.epochSeconds();
  if (epoch > 0) {
    formatTimestamp(epoch, kLocalUtcOffsetSeconds, state.timestamp,
                    sizeof(state.timestamp));
  }
  state.logStorageReady = view_.logStorageReady;
  state.loggingEnabled = view_.loggingEnabled;
  state.logRecordCount = view_.logRecordCount;
  state.logIntervalMs = view_.logIntervalMs;
  state.logFileBytes = view_.logFileBytes;
  state.bleEnabled = view_.bleEnabled;
  state.bleHidMode = view_.bleHidMode;
  state.bleConnected = view_.bleConnected;
  state.timerRunning = view_.timerRunning;
  std::snprintf(state.timerRemaining, sizeof(state.timerRemaining), "%s",
                view_.timerRemaining);
  state.timerPhase = view_.timerPhase;
  state.mode = controller_.screen() == StationScreen::kMenu
                   ? "menu"
                   : stationModeName(controller_.mode());
  state.uptimeMs = view_.uptimeMs;
  state.freeHeapBytes = view_.freeHeapBytes;
  state.sensors = snapshot_;
  state.alerts = currentAlerts();
  return formatStatusJson(state, out, outSize);
}

bool StationApp::clearLog() {
  const bool ok = recorder_.clear(millis());
  Serial.printf("log: %s\n", ok ? "cleared, header rewritten" : "clear failed");
  dirty_ = true;
  return ok;
}

void StationApp::saveWifiCredentials(const WifiCredentials& credentials) {
  if (wifi_.saveCredentials(credentials)) {
    Serial.printf("wifi: saved '%s', reconnecting\n", credentials.ssid);
  } else {
    Serial.println("wifi: could not save credentials");
  }
  dirty_ = true;
}

bool StationApp::handleCommand(const Command& command) {
  switch (command.type) {
    case CommandType::kScan:
      printScan();
      return true;
    case CommandType::kSensors:
      printSensorReport();
      return true;
    case CommandType::kLogStatus:
      printLogStatus();
      return true;
    case CommandType::kLogOn:
      recorder_.setEnabled(true);
      Serial.println("log: recording on");
      dirty_ = true;
      return true;
    case CommandType::kLogOff:
      recorder_.setEnabled(false);
      Serial.println("log: recording off");
      dirty_ = true;
      return true;
    case CommandType::kLogNow:
      recordNow(millis(), true);
      printLogStatus();
      return true;
    case CommandType::kLogClear:
      clearLog();
      return true;
    case CommandType::kPageNext:
      if (controller_.screen() == StationScreen::kMenu) {
        controller_.menuMove(1);
      } else {
        switchMode(nextStationMode(controller_.mode()));
      }
      announceMode();
      dirty_ = true;
      return true;
    case CommandType::kPagePrev:
      if (controller_.screen() == StationScreen::kMenu) {
        controller_.menuMove(-1);
      } else {
        switchMode(previousStationMode(controller_.mode()));
      }
      announceMode();
      dirty_ = true;
      return true;
    case CommandType::kPageSelect:
      if (command.page >= 1 && command.page <= kStationModeCount) {
        switchMode(stationModeFromIndex(static_cast<size_t>(command.page) - 1));
        announceMode();
      } else {
        Serial.println("error: page must be 1..6");
      }
      dirty_ = true;
      return true;
    case CommandType::kWifiStatus:
      printWifiStatus();
      return true;
    case CommandType::kWifiAp:
      wifi_.startAccessPoint();
      Serial.printf("wifi: setup AP '%s' at %s password %s\n", kSetupApSsid, wifi_.ip(),
                    wifi_.setupApPassword());
      dirty_ = true;
      return true;
    case CommandType::kWifiStation:
      if (wifi_.hasStoredCredentials()) {
        wifi_.startStation();
        Serial.println("wifi: connecting to the stored network");
      } else {
        Serial.println("wifi: no stored network; use 'wifi ap' to set one up");
      }
      dirty_ = true;
      return true;
    case CommandType::kWifiClear:
      wifi_.clearCredentials();
      Serial.println("wifi: credentials cleared, setup AP started");
      dirty_ = true;
      return true;
    case CommandType::kBleStatus:
      printBleStatus();
      return true;
    case CommandType::kBleHidOn:
      switchMode(StationMode::kBle);
      ble_.setHidMode(true);
      Serial.println("ble: hid media remote on");
      dirty_ = true;
      return true;
    case CommandType::kBleHidOff:
      ble_.setHidMode(false);
      Serial.println("ble: hid media remote off");
      dirty_ = true;
      return true;
    case CommandType::kTimerStatus:
      printTimerStatus();
      return true;
    case CommandType::kTimerSet:
      timer_.setMinutes(command.timerMinutes);
      Serial.printf("timer: %u min\n", static_cast<unsigned>(command.timerMinutes));
      dirty_ = true;
      return true;
    case CommandType::kTimerStart:
      timer_.start(millis());
      Serial.println("timer: started");
      dirty_ = true;
      return true;
    case CommandType::kTimerPause:
      timer_.pause(millis());
      Serial.println("timer: paused");
      dirty_ = true;
      return true;
    case CommandType::kTimerReset:
      timer_.reset();
      Serial.println("timer: reset");
      dirty_ = true;
      return true;
    case CommandType::kModeStatus:
      printNavigation();
      return true;
    case CommandType::kModeSelect:
      if (!controller_.selectModeByName(command.mode)) {
        Serial.printf("error: unknown mode '%s'\n", command.mode);
        return true;
      }
      if (controller_.screen() == StationScreen::kMenu) {
        leaveMode(controller_.mode());
      } else if (controller_.mode() != StationMode::kBle && ble_.hidMode()) {
        ble_.setHidMode(false);
      }
      confirmPressedInLogs_ = false;
      confirmLongFired_ = false;
      announceMode();
      dirty_ = true;
      return true;
    default:
      break;
  }
  return false;
}

}  // namespace fw

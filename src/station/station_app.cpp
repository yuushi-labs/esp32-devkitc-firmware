#include "station/station_app.h"

#include <cstdio>
#include <cstring>

namespace fw {
namespace {

constexpr uint32_t kBmpRetryIntervalMs = 10000;

void printLine(const char* text) {
  if (text != nullptr) {
    Serial.print(text);
  }
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
      scan_() {}

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

  lastSensorPollMs_ = millis() - kSensorPollIntervalMs;  // poll right away
  lastDhtPollMs_ = millis() - kDhtPollIntervalMs;
  lastBmpRetryMs_ = millis();
  lastDisplayMs_ = millis();

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

  const LogSample sample = makeLogSample(snapshot_, nowMs);
  if (recorder_.poll(sample, nowMs)) {
    Serial.printf("log: recorded uptime_ms=%lu temp=%s pressure=%s humidity=%s\n",
                  static_cast<unsigned long>(sample.uptimeMs),
                  sample.hasTemperature ? "yes" : "-",
                  sample.hasPressure ? "yes" : "-",
                  sample.hasHumidity ? "yes" : "-");
    dirty_ = true;
  }

  if (dirty_ || (nowMs - lastDisplayMs_) >= kDisplayRefreshMs) {
    updateView(nowMs);
    redraw();
    lastDisplayMs_ = nowMs;
  }
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
    // No new frame means no valid humidity, even if the sensor answered: keep
    // the last value from being shown as current.
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

void StationApp::updateView(uint32_t nowMs) {
  view_.version = FW_VERSION;
  view_.sensors = snapshot_;
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
}

void StationApp::redraw() {
  buildStationPage(controller_.page(), view_, &page_);
  drawPageLines(framebuffer_, page_);
  if (displayReady_) {
    if (!display_.flush()) {
      // A lost panel should not take the logger down with it.
      displayReady_ = false;
      Serial.println("oled: flush failed, continuing without the panel");
    }
  }
  dirty_ = false;
}

void StationApp::handleInputs(const InputEvent& event, uint32_t nowMs) {
  if (event.encoderDelta != 0) {
    for (int8_t step = 0; step < event.encoderDelta; ++step) {
      controller_.nextPage();
    }
    for (int8_t step = 0; step > event.encoderDelta; --step) {
      controller_.previousPage();
    }
    Serial.printf("input: encoder %+d -> page %s\n", static_cast<int>(event.encoderDelta),
                  stationPageName(controller_.page()));
    dirty_ = true;
  }

  if (event.press[static_cast<size_t>(InputButton::kPush)]) {
    recorder_.nextInterval();
    char interval[16];
    formatIntervalLabel(recorder_.intervalMs(), interval, sizeof(interval));
    Serial.printf("input: PSH -> interval %s\n", interval);
    dirty_ = true;
  }

  if (event.press[static_cast<size_t>(InputButton::kBack)]) {
    controller_.previousPage();
    Serial.printf("input: BAK -> page %s\n", stationPageName(controller_.page()));
    dirty_ = true;
  }

  if (event.press[static_cast<size_t>(InputButton::kConfirm)]) {
    confirmLongFired_ = false;
  }
  if (event.longPress[static_cast<size_t>(InputButton::kConfirm)]) {
    confirmLongFired_ = true;
    const bool enabled = recorder_.toggleEnabled();
    Serial.printf("input: CON long-press -> recording %s\n", enabled ? "on" : "off");
    dirty_ = true;
  }
  if (event.release[static_cast<size_t>(InputButton::kConfirm)] && !confirmLongFired_) {
    recordNow(nowMs, true);
  }
}

void StationApp::recordNow(uint32_t nowMs, bool manual) {
  const LogSample sample = makeLogSample(snapshot_, nowMs);
  if (!recorder_.record(sample, nowMs)) {
    Serial.println("log: write failed");
    return;
  }
  Serial.printf("log: %s record #%lu\n", manual ? "manual" : "auto",
                static_cast<unsigned long>(recorder_.recordCount()));
  dirty_ = true;
}

void StationApp::printSensorReport() {
  updateView(millis());
  char buffer[384];
  formatStationSensorReport(view_, buffer, sizeof(buffer));
  printLine(buffer);
}

void StationApp::printLogStatus() {
  updateView(millis());
  char buffer[256];
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

void StationApp::announcePage() {
  Serial.printf("page: %s (%u/%u)\n", stationPageName(controller_.page()),
                static_cast<unsigned>(static_cast<size_t>(controller_.page()) + 1),
                static_cast<unsigned>(kStationPageCount));
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
      if (recorder_.clear(millis())) {
        Serial.println("log: cleared, header rewritten");
      } else {
        Serial.println("log: clear failed");
      }
      dirty_ = true;
      return true;
    case CommandType::kPageNext:
      controller_.nextPage();
      announcePage();
      dirty_ = true;
      return true;
    case CommandType::kPagePrev:
      controller_.previousPage();
      announcePage();
      dirty_ = true;
      return true;
    case CommandType::kPageSelect:
      if (controller_.selectPage(command.page)) {
        announcePage();
      } else {
        Serial.println("error: page must be 1..4");
      }
      dirty_ = true;
      return true;
    default:
      break;
  }
  return false;
}

}  // namespace fw

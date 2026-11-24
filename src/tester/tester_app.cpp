#include "tester/tester_app.h"

#include <cstdio>

namespace fw {
namespace {

constexpr uint32_t kRescanIntervalMs = 5000;
constexpr uint32_t kBmpRetryIntervalMs = 5000;

}  // namespace

TesterApp::TesterApp()
    : bus_(),
      framebuffer_(),
      display_(bus_, kOledPrimaryAddress, framebuffer_),
      bmp280_(bus_, kBmp280PrimaryAddress),
      dht_(),
      inputs_(),
      sensors_(),
      inputsState_(),
      lines_() {}

void TesterApp::begin() {
  startedMs_ = millis();

  bus_.begin(kI2cSdaPin, kI2cSclPin, kI2cClockHz);
  dht_.begin(kDhtPin);
  inputs_.begin();

  oledAddress_ = Ssd1306Display::detectAddress(bus_);
  displayReady_ = oledAddress_ != 0 && display_.begin(oledAddress_);
  Serial.printf("oled: %s\n",
                displayReady_ ? "SSD1306 ready" : "not found (serial only)");

  scanBus();

  const uint8_t bmpAddress = findBmp280Address(bus_);
  const Bmp280Status status =
      bmp280_.begin(bmpAddress != 0 ? bmpAddress : kBmp280PrimaryAddress);
  sensors_.bmp280Status = status;
  sensors_.bmp280Address = bmp280_.address();
  sensors_.bmp280Chip = bmp280_.chipName();
  Serial.printf("bmp280: %s%s\n", bmp280StatusName(status),
                status == Bmp280Status::kOk ? "" : " (continuing without it)");

  pollSensors(millis());
  pollDht(millis());
  refreshInputs();

  lastDisplayMs_ = millis();
  lastEchoMs_ = millis();
  redraw();

  Serial.println("tester ready: everything you move is echoed here");
  printSensors();
}

void TesterApp::loop() {
  const uint32_t nowMs = millis();

  const InputEvent event = inputs_.poll(nowMs);
  if (event.any()) {
    handleInputs(event);
  }

  if ((nowMs - lastScanMs_) >= kRescanIntervalMs) {
    scanBus();
  }
  pollSensors(nowMs);
  pollDht(nowMs);

  if (dirty_ || (nowMs - lastDisplayMs_) >= kDisplayRefreshMs) {
    refreshInputs();
    redraw();
    lastDisplayMs_ = nowMs;
  }
}

void TesterApp::scanBus() {
  sensors_.scan = scanI2cBus(bus_);
  sensors_.oledPresent = sensors_.scan.containsKind(I2cDeviceKind::kSsd1306);
  sensors_.oledAddress = 0;
  for (size_t index = 0; index < sensors_.scan.count; ++index) {
    if (sensors_.scan.entries[index].kind == I2cDeviceKind::kSsd1306) {
      sensors_.oledAddress = sensors_.scan.entries[index].address;
      break;
    }
  }
  lastScanMs_ = millis();
  dirty_ = true;
}

void TesterApp::pollSensors(uint32_t nowMs) {
  if ((nowMs - lastSensorPollMs_) < kSensorPollIntervalMs) {
    return;
  }
  lastSensorPollMs_ = nowMs;

  if (!bmp280_.present() && (nowMs - startedMs_) >= kBmpRetryIntervalMs) {
    const uint8_t address = findBmp280Address(bus_);
    if (address != 0) {
      sensors_.bmp280Status = bmp280_.begin(address);
      sensors_.bmp280Address = bmp280_.address();
      sensors_.bmp280Chip = bmp280_.chipName();
      dirty_ = true;
    }
  }

  if (!bmp280_.present()) {
    sensors_.bmp280ReadingValid = false;
    return;
  }

  Bmp280Reading reading;
  if (bmp280_.read(&reading)) {
    sensors_.bmp280ReadingValid = true;
    sensors_.temperatureC = reading.temperatureC;
    sensors_.pressureHpa = reading.pressureHpa;
  } else {
    sensors_.bmp280ReadingValid = false;
  }
  dirty_ = true;
}

void TesterApp::pollDht(uint32_t nowMs) {
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
    sensors_.dhtPresent = dht_.responded();
    sensors_.dhtReadingValid = false;
    if (!sensors_.dhtPresent) {
      sensors_.dhtType = DhtType::kUnknown;
    }
    dirty_ = true;
    return;
  }

  const DhtFrame frame = decodeDhtFrameAuto(pulses, bitCount);
  sensors_.dhtPresent = true;
  sensors_.dhtType = frame.type;
  sensors_.dhtReadingValid = frame.reading.valid;
  if (frame.reading.valid) {
    sensors_.humidityPct = frame.reading.humidityPct;
    sensors_.dhtTemperatureC = frame.reading.temperatureC;
  }
  dirty_ = true;
}

void TesterApp::refreshInputs() {
  inputsState_.encoderPosition = inputs_.encoderPosition();
  inputsState_.pushDown = inputs_.down(InputButton::kPush);
  inputsState_.pushPresses = inputs_.pressCount(InputButton::kPush);
  inputsState_.backDown = inputs_.down(InputButton::kBack);
  inputsState_.backPresses = inputs_.pressCount(InputButton::kBack);
  inputsState_.confirmDown = inputs_.down(InputButton::kConfirm);
  inputsState_.confirmPresses = inputs_.pressCount(InputButton::kConfirm);
}

void TesterApp::handleInputs(const InputEvent& event) {
  refreshInputs();

  if (event.encoderDelta != 0) {
    Serial.printf("input: encoder %+d position=%ld\n",
                  static_cast<int>(event.encoderDelta),
                  static_cast<long>(inputsState_.encoderPosition));
  }

  for (size_t index = 0; index < kInputButtonCount; ++index) {
    const InputButton button = static_cast<InputButton>(index);
    if (event.press[index]) {
      Serial.printf("input: %s press count=%lu\n", GpioInputs::buttonName(button),
                    static_cast<unsigned long>(inputs_.pressCount(button)));
    }
    if (event.longPress[index]) {
      Serial.printf("input: %s long-press\n", GpioInputs::buttonName(button));
    }
    if (event.release[index]) {
      Serial.printf("input: %s release\n", GpioInputs::buttonName(button));
    }
  }

  // Page changes are part of operating the tester, not of the input under test.
  if (event.press[static_cast<size_t>(InputButton::kBack)]) {
    page_ = nextTesterPage(page_);
    Serial.printf("page: %s\n", testerPageName(page_));
  }
  if (event.press[static_cast<size_t>(InputButton::kConfirm)]) {
    scanBus();
    printScan();
  }
  dirty_ = true;
}

void TesterApp::redraw() {
  buildTesterPage(page_, sensors_, inputsState_, &lines_);
  drawPageLines(framebuffer_, lines_);
  if (displayReady_ && !display_.flush()) {
    displayReady_ = false;
    Serial.println("oled: flush failed, continuing without the panel");
  }
  dirty_ = false;
}

void TesterApp::printScan() {
  char buffer[384];
  formatI2cScan(sensors_.scan, buffer, sizeof(buffer));
  Serial.print(buffer);
}

void TesterApp::printSensors() {
  char buffer[512];
  formatTesterSensorReport(sensors_, buffer, sizeof(buffer));
  Serial.print(buffer);
}

void TesterApp::printInputs() {
  refreshInputs();
  char buffer[256];
  formatTesterInputReport(inputsState_, buffer, sizeof(buffer));
  Serial.print(buffer);
}

bool TesterApp::handleCommand(const Command& command) {
  switch (command.type) {
    case CommandType::kScan:
      scanBus();
      printScan();
      return true;
    case CommandType::kSensors:
      pollSensors(millis());
      printSensors();
      return true;
    case CommandType::kInputs:
      printInputs();
      return true;
    default:
      break;
  }
  return false;
}

}  // namespace fw

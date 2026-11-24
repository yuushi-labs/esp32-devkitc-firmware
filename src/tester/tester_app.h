// Tester firmware application: prove that every part and every input works.
#pragma once

#include <Arduino.h>

#include <cstdint>

#include "app_config.h"
#include "command_handler.h"
#include "display/framebuffer.h"
#include "display/page_lines.h"
#include "display/ssd1306.h"
#include "hal/dht_gpio.h"
#include "hal/gpio_inputs.h"
#include "hal/wire_i2c.h"
#include "sensors/bmp280.h"
#include "tester/tester_model.h"

namespace fw {

class TesterApp {
 public:
  TesterApp();

  void begin();
  void loop();
  bool handleCommand(const Command& command);

 private:
  void scanBus();
  void pollSensors(uint32_t nowMs);
  void pollDht(uint32_t nowMs);
  void handleInputs(const InputEvent& event);
  void refreshInputs();
  void redraw();
  void printScan();
  void printSensors();
  void printInputs();

  WireI2cTransport bus_;
  Framebuffer framebuffer_;
  Ssd1306Display display_;
  Bmp280 bmp280_;
  DhtGpioReader dht_;
  GpioInputs inputs_;
  TesterSensors sensors_;
  TesterInputs inputsState_;
  TesterPage page_ = TesterPage::kInputs;
  PageLines lines_;

  uint8_t oledAddress_ = 0;
  bool displayReady_ = false;
  bool dirty_ = true;

  uint32_t startedMs_ = 0;
  uint32_t lastScanMs_ = 0;
  uint32_t lastSensorPollMs_ = 0;
  uint32_t lastDhtPollMs_ = 0;
  uint32_t lastDisplayMs_ = 0;
  uint32_t lastEchoMs_ = 0;
};

}  // namespace fw

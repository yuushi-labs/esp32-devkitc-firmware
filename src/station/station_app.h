// Station firmware application: sensors, panel pages, buttons and the CSV log.
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
#include "hal/littlefs_storage.h"
#include "hal/wire_i2c.h"
#include "i2c_scan.h"
#include "logging/log_record.h"
#include "sensors/bmp280.h"
#include "station/station_model.h"

namespace fw {

class StationApp {
 public:
  StationApp();

  void begin();
  void loop();
  // Handles the station specific commands; false when the command is unknown
  // here (common ones are handled by main.cpp).
  bool handleCommand(const Command& command);

  const SensorSnapshot& sensors() const { return snapshot_; }
  bool loggingEnabled() const { return recorder_.enabled(); }

 private:
  void pollSensors(uint32_t nowMs);
  void pollDht(uint32_t nowMs);
  void updateView(uint32_t nowMs);
  void redraw();
  void handleInputs(const InputEvent& event, uint32_t nowMs);
  void recordNow(uint32_t nowMs, bool manual);
  void printSensorReport();
  void printLogStatus();
  void printScan();
  void announcePage();

  WireI2cTransport bus_;
  Framebuffer framebuffer_;
  Ssd1306Display display_;
  Bmp280 bmp280_;
  DhtGpioReader dht_;
  LittleFsLogStorage storage_;
  LogRecorder recorder_;
  GpioInputs inputs_;
  StationController controller_;
  PressureTrendTracker trend_;
  SensorSnapshot snapshot_;
  StationView view_;
  PageLines page_;
  I2cScanResult scan_;

  uint8_t oledAddress_ = 0;
  bool displayReady_ = false;
  bool dirty_ = true;
  bool confirmLongFired_ = false;

  uint32_t lastSensorPollMs_ = 0;
  uint32_t lastBmpRetryMs_ = 0;
  uint32_t lastDhtPollMs_ = 0;
  uint32_t lastDisplayMs_ = 0;
  uint32_t startedMs_ = 0;
};

}  // namespace fw

// Station firmware application: sensors, six UI modes, buttons, CSV log,
// Wi-Fi/web dashboard, BLE and the physical timer.
#pragma once

#include <Arduino.h>

#include <cstdint>

#include "app_config.h"
#include "command_handler.h"
#include "display/framebuffer.h"
#include "display/page_lines.h"
#include "display/ssd1306.h"
#include "graphics/mini_graph.h"
#include "graphics/qr_code.h"
#include "net/wifi_qr.h"
#include "hal/ble_service.h"
#include "hal/dht_gpio.h"
#include "hal/gpio_inputs.h"
#include "hal/littlefs_storage.h"
#include "hal/web_server.h"
#include "hal/wifi_manager.h"
#include "hal/wire_i2c.h"
#include "i2c_scan.h"
#include "logging/log_record.h"
#include "pet/pet_model.h"
#include "pet/pet_screen.h"
#include "sensors/bmp280.h"
#include "station/station_model.h"
#include "station/timer_model.h"

namespace fw {

class StationApp : public WebDashboardHost {
 public:
  StationApp();

  void begin();
  void loop();
  // Handles the station specific commands; false when the command is unknown
  // here (common ones are handled by main.cpp).
  bool handleCommand(const Command& command);

  const SensorSnapshot& sensors() const { return snapshot_; }
  bool loggingEnabled() const { return recorder_.enabled(); }
  // Drives the fast LED blink for sensor alerts and the timer alarm.
  bool alertActive() const;

  // WebDashboardHost
  size_t buildStatusJson(char* out, size_t outSize) override;
  size_t buildOledJson(char* out, size_t outSize) override;
  bool handleOledCommand(const OledCommand& command) override;
  bool clearLog() override;
  void showQr() override;
  void saveWifiCredentials(const WifiCredentials& credentials) override;

 private:
  void pollSensors(uint32_t nowMs);
  void updatePet(uint32_t nowMs);
  // Hardware display on/off; on restores the current picture. False when the
  // panel refused the command.
  bool setOledPower(bool on);
  bool oledPower() const { return oledPower_; }
  // Replays the hourly chime for a demo from the panel, serial or the web.
  void testClockChime();
  void pollDht(uint32_t nowMs);
  void sampleGraphs(uint32_t nowMs);
  void updateView(uint32_t nowMs);
  void redraw();
  void handleInputs(const InputEvent& event, uint32_t nowMs);
  void handleModeInputs(const InputEvent& event, uint32_t nowMs);
  void enterMode(StationMode mode);
  // Leaves the current mode (HID remote off, QR hidden) and opens the new one.
  void switchMode(StationMode mode);
  void leaveMode(StationMode mode);
  void recordNow(uint32_t nowMs, bool manual);
  void fillTimestamp(LogSample* sample) const;
  void updateQrPayload();
  AlertState currentAlerts() const;

  void printSensorReport();
  void printLogStatus();
  void printScan();
  void printWifiStatus();
  void printBleStatus();
  void printTimerStatus();
  void printNavigation();
  void printOledStatus();
  void printPetStatus();
  void announceMode();

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

  WifiManager wifi_;
  StationWebServer web_;
  BleService ble_;
  CountdownTimer timer_;
  GraphBuffer temperatureGraph_{kGraphCapacity};
  GraphBuffer pressureGraph_{kGraphCapacity};
  QrCode qr_;
  // What the OLED QR encodes: the dashboard address, or a Wi-Fi join payload
  // while the setup AP is up.
  char qrPayload_[96] = {0};
  VirtualPet pet_;

  uint8_t oledAddress_ = 0;
  bool displayReady_ = false;
  bool oledPower_ = true;
  // Buttons that were already down when the panel woke: their long press must
  // not reach a screen until the finger is lifted.
  bool wakeSuppressedLongPress_[kInputButtonCount] = {false, false, false};
  bool dirty_ = true;
  bool confirmLongFired_ = false;
  bool confirmPressedInLogs_ = false;
  bool qrVisible_ = false;
  // Last frame actually sent to the panel, so identical frames cost no I2C time.
  uint32_t lastFrameHash_ = 0;
  bool frameSent_ = false;
  bool alarmActive_ = false;

  uint32_t lastSensorPollMs_ = 0;
  uint32_t lastBmpRetryMs_ = 0;
  uint32_t lastDhtPollMs_ = 0;
  uint32_t lastDisplayMs_ = 0;
  uint32_t lastGraphSampleMs_ = 0;
  uint32_t qrUntilMs_ = 0;
  uint32_t alarmUntilMs_ = 0;
  uint32_t startedMs_ = 0;
};

}  // namespace fw

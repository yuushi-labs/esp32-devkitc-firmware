// Station UI model: home menu, six modes, page contents and navigation.
//
// Everything here is pure C++: the screen a button press moves to, the pressure
// trend, the text of every OLED page and the graph/QR screens. The Arduino app
// only feeds a StationView and draws the returned PageLines or Framebuffer.
#pragma once

#include <cstddef>
#include <cstdint>

#include "display/framebuffer.h"
#include "display/page_lines.h"
#include "net/wifi_credentials.h"
#include "pet/pet_model.h"
#include "sensors/sensor_snapshot.h"

namespace fw {

class GraphBuffer;
class QrCode;

enum class StationMode {
  // The dot-art cat that greets the device on power-up; the other modes are
  // one BAK press (or one menu step) away.
  kPet = 0,
  kEnvironment,
  kLogs,
  kWifi,
  kBle,
  kTimer,
  kSystem,
};

constexpr size_t kStationModeCount = 7;

const char* stationModeName(StationMode mode);
StationMode nextStationMode(StationMode mode);
StationMode previousStationMode(StationMode mode);
StationMode stationModeFromIndex(size_t index);
// Accepts "pet", "cat", "virtualpet", "env", "environment", "logs", "log",
// "wifi", "ble", "bluetooth", "timer", "system", "sys" and "1".."7".
bool stationModeFromName(const char* name, StationMode* out);

enum class EnvView { kSummary = 0, kGraph, kDetail };

constexpr size_t kEnvViewCount = 3;

const char* envViewName(EnvView view);
EnvView nextEnvView(EnvView view);
EnvView previousEnvView(EnvView view);

enum class StationScreen { kMenu = 0, kMode };
const char* stationScreenName(StationScreen screen);

struct StationView {
  const char* version = "0.0.0";
  SensorSnapshot sensors;
  // Current minus reference pressure over the trend window; drives the alert.
  float pressureDeltaHpa = 0.0f;

  bool loggingEnabled = true;
  bool logStorageReady = false;
  uint32_t logRecordCount = 0;
  uint32_t logIntervalMs = 60000;
  uint32_t logIntervalRemainingMs = 0;
  uint32_t logFileBytes = 0;

  uint8_t oledAddress = 0;
  char i2cAddresses[48] = "none";
  uint32_t uptimeMs = 0;
  uint32_t freeHeapBytes = 0;
  uint8_t dhtPin = 0;

  // Wi-Fi / NTP
  WifiMode wifiMode = WifiMode::kOff;
  bool wifiConnected = false;
  bool ntpSynced = false;
  char wifiSsid[kWifiSsidMaxLength + 1] = {0};
  char ip[16] = "0.0.0.0";
  int8_t rssi = 0;
  char clock[6] = "--:--";
  // WPA2 key of the setup AP, generated per start and shown on the panel.
  char apPassword[9] = {0};

  // BLE
  bool bleEnabled = false;
  bool bleHidMode = false;
  bool bleConnected = false;

  // Timer
  char timerRemaining[8] = "--:--";
  bool timerRunning = false;
  uint8_t timerProgress = 0;
  bool timerPomodoro = false;
  const char* timerPhase = "idle";

  // Navigation and sub screens
  EnvView envView = EnvView::kSummary;
  StationMode menuSelection = StationMode::kEnvironment;
  const GraphBuffer* temperatureGraph = nullptr;
  const GraphBuffer* pressureGraph = nullptr;
  bool qrVisible = false;
  const QrCode* qrCode = nullptr;
  // Filled while the pet screen is up; sees the whole animation state.
  const PetScene* pet = nullptr;
};

// Home menu with all modes and the cursor on `view.menuSelection`.
void buildStationMenu(const StationView& view, PageLines* out);

// Renders one mode page. The environment graph screen is pixel based, so the
// app calls buildStationGraphScreen() for it instead.
void buildStationPage(StationMode mode, const StationView& view, PageLines* out);

// Environment mini graphs (temperature and pressure) over the last samples.
void buildStationGraphScreen(const StationView& view, Framebuffer* out);

// Multi line serial reports.
size_t formatStationSensorReport(const StationView& view, char* out, size_t outSize);
size_t formatStationLogStatus(const StationView& view, char* out, size_t outSize);
size_t formatStationBleReport(const StationView& view, char* out, size_t outSize);

// Rising / falling detection with a fixed window, used on the detail page.
class PressureTrendTracker {
 public:
  void update(float pressureHpa, uint32_t nowMs);
  int8_t trend() const { return trend_; }
  // Change since the reference sample (0 when no reference yet).
  float deltaHpa() const { return deltaHpa_; }
  void reset();

 private:
  bool hasReference_ = false;
  float referenceHpa_ = 0.0f;
  uint32_t referenceMs_ = 0;
  int8_t trend_ = 0;
  float deltaHpa_ = 0.0f;
};

// Menu / mode navigation, independent of the hardware.
class StationController {
 public:
  StationScreen screen() const { return screen_; }
  StationMode mode() const { return mode_; }
  StationMode menuSelection() const { return menuSelection_; }
  EnvView envView() const { return envView_; }

  void menuMove(int delta);
  bool enter();
  bool back();
  // Jumps to the home menu with the current mode selected (the BAK press on
  // the pet screen). Returns false when the menu is already open.
  bool openMenu();
  bool selectMode(StationMode mode);
  bool selectModeIndex(size_t oneBased);
  bool selectModeByName(const char* name);
  void cycleEnvView(int delta);
  // Jumps straight to a sub view of the environment page (web remote).
  void setEnvView(EnvView view) { envView_ = view; }

 private:
  // The station boots into the pet, not the menu.
  StationScreen screen_ = StationScreen::kMode;
  StationMode mode_ = StationMode::kPet;
  StationMode menuSelection_ = StationMode::kPet;
  EnvView envView_ = EnvView::kSummary;
};

// "screen=menu mode=environment selection=environment" for the `mode` command.
size_t formatStationNavigation(const StationController& controller, char* out,
                               size_t outSize);

}  // namespace fw

// Station pages: state machine and page contents.
//
// Everything here is pure C++: the page a button press moves to, the pressure
// trend and the exact text of every OLED page. The Arduino app only feeds a
// StationView and draws the returned PageLines.
#pragma once

#include <cstddef>
#include <cstdint>

#include "display/page_lines.h"
#include "sensors/sensor_snapshot.h"

namespace fw {

enum class StationPage {
  kSummary = 0,
  kSensorDetail,
  kLogs,
  kSystem,
};

constexpr size_t kStationPageCount = 4;

const char* stationPageName(StationPage page);
StationPage nextStationPage(StationPage page);
StationPage previousStationPage(StationPage page);
StationPage stationPageFromIndex(size_t index);

struct StationView {
  const char* version = "0.0.0";
  SensorSnapshot sensors;

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
};

// Renders one station page into at most 8 lines.
void buildStationPage(StationPage page, const StationView& view, PageLines* out);

// Multi line serial reports for the `sensors` and `log` commands.
size_t formatStationSensorReport(const StationView& view, char* out, size_t outSize);
size_t formatStationLogStatus(const StationView& view, char* out, size_t outSize);

// Rising / falling detection with a fixed window, used on the detail page.
class PressureTrendTracker {
 public:
  void update(float pressureHpa, uint32_t nowMs);
  int8_t trend() const { return trend_; }
  void reset();

 private:
  bool hasReference_ = false;
  float referenceHpa_ = 0.0f;
  uint32_t referenceMs_ = 0;
  int8_t trend_ = 0;
};

// Page navigation and the log interval selection, independent of the hardware.
class StationController {
 public:
  StationPage page() const { return page_; }
  StationPage nextPage();
  StationPage previousPage();
  bool selectPage(size_t oneBased);

 private:
  StationPage page_ = StationPage::kSummary;
};

}  // namespace fw

// Web dashboard payloads: the JSON API, the offline dashboard page and the
// captive portal setup page.
//
// The HTML is a plain string with no CDN dependency, so the page works on a
// LAN without internet access. Everything here is pure C++ and unit tested.
#pragma once

#include <cstddef>
#include <cstdint>

#include "logging/log_record.h"
#include "net/wifi_credentials.h"
#include "sensors/sensor_snapshot.h"
#include "station/alerts.h"

namespace fw {

struct DashboardState {
  const char* version = "0.0.0";
  WifiMode wifiMode = WifiMode::kOff;
  bool wifiConnected = false;
  char ssid[kWifiSsidMaxLength + 1] = {0};
  char ip[16] = "0.0.0.0";
  int8_t rssi = 0;
  bool ntpSynced = false;
  char timestamp[20] = {0};
  // Where the saved network lives (littlefs / nvs / none).
  const char* wifiStorage = "none";

  bool logStorageReady = false;
  bool loggingEnabled = false;
  uint32_t logRecordCount = 0;
  uint32_t logIntervalMs = 0;
  uint32_t logFileBytes = 0;

  bool bleEnabled = false;
  bool bleHidMode = false;
  bool bleConnected = false;

  bool timerRunning = false;
  char timerRemaining[8] = "--:--";
  const char* timerPhase = "idle";

  const char* mode = "menu";
  uint32_t uptimeMs = 0;
  uint32_t freeHeapBytes = 0;

  SensorSnapshot sensors;
  AlertState alerts;
};

// GET /api/status
size_t formatStatusJson(const DashboardState& state, char* out, size_t outSize);

// GET /api/logs - JSON array of the supplied samples.
size_t formatLogsJson(const LogSample* samples, size_t count, char* out,
                      size_t outSize);

// Parses CSV text (as stored in /log.csv) and emits the newest `maxRows` data
// rows as a JSON array. The header and blank lines are skipped.
size_t formatLogsJsonFromCsv(const char* csv, size_t maxRows, char* out,
                             size_t outSize);

// GET /
const char* dashboardHtml();

// GET / (captive portal): scanned SSIDs as one-tap chips and the setup form.
// `token` is echoed back by POST /save so the server can tell its own form apart
// from a request another client of the setup AP invented. `scanning` keeps the
// empty list refreshing until the background scan reports back, without wiping
// what the user already typed.
size_t formatSetupPage(const char* const* ssids, size_t count, const char* message,
                       const char* token, bool scanning, char* out, size_t outSize);

// Shown when /setup is opened while the station is on a saved network: the
// credentials can only be changed from the setup access point.
size_t formatSetupLockedPage(char* out, size_t outSize);

// Shown after POST /save.
size_t formatSavedPage(const char* ssid, char* out, size_t outSize);

}  // namespace fw

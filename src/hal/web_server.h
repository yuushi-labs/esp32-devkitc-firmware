// HTTP server for the dashboard, the JSON API and the captive portal.
#pragma once

#include <WebServer.h>

#include <cstddef>
#include <cstdint>

#include "app_config.h"
#include "net/web_dashboard.h"
#include "net/wifi_credentials.h"

namespace fw {

class WifiManager;

// Implemented by the station app so the web server stays free of the sensor
// and log internals.
class WebDashboardHost {
 public:
  virtual ~WebDashboardHost() = default;
  virtual size_t buildStatusJson(char* out, size_t outSize) = 0;
  virtual bool clearLog() = 0;
  virtual void showQr() = 0;
  virtual void saveWifiCredentials(const WifiCredentials& credentials) = 0;
};

class StationWebServer {
 public:
  void begin(WifiManager& wifi, WebDashboardHost& host);
  void loop(uint32_t nowMs);

  // Short lived token that ties a POST /save to a form this server produced.
  // Only requests from the setup AP can save credentials anyway; the token
  // keeps other clients of the same AP from posting blindly.
  const char* setupToken() const { return setupToken_; }

 private:
  void handleRoot();
  void handleStatus();
  void handleLogs();
  void handleDownload();
  void handleClear();
  void handleQr();
  void handleSetup();
  void handleSave();
  void handleNotFound();

  bool setupRequestAuthorised() const;
  bool setupAccessPointActive() const;
  void issueSetupToken(uint32_t nowMs);
  String buildSetupPage(const char* message);
  void sendSavedPage(const char* ssid);

  WebServer server_{kWebServerPort};
  WifiManager* wifi_ = nullptr;
  WebDashboardHost* host_ = nullptr;
  bool pendingSave_ = false;
  uint32_t saveAtMs_ = 0;
  WifiCredentials pendingCredentials_;
  char setupToken_[17] = {0};
  uint32_t setupTokenIssuedMs_ = 0;
};

}  // namespace fw

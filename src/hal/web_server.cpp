#include "hal/web_server.h"

#include <LittleFS.h>
#include <esp_random.h>

#include <cstring>
#include <memory>
#include <new>
#include <vector>

#include "hal/wifi_manager.h"
#include "logging/log_record.h"

namespace fw {
namespace {

constexpr size_t kLogJsonRows = 120;
constexpr size_t kLogJsonBufferSize = 20000;
constexpr size_t kSetupBufferSize = 12000;
constexpr size_t kStatusBufferSize = 1536;
constexpr size_t kMaxScannedSsids = 12;
// Give the browser time to render the "saved" page before the AP goes down.
constexpr uint32_t kSaveDelayMs = 1500;

// Eight random bytes as hex, the token format the setup form carries.
void generateSetupToken(char* out, size_t outSize) {
  std::snprintf(out, outSize, "%08lx%08lx", static_cast<unsigned long>(esp_random()),
                static_cast<unsigned long>(esp_random()));
}

}  // namespace

void StationWebServer::begin(WifiManager& wifi, WebDashboardHost& host) {
  wifi_ = &wifi;
  host_ = &host;

  server_.on("/", HTTP_GET, [this]() { handleRoot(); });
  server_.on("/api/status", HTTP_GET, [this]() { handleStatus(); });
  server_.on("/api/logs", HTTP_GET, [this]() { handleLogs(); });
  server_.on("/download", HTTP_GET, [this]() { handleDownload(); });
  server_.on("/api/log/clear", HTTP_POST, [this]() { handleClear(); });
  server_.on("/api/qr", HTTP_POST, [this]() { handleQr(); });
  server_.on("/setup", HTTP_GET, [this]() { handleSetup(); });
  server_.on("/save", HTTP_POST, [this]() { handleSave(); });
  // Browsers ask for these on their own. Answering here keeps them out of the
  // captive portal redirect (and so out of handleSetup) on both GET and HEAD.
  server_.on("/favicon.ico", HTTP_GET, [this]() { handleNoContent(); });
  server_.on("/favicon.ico", HTTP_HEAD, [this]() { handleNoContent(); });
  server_.on("/robots.txt", HTTP_GET, [this]() { handleNoContent(); });
  server_.on("/robots.txt", HTTP_HEAD, [this]() { handleNoContent(); });
  server_.onNotFound([this]() { handleNotFound(); });
  server_.begin();
  Serial.printf("web: listening on port %u\n", static_cast<unsigned>(kWebServerPort));
}

void StationWebServer::loop(uint32_t nowMs) {
  server_.handleClient();
  if (setupSession_.active() && !setupAccessPointActive()) {
    // The setup AP is down, so the form it served can no longer be posted.
    setupSession_.reset();
  }
  if (pendingSave_ && nowMs >= saveAtMs_) {
    pendingSave_ = false;
    if (host_ != nullptr) {
      host_->saveWifiCredentials(pendingCredentials_);
    }
  }
}

void StationWebServer::handleRoot() {
  if (wifi_ != nullptr && wifi_->apActive()) {
    handleSetup();
    return;
  }
  server_.send(200, "text/html", dashboardHtml());
}

void StationWebServer::handleStatus() {
  if (host_ == nullptr) {
    server_.send(503, "application/json", "{\"error\":\"starting\"}");
    return;
  }
  std::unique_ptr<char[]> buffer(new (std::nothrow) char[kStatusBufferSize]);
  if (!buffer) {
    server_.send(500, "application/json", "{\"error\":\"out of memory\"}");
    return;
  }
  const size_t needed = host_->buildStatusJson(buffer.get(), kStatusBufferSize);
  if (needed >= kStatusBufferSize) {
    server_.send(500, "application/json", "{\"error\":\"status too large\"}");
    return;
  }
  server_.send(200, "application/json", buffer.get());
}

void StationWebServer::handleLogs() {
  if (!LittleFS.exists(kLogPath)) {
    server_.send(200, "application/json", "[]");
    return;
  }
  File file = LittleFS.open(kLogPath, "r");
  if (!file) {
    server_.send(500, "application/json", "{\"error\":\"cannot open log\"}");
    return;
  }

  std::vector<String> lines;
  lines.reserve(kLogJsonRows);
  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) {
      continue;
    }
    lines.push_back(line);
    if (lines.size() > kLogJsonRows) {
      lines.erase(lines.begin());
    }
  }
  file.close();

  String csv;
  csv.reserve(lines.size() * 48);
  for (const String& line : lines) {
    csv += line;
    csv += '\n';
  }

  std::unique_ptr<char[]> buffer(new (std::nothrow) char[kLogJsonBufferSize]);
  if (!buffer) {
    server_.send(500, "application/json", "{\"error\":\"out of memory\"}");
    return;
  }
  const size_t needed =
      formatLogsJsonFromCsv(csv.c_str(), kLogJsonRows, buffer.get(), kLogJsonBufferSize);
  if (needed >= kLogJsonBufferSize) {
    server_.send(500, "application/json", "{\"error\":\"log too large\"}");
    return;
  }
  server_.send(200, "application/json", buffer.get());
}

void StationWebServer::handleDownload() {
  if (!LittleFS.exists(kLogPath)) {
    server_.send(404, "text/plain", "no log yet");
    return;
  }
  File file = LittleFS.open(kLogPath, "r");
  if (!file) {
    server_.send(500, "text/plain", "cannot open log");
    return;
  }
  server_.sendHeader("Content-Disposition", "attachment; filename=\"log.csv\"");
  server_.streamFile(file, "text/csv");
  file.close();
}

void StationWebServer::handleClear() {
  const bool ok = host_ != nullptr && host_->clearLog();
  server_.send(ok ? 200 : 500, "application/json",
               ok ? "{\"ok\":true}" : "{\"error\":\"clear failed\"}");
}

void StationWebServer::handleQr() {
  if (host_ != nullptr) {
    host_->showQr();
  }
  server_.send(200, "application/json", "{\"ok\":true}");
}

void StationWebServer::ensureSetupToken(uint32_t nowMs) {
  // The token is reused while it is valid: a second page load (the browser
  // asking for /favicon.ico, a captive portal probe, the reload while the scan
  // is still running) must not invalidate the form the user is filling in.
  setupSession_.token(nowMs, kSetupSessionMs, generateSetupToken);
}

bool StationWebServer::setupAccessPointActive() const {
  return wifi_ != nullptr && wifi_->apActive();
}

bool StationWebServer::setupRequestAuthorised() {
  if (!setupAccessPointActive()) {
    // Changing the saved network needs someone at the device (or on the setup
    // AP); a client of the station's own LAN cannot rewrite it.
    return false;
  }
  if (!server_.hasArg("token")) {
    return false;
  }
  return setupSession_.authorised(server_.arg("token").c_str(), millis(), kSetupSessionMs);
}

String StationWebServer::buildSetupPage(const char* message) {
  static char ssids[kMaxScannedSsids][kWifiSsidMaxLength + 1];
  char* pointers[kMaxScannedSsids] = {nullptr};
  size_t count = 0;
  if (wifi_ != nullptr) {
    // The scan runs in the background so the page is answered immediately; the
    // page reloads itself while the list is still empty.
    wifi_->requestScan(millis());
    count = wifi_->cachedNetworks(ssids, kMaxScannedSsids);
  }
  for (size_t index = 0; index < count; ++index) {
    pointers[index] = ssids[index];
  }

  std::unique_ptr<char[]> buffer(new (std::nothrow) char[kSetupBufferSize]);
  if (!buffer) {
    return String("<html><body>out of memory</body></html>");
  }
  const size_t needed = formatSetupPage(pointers, count, message, setupSession_.current(),
                                        wifi_ != nullptr && wifi_->scanInProgress(),
                                        buffer.get(), kSetupBufferSize);
  if (needed >= kSetupBufferSize) {
    return String("<html><body>setup page too large</body></html>");
  }
  return String(buffer.get());
}

void StationWebServer::handleSetup() {
  if (!setupAccessPointActive()) {
    std::unique_ptr<char[]> buffer(new (std::nothrow) char[2048]);
    if (!buffer) {
      server_.send(403, "text/plain", "connect to the setup access point first");
      return;
    }
    const size_t needed = formatSetupLockedPage(buffer.get(), 2048);
    if (needed >= 2048) {
      server_.send(403, "text/plain", "connect to the setup access point first");
      return;
    }
    server_.send(403, "text/html", buffer.get());
    return;
  }
  // Serving the form is what starts the setup session; only a POST carrying
  // this token can save, and only for the next ten minutes. A page load while
  // the token is still valid keeps the token it already handed out.
  ensureSetupToken(millis());
  server_.send(200, "text/html", buildSetupPage(""));
}

void StationWebServer::handleSave() {
  if (!setupRequestAuthorised()) {
    // Anything that did not come from the form this server just handed out (and
    // is not on the setup AP) is refused before it touches the credentials.
    server_.send(403, "text/plain", "authorisation required");
    return;
  }

  WifiCredentials credentials;
  WifiValidation error = WifiValidation::kEmptySsid;
  bool accepted = false;
  if (server_.hasArg("ssid")) {
    // Browsers send the setup form as application/x-www-form-urlencoded, so
    // WebServer decodes the fields into args instead of the raw body.
    const String ssid = server_.arg("ssid");
    const String password = server_.hasArg("password") ? server_.arg("password") : "";
    error = makeWifiCredentials(ssid.c_str(), password.c_str(), &credentials);
    accepted = error == WifiValidation::kOk;
  } else {
    accepted = parseWifiSetupForm(server_.arg("plain").c_str(), &credentials, &error);
  }
  if (!accepted) {
    server_.send(400, "text/html", buildSetupPage(wifiValidationMessage(error)));
    return;
  }

  pendingCredentials_ = credentials;
  pendingSave_ = true;
  saveAtMs_ = millis() + kSaveDelayMs;
  // The form has done its job; the next save needs a fresh form and a fresh
  // token.
  setupSession_.reset();
  sendSavedPage(credentials.ssid);
}

void StationWebServer::sendSavedPage(const char* ssid) {
  std::unique_ptr<char[]> buffer(new (std::nothrow) char[2048]);
  if (!buffer) {
    server_.send(200, "text/plain", "saved");
    return;
  }
  const size_t needed = formatSavedPage(ssid, buffer.get(), 2048);
  if (needed >= 2048) {
    server_.send(200, "text/plain", "saved");
    return;
  }
  server_.send(200, "text/html", buffer.get());
}

void StationWebServer::handleNoContent() {
  server_.send(204, "text/plain", "");
}

void StationWebServer::handleNotFound() {
  if (wifi_ != nullptr && wifi_->apActive()) {
    // Captive portal: every probe lands on the setup page.
    server_.sendHeader("Location", kSetupApUrl);
    server_.send(302, "text/plain", "");
    return;
  }
  server_.send(404, "text/plain", "not found");
}

}  // namespace fw

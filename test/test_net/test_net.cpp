#include <unity.h>

#include <cstdio>
#include <cstring>

#include "net/ntp_clock.h"
#include "net/setup_session.h"
#include "station/oled_remote.h"
#include "net/web_dashboard.h"
#include "net/wifi_credentials.h"

void setUp() {}
void tearDown() {}

namespace {

using namespace fw;

// Deterministic stand-ins for the esp_random() counter on the device.
int g_tokenCounter = 0;
char g_generatedToken[SetupSession::kTokenLength + 1] = {0};

void testTokenGenerator(char* out, size_t outSize) {
  ++g_tokenCounter;
  std::snprintf(out, outSize, "tok-%d", g_tokenCounter);
}

void test_wifi_validation_rules() {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(WifiValidation::kOk),
                        static_cast<int>(validateWifiCredentials("Home", "password1")));
  // An empty password means an open network.
  TEST_ASSERT_EQUAL_INT(static_cast<int>(WifiValidation::kOk),
                        static_cast<int>(validateWifiCredentials("Home", "")));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(WifiValidation::kOk),
                        static_cast<int>(validateWifiCredentials("Home", nullptr)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(WifiValidation::kEmptySsid),
                        static_cast<int>(validateWifiCredentials("", "password1")));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(WifiValidation::kEmptySsid),
                        static_cast<int>(validateWifiCredentials(nullptr, "password1")));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(WifiValidation::kPasswordTooShort),
                        static_cast<int>(validateWifiCredentials("Home", "short")));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(WifiValidation::kPasswordTooLong),
                        static_cast<int>(validateWifiCredentials(
                            "Home", "1234567890123456789012345678901234567890123456789012345678901234")));

  char longSsid[40];
  std::memset(longSsid, 'a', 33);
  longSsid[33] = '\0';
  TEST_ASSERT_EQUAL_INT(static_cast<int>(WifiValidation::kSsidTooLong),
                        static_cast<int>(validateWifiCredentials(longSsid, "password1")));
}

void test_credentials_copy_only_when_valid() {
  WifiCredentials credentials;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(WifiValidation::kOk),
                        static_cast<int>(makeWifiCredentials("Cafe", "open sesame", &credentials)));
  TEST_ASSERT_TRUE(credentials.valid);
  TEST_ASSERT_EQUAL_STRING("Cafe", credentials.ssid);
  TEST_ASSERT_EQUAL_STRING("open sesame", credentials.password);

  WifiCredentials invalid;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(WifiValidation::kEmptySsid),
                        static_cast<int>(makeWifiCredentials("", "x", &invalid)));
  TEST_ASSERT_FALSE(invalid.valid);
}

void test_url_decode_handles_plus_and_percent() {
  char out[32];
  TEST_ASSERT_EQUAL_UINT32(10, static_cast<uint32_t>(urlDecode("My+Network", 10, out, sizeof(out))));
  TEST_ASSERT_EQUAL_STRING("My Network", out);
  urlDecode("p%40ss%20word", 13, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("p@ss word", out);
  urlDecode("bad%zz", 6, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("bad%zz", out);

  // A value that does not fit is reported instead of being silently shortened.
  char small[5];
  bool truncated = false;
  urlDecode("abcdefgh", 8, small, sizeof(small), &truncated);
  TEST_ASSERT_TRUE(truncated);
  TEST_ASSERT_EQUAL_STRING("abcd", small);
  urlDecode("abc", 3, small, sizeof(small), &truncated);
  TEST_ASSERT_FALSE(truncated);
}

void test_setup_form_parses() {
  WifiCredentials credentials;
  WifiValidation error = WifiValidation::kOk;
  TEST_ASSERT_TRUE(parseWifiSetupForm(
      "ssid=My+Network&password=p%40ssword", &credentials, &error));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(WifiValidation::kOk), static_cast<int>(error));
  TEST_ASSERT_EQUAL_STRING("My Network", credentials.ssid);
  TEST_ASSERT_EQUAL_STRING("p@ssword", credentials.password);

  TEST_ASSERT_TRUE(parseWifiSetupForm("ssid=Open", &credentials, &error));
  TEST_ASSERT_EQUAL_STRING("Open", credentials.ssid);
  TEST_ASSERT_EQUAL_STRING("", credentials.password);

  TEST_ASSERT_FALSE(parseWifiSetupForm("password=password1", &credentials, &error));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(WifiValidation::kEmptySsid), static_cast<int>(error));

  TEST_ASSERT_FALSE(parseWifiSetupForm("ssid=Home&password=short", &credentials, &error));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(WifiValidation::kPasswordTooShort),
                        static_cast<int>(error));
  TEST_ASSERT_FALSE(parseWifiSetupForm(nullptr, &credentials, &error));
}

void test_setup_form_rejects_values_that_do_not_fit() {
  WifiCredentials credentials;
  WifiValidation error = WifiValidation::kOk;
  // 33 characters: one over the 32 character SSID limit.
  TEST_ASSERT_FALSE(parseWifiSetupForm(
      "ssid=123456789012345678901234567890123", &credentials, &error));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(WifiValidation::kSsidTooLong),
                        static_cast<int>(error));

  // 64 characters: one over the 63 character password limit.
  TEST_ASSERT_FALSE(parseWifiSetupForm(
      "ssid=Home&password=1234567890123456789012345678901234567890123456789012345678901234",
      &credentials, &error));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(WifiValidation::kPasswordTooLong),
                        static_cast<int>(error));

  // Exactly at the limits is still accepted.
  TEST_ASSERT_TRUE(parseWifiSetupForm(
      "ssid=12345678901234567890123456789012&password=12345678", &credentials,
      &error));
}

void test_setup_form_rejects_an_embedded_nul() {
  WifiCredentials credentials;
  WifiValidation error = WifiValidation::kOk;
  // "Home%00Guest" fits the buffer, but only "Home" would be usable.
  TEST_ASSERT_FALSE(parseWifiSetupForm("ssid=Home%00Guest&password=12345678",
                                      &credentials, &error));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(WifiValidation::kEmbeddedNul),
                        static_cast<int>(error));

  TEST_ASSERT_FALSE(parseWifiSetupForm("ssid=Home&password=12345678%00tail",
                                      &credentials, &error));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(WifiValidation::kEmbeddedNul),
                        static_cast<int>(error));
}

void test_wifi_status_report_formats_local_time() {
  WifiStatus status;
  status.mode = WifiMode::kStation;
  status.connected = true;
  status.ntpSynced = true;
  std::snprintf(status.ssid, sizeof(status.ssid), "Home");
  std::snprintf(status.ip, sizeof(status.ip), "192.168.1.42");
  status.rssi = -55;
  status.epochSeconds = 1700000000;
  status.utcOffsetSeconds = 9 * 3600;

  char out[320];
  formatWifiStatus(status, out, sizeof(out));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "wifi: station (connected)"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "ssid: Home"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "ip: 192.168.1.42"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "rssi: -55 dBm"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "ntp: 2023-11-15 07:13:20"));
}

void test_station_url() {
  char out[32];
  TEST_ASSERT_EQUAL_UINT32(19, static_cast<uint32_t>(formatStationUrl("192.168.4.1", out, sizeof(out))));
  TEST_ASSERT_EQUAL_STRING("http://192.168.4.1/", out);
  formatStationUrl(nullptr, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("http://0.0.0.0/", out);
}

void test_dashboard_url_covers_station_and_setup_ap() {
  char out[40];
  // Once the stored network is up the QR code opens the station's own IP.
  TEST_ASSERT_EQUAL_UINT32(20, static_cast<uint32_t>(
                                   formatDashboardUrl(true, "192.168.1.42", false, out, sizeof(out))));
  TEST_ASSERT_EQUAL_STRING("http://192.168.1.42/", out);

  // Before a network is saved the setup AP is the only dashboard there is, so
  // CON on the Wi-Fi screen has to show its address instead of nothing.
  TEST_ASSERT_EQUAL_UINT32(19, static_cast<uint32_t>(
                                   formatDashboardUrl(false, "0.0.0.0", true, out, sizeof(out))));
  TEST_ASSERT_EQUAL_STRING("http://192.168.4.1/", out);

  // Neither associated nor serving the AP: no address to show.
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(
                                   formatDashboardUrl(false, "0.0.0.0", false, out, sizeof(out))));
  TEST_ASSERT_EQUAL_STRING("", out);
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(
                                   formatDashboardUrl(false, "0.0.0.0", false, nullptr, 0)));
}

void test_setup_token_is_reused_while_it_is_valid() {
  g_tokenCounter = 0;
  SetupSession session;
  char first[SetupSession::kTokenLength + 1];
  std::snprintf(first, sizeof(first), "%s", session.token(1000, 600000, testTokenGenerator));
  TEST_ASSERT_EQUAL_STRING("tok-1", first);

  // /favicon.ico, /robots.txt and captive portal probes serve the page again;
  // the form the browser is showing has to keep working.
  const char* again = session.token(2000, 600000, testTokenGenerator);
  TEST_ASSERT_EQUAL_STRING(first, again);
  TEST_ASSERT_EQUAL_STRING("tok-1", session.token(590000, 600000, testTokenGenerator));
  TEST_ASSERT_TRUE(session.authorised(first, 590000, 600000));
}

void test_setup_token_renews_after_it_expires() {
  g_tokenCounter = 0;
  SetupSession session;
  char first[SetupSession::kTokenLength + 1];
  std::snprintf(first, sizeof(first), "%s", session.token(1000, 600000, testTokenGenerator));

  const char* renewed = session.token(1000 + 600001, 600000, testTokenGenerator);
  TEST_ASSERT_TRUE(std::strcmp(first, renewed) != 0);
  TEST_ASSERT_FALSE(session.authorised(first, 1000 + 600001, 600000));
  TEST_ASSERT_TRUE(session.authorised(renewed, 1000 + 600001, 600000));
}

void test_setup_token_rejects_a_wrong_missing_or_reset_token() {
  g_tokenCounter = 0;
  SetupSession session;
  std::snprintf(g_generatedToken, sizeof(g_generatedToken), "%s",
                session.token(0, 600000, testTokenGenerator));

  TEST_ASSERT_FALSE(session.authorised("tok-9", 10, 600000));
  TEST_ASSERT_FALSE(session.authorised("", 10, 600000));
  TEST_ASSERT_FALSE(session.authorised(nullptr, 10, 600000));

  // Saving (or the AP stopping) drops the token, so the next form gets a new one.
  session.reset();
  TEST_ASSERT_FALSE(session.active());
  TEST_ASSERT_FALSE(session.authorised(g_generatedToken, 20, 600000));
  TEST_ASSERT_EQUAL_STRING("", session.current());

  const char* fresh = session.token(30, 600000, testTokenGenerator);
  TEST_ASSERT_EQUAL_STRING("tok-2", fresh);
  TEST_ASSERT_TRUE(session.authorised(fresh, 40, 600000));
}

void test_setup_token_without_a_generator_stays_empty() {
  SetupSession session;
  TEST_ASSERT_EQUAL_STRING("", session.token(0, 600000, nullptr));
  TEST_ASSERT_FALSE(session.authorised("", 0, 600000));
}

void test_civil_time_conversion() {
  CivilTime time = civilTimeFromUnix(0);
  TEST_ASSERT_EQUAL_INT(1970, time.year);
  TEST_ASSERT_EQUAL_UINT8(1, time.month);
  TEST_ASSERT_EQUAL_UINT8(1, time.day);
  TEST_ASSERT_EQUAL_UINT8(4, time.weekday);

  time = civilTimeFromUnix(1700000000);
  TEST_ASSERT_EQUAL_INT(2023, time.year);
  TEST_ASSERT_EQUAL_UINT8(11, time.month);
  TEST_ASSERT_EQUAL_UINT8(14, time.day);
  TEST_ASSERT_EQUAL_UINT8(22, time.hour);
  TEST_ASSERT_EQUAL_UINT8(13, time.minute);
  TEST_ASSERT_EQUAL_UINT8(20, time.second);
  TEST_ASSERT_EQUAL_UINT8(2, time.weekday);

  time = civilTimeFromUnix(-1);
  TEST_ASSERT_EQUAL_INT(1969, time.year);
  TEST_ASSERT_EQUAL_UINT8(12, time.month);
  TEST_ASSERT_EQUAL_UINT8(31, time.day);
  TEST_ASSERT_EQUAL_UINT8(23, time.hour);
  TEST_ASSERT_EQUAL_UINT8(59, time.minute);
  TEST_ASSERT_EQUAL_UINT8(59, time.second);

  // Leap day 2024-02-29 00:00:00 UTC.
  time = civilTimeFromUnix(1709164800);
  TEST_ASSERT_EQUAL_INT(2024, time.year);
  TEST_ASSERT_EQUAL_UINT8(2, time.month);
  TEST_ASSERT_EQUAL_UINT8(29, time.day);
  TEST_ASSERT_EQUAL_UINT8(0, time.hour);
}

void test_timestamp_formatting() {
  char out[24];
  TEST_ASSERT_EQUAL_UINT32(19, static_cast<uint32_t>(
                                   formatTimestamp(1700000000, 9 * 3600, out, sizeof(out))));
  TEST_ASSERT_EQUAL_STRING("2023-11-15 07:13:20", out);

  formatTimestamp(1700000000, 0, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("2023-11-14 22:13:20", out);

  char clock[8];
  formatClockTime(1700000000, 9 * 3600, clock, sizeof(clock));
  TEST_ASSERT_EQUAL_STRING("07:13", clock);

  char date[12];
  formatDate(1700000000, 9 * 3600, date, sizeof(date));
  TEST_ASSERT_EQUAL_STRING("2023-11-15", date);

  char tiny[4];
  TEST_ASSERT_TRUE(formatTimestamp(1700000000, 0, tiny, sizeof(tiny)) > sizeof(tiny) - 1);
  TEST_ASSERT_EQUAL_UINT32(sizeof(tiny) - 1, static_cast<uint32_t>(std::strlen(tiny)));
}

void test_ntp_clock_extrapolates_and_clears() {
  NtpClock clock;
  TEST_ASSERT_FALSE(clock.synced());
  TEST_ASSERT_EQUAL_INT64(0, clock.epochAt(1234));

  clock.markSynced(1000, 5000);
  TEST_ASSERT_TRUE(clock.synced());
  TEST_ASSERT_EQUAL_INT64(1000, clock.epochAt(5000));
  TEST_ASSERT_EQUAL_INT64(1060, clock.epochAt(65000));
  TEST_ASSERT_EQUAL_INT64(1000, clock.lastSyncEpoch());
  TEST_ASSERT_EQUAL_UINT32(5000, clock.lastSyncMs());

  clock.clear();
  TEST_ASSERT_FALSE(clock.synced());
  TEST_ASSERT_EQUAL_INT64(0, clock.epochAt(65000));
}

DashboardState fullState() {
  DashboardState state;
  state.version = "0.3.0";
  state.wifiMode = WifiMode::kStation;
  state.wifiConnected = true;
  std::snprintf(state.ssid, sizeof(state.ssid), "Home");
  std::snprintf(state.ip, sizeof(state.ip), "192.168.1.42");
  state.rssi = -55;
  state.ntpSynced = true;
  std::snprintf(state.timestamp, sizeof(state.timestamp), "2026-11-25 07:37:00");
  state.logStorageReady = true;
  state.loggingEnabled = true;
  state.logRecordCount = 12;
  state.logIntervalMs = 60000;
  state.logFileBytes = 1234;
  state.bleEnabled = true;
  state.bleHidMode = true;
  state.bleConnected = false;
  state.timerRunning = true;
  std::snprintf(state.timerRemaining, sizeof(state.timerRemaining), "25:00");
  state.timerPhase = "work";
  state.mode = "environment";
  state.uptimeMs = 123456;
  state.freeHeapBytes = 123456;
  state.sensors.bmp280Present = true;
  state.sensors.bmp280Chip = "BMP280";
  state.sensors.temperatureValid = true;
  state.sensors.temperatureC = 23.4f;
  state.sensors.pressureValid = true;
  state.sensors.pressureHpa = 1006.5f;
  state.sensors.humidityValid = true;
  state.sensors.humidityPct = 45.0f;
  return state;
}

void test_status_json_contains_every_section() {
  const DashboardState state = fullState();
  char out[1536];
  const size_t needed = formatStatusJson(state, out, sizeof(out));
  TEST_ASSERT_TRUE(needed < sizeof(out));
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(std::strlen(out)),
                           static_cast<uint32_t>(needed));

  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"version\":\"0.3.0\""));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"uptime_ms\":123456"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"free_heap\":123456"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"mode\":\"environment\""));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"mode\":\"station\""));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"connected\":true"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"ssid\":\"Home\""));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"ip\":\"192.168.1.42\""));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"rssi\":-55"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"ntp\":true"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"time\":\"2026-11-25 07:37:00\""));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"temperature\":23.40"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"humidity\":45.00"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"pressure\":1006.50"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"trend\":\"cloudy\""));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"index\":69.2"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"label\":\"warm\""));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"high_temperature\":false"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"low_humidity\":false"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"pressure_jump\":false"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"records\":12"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"interval_ms\":60000"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"hid\":true"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"timer\":{\"running\":true"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"remaining\":\"25:00\""));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"phase\":\"work\""));
}

void test_status_json_uses_null_for_missing_sensors() {
  DashboardState state = fullState();
  state.sensors = SensorSnapshot{};
  state.ntpSynced = false;
  state.timestamp[0] = '\0';
  state.alerts.highTemperature = true;
  char out[1536];
  formatStatusJson(state, out, sizeof(out));

  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"temperature\":null"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"humidity\":null"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"pressure\":null"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"comfort\":null"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"time\":null"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"high_temperature\":true"));
}

void test_status_json_escapes_strings() {
  DashboardState state = fullState();
  std::snprintf(state.ssid, sizeof(state.ssid), "My \"Net\"\\");
  char out[1536];
  formatStatusJson(state, out, sizeof(out));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"ssid\":\"My \\\"Net\\\"\\\\\""));
}

void test_logs_json_formats_samples() {
  LogSample samples[2];
  samples[0].uptimeMs = 1000;
  samples[0].hasTemperature = true;
  samples[0].temperatureC = 20.0f;
  std::snprintf(samples[1].timestamp, sizeof(samples[1].timestamp), "2026-11-25 07:37:00");
  samples[1].uptimeMs = 2000;
  samples[1].hasPressure = true;
  samples[1].pressureHpa = 1000.5f;

  char out[256];
  formatLogsJson(samples, 2, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING(
      "[{\"t\":null,\"uptime_ms\":1000,\"temp_c\":20.00,\"pressure_hpa\":null,"
      "\"humidity_pct\":null},{\"t\":\"2026-11-25 07:37:00\",\"uptime_ms\":2000,"
      "\"temp_c\":null,\"pressure_hpa\":1000.50,\"humidity_pct\":null}]",
      out);
}

void test_logs_json_from_csv_keeps_the_newest_rows() {
  const char* csv =
      "timestamp,uptime_ms,temp_c,pressure_hpa,humidity_pct\n"
      "2026-11-25 07:00:00,1000,20.00,1000.00,40.0\n"
      "2026-11-25 07:01:00,2000,20.50,1000.10,41.0\n"
      "2026-11-25 07:02:00,3000,21.00,1000.20,42.0\n";

  char out[512];
  const size_t needed = formatLogsJsonFromCsv(csv, 2, out, sizeof(out));
  TEST_ASSERT_TRUE(needed < sizeof(out));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"uptime_ms\":2000"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"uptime_ms\":3000"));
  TEST_ASSERT_NULL(std::strstr(out, "\"uptime_ms\":1000"));

  formatLogsJsonFromCsv("", 10, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("[]", out);
  formatLogsJsonFromCsv(nullptr, 10, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("[]", out);
}

void test_dashboard_html_is_self_contained() {
  const char* html = dashboardHtml();
  TEST_ASSERT_NOT_NULL(std::strstr(html, "/api/status"));
  TEST_ASSERT_NOT_NULL(std::strstr(html, "/api/logs"));
  TEST_ASSERT_NOT_NULL(std::strstr(html, "/download"));
  TEST_ASSERT_NOT_NULL(std::strstr(html, "<canvas"));
  TEST_ASSERT_NOT_NULL(std::strstr(html, "/setup"));
  TEST_ASSERT_NULL(std::strstr(html, "http://"));
  TEST_ASSERT_NULL(std::strstr(html, "https://"));
}

void test_oled_command_parses_power_mode_and_actions() {
  OledCommand command;
  const char* error = nullptr;

  TEST_ASSERT_TRUE(parseOledCommand("on", nullptr, nullptr, nullptr, &command, &error));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(OledPowerAction::kOn),
                        static_cast<int>(command.power));
  TEST_ASSERT_TRUE(command.any());

  TEST_ASSERT_TRUE(parseOledCommand("OFF", nullptr, nullptr, nullptr, &command, &error));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(OledPowerAction::kOff),
                        static_cast<int>(command.power));

  TEST_ASSERT_TRUE(
      parseOledCommand("toggle", nullptr, nullptr, nullptr, &command, &error));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(OledPowerAction::kToggle),
                        static_cast<int>(command.power));

  // Everything empty is a no-op request.
  TEST_ASSERT_TRUE(parseOledCommand("", "", "", "", &command, &error));
  TEST_ASSERT_FALSE(command.any());

  TEST_ASSERT_TRUE(parseOledCommand(nullptr, "pet", nullptr, nullptr, &command, &error));
  TEST_ASSERT_TRUE(command.hasMode);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StationMode::kPet),
                        static_cast<int>(command.mode));

  TEST_ASSERT_TRUE(parseOledCommand(nullptr, "MENU", nullptr, nullptr, &command, &error));
  TEST_ASSERT_TRUE(command.hasMenu);
  TEST_ASSERT_FALSE(command.hasMode);

  TEST_ASSERT_TRUE(
      parseOledCommand(nullptr, "env", "next", "graph", &command, &error));
  TEST_ASSERT_EQUAL_INT(1, static_cast<int>(command.step));
  TEST_ASSERT_TRUE(command.hasView);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(EnvView::kGraph),
                        static_cast<int>(command.view));

  TEST_ASSERT_TRUE(
      parseOledCommand(nullptr, nullptr, "back", nullptr, &command, &error));
  TEST_ASSERT_TRUE(command.back);
  TEST_ASSERT_TRUE(
      parseOledCommand(nullptr, nullptr, "select", nullptr, &command, &error));
  TEST_ASSERT_TRUE(command.select);
  TEST_ASSERT_TRUE(
      parseOledCommand(nullptr, nullptr, "clock", nullptr, &command, &error));
  TEST_ASSERT_TRUE(command.testChime);
  TEST_ASSERT_TRUE(
      parseOledCommand(nullptr, nullptr, "prev", nullptr, &command, &error));
  TEST_ASSERT_EQUAL_INT(-1, static_cast<int>(command.step));
}

void test_oled_command_rejects_unknown_values() {
  OledCommand command;
  const char* error = nullptr;
  TEST_ASSERT_FALSE(parseOledCommand("dim", nullptr, nullptr, nullptr, &command, &error));
  TEST_ASSERT_NOT_NULL(error);
  TEST_ASSERT_FALSE(parseOledCommand(nullptr, "bogus", nullptr, nullptr, &command, &error));
  TEST_ASSERT_NOT_NULL(error);
  TEST_ASSERT_FALSE(
      parseOledCommand(nullptr, nullptr, "explode", nullptr, &command, &error));
  TEST_ASSERT_NOT_NULL(error);
  TEST_ASSERT_FALSE(
      parseOledCommand(nullptr, nullptr, nullptr, "tilt", &command, &error));
  TEST_ASSERT_NOT_NULL(error);
}

void test_oled_state_json_reports_the_screen() {
  PetScene pet;
  std::snprintf(pet.clock, sizeof(pet.clock), "14:35");
  std::snprintf(pet.speech, sizeof(pet.speech), "nice day");
  std::snprintf(pet.bubbleText, sizeof(pet.bubbleText), "07:00 !");
  pet.spriteX = 42;
  pet.mood = PetMood::kWarm;
  pet.activity = PetActivity::kSit;

  OledState state;
  state.power = false;
  state.panelPresent = true;
  state.address = 0x3C;
  state.screen = "mode";
  state.mode = "pet";
  state.menu = "pet";
  state.view = "summary";
  state.pet = &pet;

  char out[512];
  const size_t needed = formatOledStateJson(state, out, sizeof(out));
  TEST_ASSERT_TRUE(needed < sizeof(out));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"power\":false"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"present\":true"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"address\":\"0x3C\""));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"screen\":\"mode\""));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"mode\":\"pet\""));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"activity\":\"sit\""));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"mood\":\"warm\""));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"clock\":\"14:35\""));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"bubble\":\"\""));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"x\":42"));

  // A truncated buffer still reports the size the full JSON needs.
  char small[32];
  TEST_ASSERT_TRUE(formatOledStateJson(state, small, sizeof(small)) > sizeof(small));
  TEST_ASSERT_EQUAL_CHAR('\0', small[sizeof(small) - 1]);

  // Without a scene the pet block is null instead of missing.
  OledState bare;
  formatOledStateJson(bare, out, sizeof(out));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "\"pet\":null"));
}

void test_dashboard_ships_the_oled_remote() {
  const char* html = dashboardHtml();
  TEST_ASSERT_NOT_NULL(std::strstr(html, "/api/oled"));
  TEST_ASSERT_NOT_NULL(std::strstr(html, "OLED remote"));
  TEST_ASSERT_NOT_NULL(std::strstr(html, "data-mode=\"pet\""));
  TEST_ASSERT_NOT_NULL(std::strstr(html, "data-view=\"graph\""));
  TEST_ASSERT_NOT_NULL(std::strstr(html, "action:'clock'"));
  TEST_ASSERT_NOT_NULL(std::strstr(html, "power:'toggle'"));
}

void test_setup_page_lists_and_escapes_networks() {
  const char* ssids[] = {"Home", "<script>"};
  char out[4096];
  const size_t needed =
      formatSetupPage(ssids, 2, "wrong password", "abc123", false, out, sizeof(out));
  TEST_ASSERT_TRUE(needed < sizeof(out));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "action=\"/save\""));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "Nearby networks"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "Home"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "&lt;script&gt;"));
  TEST_ASSERT_NULL(std::strstr(out, "<script>"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "wrong password"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "name=\"password\""));
  // The form carries the short lived token POST /save has to echo back.
  TEST_ASSERT_NOT_NULL(std::strstr(out, "name=\"token\" value=\"abc123\""));
  // With results the page must not reload itself.
  TEST_ASSERT_NULL(std::strstr(out, "location.reload"));
}

void test_setup_page_reloads_while_the_scan_is_empty() {
  char out[4096];
  formatSetupPage(nullptr, 0, "", "tok", true, out, sizeof(out));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "no networks found yet"));
  // The reload must keep whatever is already typed into the form.
  TEST_ASSERT_NOT_NULL(std::strstr(out, "location.reload"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "ssid.value"));
}

void test_setup_page_does_not_reload_after_an_empty_scan() {
  char out[4096];
  // Hidden SSIDs: the scan finished with nothing, so hand entry must survive.
  formatSetupPage(nullptr, 0, "", "tok", false, out, sizeof(out));
  TEST_ASSERT_NULL(std::strstr(out, "location.reload"));
}

void test_setup_locked_page_explains_the_device_path() {
  char out[2048];
  const size_t needed = formatSetupLockedPage(out, sizeof(out));
  TEST_ASSERT_TRUE(needed < sizeof(out));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "locked"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "ESP32-Station-AP"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "wifi ap"));
}

void test_saved_page_escapes_the_ssid() {
  char out[2048];
  formatSavedPage("A&B", out, sizeof(out));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "A&amp;B"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "Saved"));
}

}  // namespace

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_wifi_validation_rules);
  RUN_TEST(test_credentials_copy_only_when_valid);
  RUN_TEST(test_url_decode_handles_plus_and_percent);
  RUN_TEST(test_setup_form_parses);
  RUN_TEST(test_setup_form_rejects_values_that_do_not_fit);
  RUN_TEST(test_setup_form_rejects_an_embedded_nul);
  RUN_TEST(test_wifi_status_report_formats_local_time);
  RUN_TEST(test_station_url);
  RUN_TEST(test_dashboard_url_covers_station_and_setup_ap);
  RUN_TEST(test_setup_token_is_reused_while_it_is_valid);
  RUN_TEST(test_setup_token_renews_after_it_expires);
  RUN_TEST(test_setup_token_rejects_a_wrong_missing_or_reset_token);
  RUN_TEST(test_setup_token_without_a_generator_stays_empty);
  RUN_TEST(test_civil_time_conversion);
  RUN_TEST(test_timestamp_formatting);
  RUN_TEST(test_ntp_clock_extrapolates_and_clears);
  RUN_TEST(test_status_json_contains_every_section);
  RUN_TEST(test_status_json_uses_null_for_missing_sensors);
  RUN_TEST(test_status_json_escapes_strings);
  RUN_TEST(test_logs_json_formats_samples);
  RUN_TEST(test_logs_json_from_csv_keeps_the_newest_rows);
  RUN_TEST(test_dashboard_html_is_self_contained);
  RUN_TEST(test_oled_command_parses_power_mode_and_actions);
  RUN_TEST(test_oled_command_rejects_unknown_values);
  RUN_TEST(test_oled_state_json_reports_the_screen);
  RUN_TEST(test_dashboard_ships_the_oled_remote);
  RUN_TEST(test_setup_page_lists_and_escapes_networks);
  RUN_TEST(test_setup_page_reloads_while_the_scan_is_empty);
  RUN_TEST(test_setup_page_does_not_reload_after_an_empty_scan);
  RUN_TEST(test_setup_locked_page_explains_the_device_path);
  RUN_TEST(test_saved_page_escapes_the_ssid);
  return UNITY_END();
}

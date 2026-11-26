#include <unity.h>

#include <cstring>
#include <string>

#include "app_config.h"
#include "graphics/mini_graph.h"
#include "station/station_model.h"

void setUp() {}
void tearDown() {}

namespace {

using namespace fw;

std::string pageText(const PageLines& page) {
  std::string text;
  for (size_t index = 0; index < page.count; ++index) {
    text += page.lines[index].text;
    text += '\n';
  }
  return text;
}

bool pageHas(const PageLines& page, const char* needle) {
  for (size_t index = 0; index < page.count; ++index) {
    if (std::strstr(page.lines[index].text, needle) != nullptr) {
      return true;
    }
  }
  return false;
}

SensorSnapshot healthySensors() {
  SensorSnapshot sensors;
  sensors.bmp280Present = true;
  sensors.bmp280Chip = "BMP280";
  sensors.bmp280Address = 0x76;
  sensors.temperatureValid = true;
  sensors.temperatureC = 23.4f;
  sensors.pressureValid = true;
  sensors.pressureHpa = 1006.5f;
  sensors.dhtPresent = true;
  sensors.dhtType = DhtType::kDht22;
  sensors.humidityValid = true;
  sensors.humidityPct = 45.0f;
  sensors.dhtTemperatureC = 22.9f;
  return sensors;
}

StationView healthyView() {
  StationView view;
  view.version = "0.3.0";
  view.sensors = healthySensors();
  view.loggingEnabled = true;
  view.logStorageReady = true;
  view.logRecordCount = 12;
  view.logIntervalMs = 60000;
  view.logIntervalRemainingMs = 42000;
  view.logFileBytes = 1234;
  view.uptimeMs = 3723000;
  view.freeHeapBytes = 123456;
  view.oledAddress = 0x3C;
  std::snprintf(view.i2cAddresses, sizeof(view.i2cAddresses), "0x3C,0x76");
  view.dhtPin = 4;
  std::snprintf(view.clock, sizeof(view.clock), "12:34");
  return view;
}

void test_menu_lists_all_seven_modes_with_cursor() {
  StationView view = healthyView();
  view.menuSelection = StationMode::kWifi;

  PageLines page;
  buildStationMenu(view, &page);
  // The title carries the clock and the seven modes fill the remaining lines.
  TEST_ASSERT_EQUAL_UINT32(8, page.count);
  TEST_ASSERT_EQUAL_STRING("STATION MENU 12:34", page.lines[0].text);
  TEST_ASSERT_EQUAL_STRING(" 1 PET CAT", page.lines[1].text);
  TEST_ASSERT_EQUAL_STRING(" 2 ENV MONITOR", page.lines[2].text);
  TEST_ASSERT_EQUAL_STRING(" 3 LOG LittleFS", page.lines[3].text);
  TEST_ASSERT_EQUAL_STRING(">4 WIFI & WEB", page.lines[4].text);
  TEST_ASSERT_EQUAL_STRING(" 5 BLUETOOTH", page.lines[5].text);
  TEST_ASSERT_EQUAL_STRING(" 6 TIMER", page.lines[6].text);
  TEST_ASSERT_EQUAL_STRING(" 7 SYSTEM", page.lines[7].text);
  TEST_ASSERT_TRUE(page.height() <= Framebuffer::kHeight);
}

void test_menu_marks_the_selected_pet_entry() {
  StationView view = healthyView();
  view.menuSelection = StationMode::kPet;
  PageLines page;
  buildStationMenu(view, &page);
  TEST_ASSERT_EQUAL_STRING(">1 PET CAT", page.lines[1].text);
  TEST_ASSERT_EQUAL_STRING(" 3 LOG LittleFS", page.lines[3].text);
}

void test_pet_page_reports_the_animation_state() {
  StationView view = healthyView();
  PetScene pet;
  std::snprintf(pet.clock, sizeof(pet.clock), "12:34");
  std::snprintf(pet.speech, sizeof(pet.speech), "nice day");
  std::snprintf(pet.sensors, sizeof(pet.sensors), "23.4C 45%%");
  pet.mood = PetMood::kComfortable;
  pet.activity = PetActivity::kSit;
  view.pet = &pet;

  PageLines page;
  buildStationPage(StationMode::kPet, view, &page);
  TEST_ASSERT_TRUE(pageHas(page, "VIRTUAL PET"));
  TEST_ASSERT_TRUE(pageHas(page, "12:34 nice"));
  TEST_ASSERT_TRUE(pageHas(page, "state sit"));
  TEST_ASSERT_TRUE(pageHas(page, "nice day"));
  TEST_ASSERT_TRUE(pageHas(page, "BAK menu"));
}

void test_pet_page_without_a_scene_falls_back_to_text() {
  StationView view = healthyView();
  view.pet = nullptr;
  PageLines page;
  buildStationPage(StationMode::kPet, view, &page);
  TEST_ASSERT_TRUE(pageHas(page, "VIRTUAL PET"));
  TEST_ASSERT_TRUE(pageHas(page, "state walk"));
}

void test_environment_summary_shows_every_reading() {
  StationView view = healthyView();
  PageLines page;
  buildStationPage(StationMode::kEnvironment, view, &page);

  TEST_ASSERT_EQUAL_STRING("ENV STATION", page.lines[0].text);
  TEST_ASSERT_EQUAL_UINT8(2, page.lines[1].scale);
  TEST_ASSERT_TRUE(pageHas(page, "23.4"));
  TEST_ASSERT_TRUE(pageHas(page, "1006.5 hPa steady"));
  TEST_ASSERT_TRUE(pageHas(page, "45.0 % DHT22"));
  TEST_ASSERT_TRUE(pageHas(page, "warm/cloudy"));
  TEST_ASSERT_TRUE(pageHas(page, "REC 12 n42s"));
  TEST_ASSERT_TRUE(pageHas(page, "alert none"));
  TEST_ASSERT_TRUE(page.height() <= Framebuffer::kHeight);
}

void test_environment_summary_handles_missing_sensors() {
  StationView view = healthyView();
  view.sensors = SensorSnapshot{};
  PageLines page;
  buildStationPage(StationMode::kEnvironment, view, &page);
  TEST_ASSERT_TRUE(pageHas(page, "--.-"));
  TEST_ASSERT_TRUE(pageHas(page, "---.- hPa"));
  TEST_ASSERT_TRUE(pageHas(page, "humidity: none"));
  TEST_ASSERT_TRUE(pageHas(page, "weather cloudy"));
}

void test_environment_summary_shows_alerts() {
  StationView view = healthyView();
  view.sensors.temperatureC = 31.0f;
  view.sensors.humidityPct = 30.0f;
  view.pressureDeltaHpa = 2.0f;

  PageLines page;
  buildStationPage(StationMode::kEnvironment, view, &page);
  TEST_ASSERT_TRUE(pageHas(page, "HOT31.0"));
  TEST_ASSERT_TRUE(pageHas(page, "DRY30"));
  TEST_ASSERT_TRUE(pageHas(page, "PJMP"));
}

void test_environment_detail_shows_trend_and_comfort() {
  StationView view = healthyView();
  view.envView = EnvView::kDetail;
  view.sensors.pressureTrend = 1;
  view.pressureDeltaHpa = 2.0f;

  PageLines page;
  buildStationPage(StationMode::kEnvironment, view, &page);
  TEST_ASSERT_TRUE(pageHas(page, "BMP280 0x76"));
  TEST_ASSERT_TRUE(pageHas(page, "trend rising +2.0"));
  TEST_ASSERT_TRUE(pageHas(page, "DI 69.2 warm"));
  TEST_ASSERT_TRUE(pageHas(page, "DHT22 45.0 % 22.9"));
}

void test_logs_page_reports_ntp_and_file() {
  StationView view = healthyView();
  view.ntpSynced = true;
  PageLines page;
  buildStationPage(StationMode::kLogs, view, &page);

  TEST_ASSERT_EQUAL_STRING("LOG littlefs", page.lines[0].text);
  TEST_ASSERT_TRUE(pageHas(page, "state REC"));
  TEST_ASSERT_TRUE(pageHas(page, "records 12"));
  TEST_ASSERT_TRUE(pageHas(page, "every 1 min"));
  TEST_ASSERT_TRUE(pageHas(page, "next 42 s"));
  TEST_ASSERT_TRUE(pageHas(page, "size 1234 B"));
  TEST_ASSERT_TRUE(pageHas(page, "time ntp"));
  TEST_ASSERT_TRUE(pageHas(page, "/log.csv"));
}

void test_wifi_page_station_and_setup() {
  StationView view = healthyView();
  view.wifiMode = WifiMode::kStation;
  view.wifiConnected = true;
  std::snprintf(view.wifiSsid, sizeof(view.wifiSsid), "Home");
  std::snprintf(view.ip, sizeof(view.ip), "192.168.1.42");
  view.rssi = -55;
  view.ntpSynced = true;

  PageLines page;
  buildStationPage(StationMode::kWifi, view, &page);
  TEST_ASSERT_EQUAL_STRING("WIFI STATION", page.lines[0].text);
  TEST_ASSERT_TRUE(pageHas(page, "ssid Home"));
  TEST_ASSERT_TRUE(pageHas(page, "ip 192.168.1.42"));
  TEST_ASSERT_TRUE(pageHas(page, "rssi -55 dBm"));
  TEST_ASSERT_TRUE(pageHas(page, "time 12:34"));
  TEST_ASSERT_TRUE(pageHas(page, "CON show QR"));

  view.wifiMode = WifiMode::kAccessPoint;
  buildStationPage(StationMode::kWifi, view, &page);
  TEST_ASSERT_EQUAL_STRING("WIFI SETUP (AP)", page.lines[0].text);
  TEST_ASSERT_TRUE(pageHas(page, kSetupApSsid));
  TEST_ASSERT_TRUE(pageHas(page, "http://192.168.4.1/"));
}

void test_ble_page_reflects_hid_mode() {
  StationView view = healthyView();
  view.bleEnabled = true;
  view.bleHidMode = false;
  view.bleConnected = true;

  PageLines page;
  buildStationPage(StationMode::kBle, view, &page);
  TEST_ASSERT_EQUAL_STRING("BLE ON", page.lines[0].text);
  TEST_ASSERT_TRUE(pageHas(page, "hid off"));
  TEST_ASSERT_TRUE(pageHas(page, "link connected"));
  TEST_ASSERT_TRUE(pageHas(page, "PSH HID on"));

  view.bleHidMode = true;
  buildStationPage(StationMode::kBle, view, &page);
  TEST_ASSERT_TRUE(pageHas(page, "hid media remote"));
  TEST_ASSERT_TRUE(pageHas(page, "PSH play  CON next"));
  TEST_ASSERT_TRUE(pageHas(page, "BAK prev  hold menu"));
}

void test_timer_page_has_big_readout_and_progress() {
  StationView view = healthyView();
  std::snprintf(view.timerRemaining, sizeof(view.timerRemaining), "25:00");
  view.timerRunning = true;
  view.timerProgress = 40;
  view.timerPomodoro = true;
  view.timerPhase = "work";

  PageLines page;
  buildStationPage(StationMode::kTimer, view, &page);
  TEST_ASSERT_EQUAL_STRING("TIMER POMODORO", page.lines[0].text);
  TEST_ASSERT_EQUAL_STRING("25:00", page.lines[1].text);
  TEST_ASSERT_EQUAL_UINT8(2, page.lines[1].scale);
  TEST_ASSERT_TRUE(pageHas(page, "work running"));
  TEST_ASSERT_TRUE(pageHas(page, "[####------] 40%"));
  TEST_ASSERT_TRUE(page.height() <= Framebuffer::kHeight);
}

void test_system_page_lists_links_and_storage() {
  StationView view = healthyView();
  view.wifiConnected = true;
  std::snprintf(view.ip, sizeof(view.ip), "192.168.1.42");
  view.bleEnabled = true;

  PageLines page;
  buildStationPage(StationMode::kSystem, view, &page);
  TEST_ASSERT_EQUAL_STRING("SYSTEM v0.3.0", page.lines[0].text);
  TEST_ASSERT_TRUE(pageHas(page, "up 1h02m03s"));
  TEST_ASSERT_TRUE(pageHas(page, "heap 123456 B"));
  TEST_ASSERT_TRUE(pageHas(page, "i2c 0x3C,0x76"));
  TEST_ASSERT_TRUE(pageHas(page, "oled 0x3C"));
  TEST_ASSERT_TRUE(pageHas(page, "wifi 192.168.1.42"));
  TEST_ASSERT_TRUE(pageHas(page, "ble on"));
  TEST_ASSERT_TRUE(pageHas(page, "log ready"));
}

void test_mode_names_and_cycle() {
  TEST_ASSERT_EQUAL_STRING("environment", stationModeName(StationMode::kEnvironment));
  TEST_ASSERT_EQUAL_STRING("pet", stationModeName(StationMode::kPet));
  TEST_ASSERT_EQUAL_STRING("logs", stationModeName(StationMode::kLogs));
  TEST_ASSERT_EQUAL_STRING("wifi", stationModeName(StationMode::kWifi));
  TEST_ASSERT_EQUAL_STRING("ble", stationModeName(StationMode::kBle));
  TEST_ASSERT_EQUAL_STRING("timer", stationModeName(StationMode::kTimer));
  TEST_ASSERT_EQUAL_STRING("system", stationModeName(StationMode::kSystem));

  TEST_ASSERT_EQUAL_INT(static_cast<int>(StationMode::kLogs),
                        static_cast<int>(nextStationMode(StationMode::kEnvironment)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StationMode::kPet),
                        static_cast<int>(nextStationMode(StationMode::kSystem)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StationMode::kSystem),
                        static_cast<int>(previousStationMode(StationMode::kPet)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StationMode::kPet),
                        static_cast<int>(stationModeFromIndex(7)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StationMode::kLogs),
                        static_cast<int>(stationModeFromIndex(2)));
}

void test_mode_names_parse() {
  StationMode mode = StationMode::kSystem;
  TEST_ASSERT_TRUE(stationModeFromName("env", &mode));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StationMode::kEnvironment), static_cast<int>(mode));
  TEST_ASSERT_TRUE(stationModeFromName("ENVIRONMENT", &mode));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StationMode::kEnvironment), static_cast<int>(mode));
  TEST_ASSERT_TRUE(stationModeFromName("log", &mode));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StationMode::kLogs), static_cast<int>(mode));
  TEST_ASSERT_TRUE(stationModeFromName("bluetooth", &mode));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StationMode::kBle), static_cast<int>(mode));
  TEST_ASSERT_TRUE(stationModeFromName("6", &mode));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StationMode::kTimer), static_cast<int>(mode));
  TEST_ASSERT_TRUE(stationModeFromName("7", &mode));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StationMode::kSystem), static_cast<int>(mode));
  TEST_ASSERT_TRUE(stationModeFromName("1", &mode));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StationMode::kPet), static_cast<int>(mode));
  TEST_ASSERT_TRUE(stationModeFromName("pet", &mode));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StationMode::kPet), static_cast<int>(mode));
  TEST_ASSERT_TRUE(stationModeFromName("CAT", &mode));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StationMode::kPet), static_cast<int>(mode));
  TEST_ASSERT_FALSE(stationModeFromName("8", &mode));
  TEST_ASSERT_FALSE(stationModeFromName("bogus", &mode));
  TEST_ASSERT_FALSE(stationModeFromName(nullptr, &mode));
}

void test_environment_view_cycles() {
  TEST_ASSERT_EQUAL_STRING("summary", envViewName(EnvView::kSummary));
  TEST_ASSERT_EQUAL_STRING("graph", envViewName(EnvView::kGraph));
  TEST_ASSERT_EQUAL_STRING("detail", envViewName(EnvView::kDetail));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(EnvView::kGraph),
                        static_cast<int>(nextEnvView(EnvView::kSummary)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(EnvView::kSummary),
                        static_cast<int>(nextEnvView(EnvView::kDetail)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(EnvView::kDetail),
                        static_cast<int>(previousEnvView(EnvView::kSummary)));
}

void test_controller_boots_into_the_pet() {
  StationController controller;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StationScreen::kMode),
                        static_cast<int>(controller.screen()));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StationMode::kPet),
                        static_cast<int>(controller.mode()));
  // BAK on the pet screen: the home menu opens with the pet selected.
  TEST_ASSERT_TRUE(controller.openMenu());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StationScreen::kMenu),
                        static_cast<int>(controller.screen()));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StationMode::kPet),
                        static_cast<int>(controller.menuSelection()));
  TEST_ASSERT_FALSE(controller.openMenu());
}

void test_controller_navigation() {
  StationController controller;
  TEST_ASSERT_TRUE(controller.selectMode(StationMode::kEnvironment));
  TEST_ASSERT_TRUE(controller.selectModeByName("menu"));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StationScreen::kMenu),
                        static_cast<int>(controller.screen()));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StationMode::kEnvironment),
                        static_cast<int>(controller.menuSelection()));

  controller.menuMove(-1);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StationMode::kPet),
                        static_cast<int>(controller.menuSelection()));
  controller.menuMove(1);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StationMode::kEnvironment),
                        static_cast<int>(controller.menuSelection()));
  controller.menuMove(3);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StationMode::kBle),
                        static_cast<int>(controller.menuSelection()));
  controller.menuMove(-3);
  controller.menuMove(1);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StationMode::kLogs),
                        static_cast<int>(controller.menuSelection()));

  TEST_ASSERT_TRUE(controller.enter());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StationScreen::kMode),
                        static_cast<int>(controller.screen()));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StationMode::kLogs),
                        static_cast<int>(controller.mode()));
  TEST_ASSERT_FALSE(controller.enter());

  TEST_ASSERT_TRUE(controller.back());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StationScreen::kMenu),
                        static_cast<int>(controller.screen()));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StationMode::kLogs),
                        static_cast<int>(controller.menuSelection()));
  TEST_ASSERT_FALSE(controller.back());

  TEST_ASSERT_TRUE(controller.selectModeIndex(4));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StationMode::kWifi),
                        static_cast<int>(controller.mode()));
  TEST_ASSERT_TRUE(controller.selectModeIndex(1));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StationMode::kPet),
                        static_cast<int>(controller.mode()));
  TEST_ASSERT_TRUE(controller.selectModeIndex(7));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StationMode::kSystem),
                        static_cast<int>(controller.mode()));
  TEST_ASSERT_FALSE(controller.selectModeIndex(0));
  TEST_ASSERT_FALSE(controller.selectModeIndex(8));

  TEST_ASSERT_TRUE(controller.selectModeByName("menu"));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StationScreen::kMenu),
                        static_cast<int>(controller.screen()));
  TEST_ASSERT_TRUE(controller.selectModeByName("BLE"));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StationMode::kBle),
                        static_cast<int>(controller.mode()));
  TEST_ASSERT_FALSE(controller.selectModeByName("bogus"));

  controller.cycleEnvView(1);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(EnvView::kGraph),
                        static_cast<int>(controller.envView()));
  controller.cycleEnvView(-1);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(EnvView::kSummary),
                        static_cast<int>(controller.envView()));
  controller.cycleEnvView(-1);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(EnvView::kDetail),
                        static_cast<int>(controller.envView()));
}

void test_pressure_trend_tracker_uses_the_window() {
  PressureTrendTracker tracker;
  tracker.update(1000.0f, 0);
  TEST_ASSERT_EQUAL_INT(0, tracker.trend());
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, tracker.deltaHpa());

  tracker.update(1000.5f, 1000);
  TEST_ASSERT_EQUAL_INT(0, tracker.trend());
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, tracker.deltaHpa());

  tracker.update(1001.6f, kPressureTrendWindowMs + 1);
  TEST_ASSERT_EQUAL_INT(1, tracker.trend());
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.6f, tracker.deltaHpa());

  tracker.update(999.0f, 2 * kPressureTrendWindowMs + 2);
  TEST_ASSERT_EQUAL_INT(-1, tracker.trend());
  TEST_ASSERT_FLOAT_WITHIN(0.001f, -2.6f, tracker.deltaHpa());

  tracker.reset();
  TEST_ASSERT_EQUAL_INT(0, tracker.trend());
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, tracker.deltaHpa());
}

void test_sensor_report_mentions_comfort_weather_and_alert() {
  StationView view = healthyView();
  view.sensors.temperatureC = 31.0f;
  view.sensors.humidityPct = 30.0f;
  view.sensors.pressureTrend = -1;
  view.pressureDeltaHpa = 2.0f;

  char buffer[512];
  formatStationSensorReport(view, buffer, sizeof(buffer));
  TEST_ASSERT_NOT_NULL(std::strstr(buffer, "bmp280: BMP280 at 0x76"));
  TEST_ASSERT_NOT_NULL(std::strstr(buffer, "comfort:"));
  TEST_ASSERT_NOT_NULL(std::strstr(buffer, "weather: rain"));
  TEST_ASSERT_NOT_NULL(std::strstr(buffer, "alert: HOT 31.0C"));
  TEST_ASSERT_NOT_NULL(std::strstr(buffer, "P JUMP"));
}

void test_log_status_report_includes_ntp_and_path() {
  StationView view = healthyView();
  char buffer[320];
  formatStationLogStatus(view, buffer, sizeof(buffer));
  TEST_ASSERT_NOT_NULL(std::strstr(buffer, "log: littlefs ready"));
  TEST_ASSERT_NOT_NULL(std::strstr(buffer, "interval: 1 min"));
  TEST_ASSERT_NOT_NULL(std::strstr(buffer, "timestamp: not synced"));
  TEST_ASSERT_NOT_NULL(std::strstr(buffer, "/log.csv"));

  view.ntpSynced = true;
  formatStationLogStatus(view, buffer, sizeof(buffer));
  TEST_ASSERT_NOT_NULL(std::strstr(buffer, "timestamp: ntp (local time)"));
}

void test_ble_report_lists_the_gatt_service() {
  StationView view = healthyView();
  view.bleEnabled = true;
  view.bleHidMode = true;
  view.bleConnected = true;
  char buffer[384];
  formatStationBleReport(view, buffer, sizeof(buffer));
  TEST_ASSERT_NOT_NULL(std::strstr(buffer, "ble: enabled"));
  TEST_ASSERT_NOT_NULL(std::strstr(buffer, "hid: media remote on"));
  TEST_ASSERT_NOT_NULL(std::strstr(buffer, "0x181A"));
  TEST_ASSERT_NOT_NULL(std::strstr(buffer, "link: connected"));
}

void test_navigation_report() {
  StationController controller;
  controller.selectModeByName("wifi");
  char buffer[128];
  formatStationNavigation(controller, buffer, sizeof(buffer));
  TEST_ASSERT_EQUAL_STRING("screen=mode mode=wifi selection=wifi view=summary\n", buffer);
}

void test_graph_screen_draws_both_series() {
  GraphBuffer temperature(64);
  GraphBuffer pressure(64);
  for (int index = 0; index < 40; ++index) {
    temperature.push(20.0f + static_cast<float>(index) * 0.1f);
    pressure.push(1000.0f + static_cast<float>(index) * 0.05f);
  }

  StationView view = healthyView();
  view.temperatureGraph = &temperature;
  view.pressureGraph = &pressure;

  Framebuffer framebuffer;
  buildStationGraphScreen(view, &framebuffer);

  size_t topPixels = 0;
  size_t bottomPixels = 0;
  for (int y = 9; y < 34; ++y) {
    for (int x = 0; x < Framebuffer::kWidth; ++x) {
      topPixels += framebuffer.pixel(x, y) ? 1 : 0;
    }
  }
  for (int y = 36; y < Framebuffer::kHeight; ++y) {
    for (int x = 0; x < Framebuffer::kWidth; ++x) {
      bottomPixels += framebuffer.pixel(x, y) ? 1 : 0;
    }
  }
  TEST_ASSERT_TRUE(topPixels > 100);
  TEST_ASSERT_TRUE(bottomPixels > 100);
}

void test_graph_screen_without_samples_says_so() {
  StationView view = healthyView();
  Framebuffer framebuffer;
  buildStationGraphScreen(view, &framebuffer);

  size_t dark = 0;
  for (int y = 20; y < 40; ++y) {
    for (int x = 0; x < Framebuffer::kWidth; ++x) {
      dark += framebuffer.pixel(x, y) ? 1 : 0;
    }
  }
  TEST_ASSERT_TRUE(dark > 0);
}


void test_wifi_qr_closes_before_leaving_the_mode() {
  // The v0.4.0 report: 4 WIFI & WEB -> CON (QR) -> BAK -> 4 again and the page
  // could never leave the QR. The overlay now resets on every entry and closes
  // on the first BAK.
  StationController controller;
  WifiQrState qr;

  TEST_ASSERT_TRUE(controller.selectMode(StationMode::kWifi));
  qr.enterMode();
  TEST_ASSERT_FALSE(qr.visible());

  qr.show();
  TEST_ASSERT_TRUE(qr.visible());

  // First BAK: the code closes and the Wi-Fi status page stays up.
  TEST_ASSERT_TRUE(qr.closeOnBack());
  TEST_ASSERT_FALSE(qr.visible());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StationScreen::kMode),
                        static_cast<int>(controller.screen()));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StationMode::kWifi),
                        static_cast<int>(controller.mode()));

  // Second BAK: the menu opens with the Wi-Fi entry selected.
  TEST_ASSERT_FALSE(qr.closeOnBack());
  qr.leaveMode();
  TEST_ASSERT_TRUE(controller.back());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StationScreen::kMenu),
                        static_cast<int>(controller.screen()));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StationMode::kWifi),
                        static_cast<int>(controller.menuSelection()));

  // Re-entering shows the status page, not the old code.
  TEST_ASSERT_TRUE(controller.enter());
  qr.enterMode();
  TEST_ASSERT_FALSE(qr.visible());
  TEST_ASSERT_FALSE(qr.closeOnBack());

  StationView view = healthyView();
  view.wifiMode = WifiMode::kStation;
  view.wifiConnected = true;
  std::snprintf(view.wifiSsid, sizeof(view.wifiSsid), "Home");
  std::snprintf(view.ip, sizeof(view.ip), "192.168.1.42");
  view.qrVisible = qr.visible();
  PageLines page;
  buildStationPage(StationMode::kWifi, view, &page);
  TEST_ASSERT_EQUAL_STRING("WIFI STATION", page.lines[0].text);
  TEST_ASSERT_TRUE(pageHas(page, "CON show QR"));
  TEST_ASSERT_TRUE(pageHas(page, "PSH AP/STA  BAK back"));
}

void test_wifi_qr_resets_on_entry_exit_and_mode_switch() {
  WifiQrState qr;
  // Leaving the mode hides the code even when no BAK was pressed there.
  qr.show();
  qr.leaveMode();
  TEST_ASSERT_FALSE(qr.visible());

  // Entering the mode again always starts on the status page.
  qr.show();
  qr.enterMode();
  TEST_ASSERT_FALSE(qr.visible());

  // PSH (setup AP <-> station) shows the status page of the new mode.
  qr.show();
  qr.onModeSwitch();
  TEST_ASSERT_FALSE(qr.visible());

  // hide() is what the 60 second timeout uses.
  qr.show();
  qr.hide();
  TEST_ASSERT_FALSE(qr.visible());
  TEST_ASSERT_FALSE(qr.closeOnBack());
}

void test_wifi_qr_shows_only_while_it_is_up() {
  WifiQrState qr;
  TEST_ASSERT_FALSE(qr.visible());
  qr.enterMode();
  qr.show();
  TEST_ASSERT_TRUE(qr.visible());
  TEST_ASSERT_TRUE(qr.closeOnBack());
  TEST_ASSERT_FALSE(qr.visible());
}

}  // namespace

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_menu_lists_all_seven_modes_with_cursor);
  RUN_TEST(test_menu_marks_the_selected_pet_entry);
  RUN_TEST(test_pet_page_reports_the_animation_state);
  RUN_TEST(test_pet_page_without_a_scene_falls_back_to_text);
  RUN_TEST(test_controller_boots_into_the_pet);
  RUN_TEST(test_environment_summary_shows_every_reading);
  RUN_TEST(test_environment_summary_handles_missing_sensors);
  RUN_TEST(test_environment_summary_shows_alerts);
  RUN_TEST(test_environment_detail_shows_trend_and_comfort);
  RUN_TEST(test_logs_page_reports_ntp_and_file);
  RUN_TEST(test_wifi_page_station_and_setup);
  RUN_TEST(test_wifi_qr_closes_before_leaving_the_mode);
  RUN_TEST(test_wifi_qr_resets_on_entry_exit_and_mode_switch);
  RUN_TEST(test_wifi_qr_shows_only_while_it_is_up);
  RUN_TEST(test_ble_page_reflects_hid_mode);
  RUN_TEST(test_timer_page_has_big_readout_and_progress);
  RUN_TEST(test_system_page_lists_links_and_storage);
  RUN_TEST(test_mode_names_and_cycle);
  RUN_TEST(test_mode_names_parse);
  RUN_TEST(test_environment_view_cycles);
  RUN_TEST(test_controller_navigation);
  RUN_TEST(test_pressure_trend_tracker_uses_the_window);
  RUN_TEST(test_sensor_report_mentions_comfort_weather_and_alert);
  RUN_TEST(test_log_status_report_includes_ntp_and_path);
  RUN_TEST(test_ble_report_lists_the_gatt_service);
  RUN_TEST(test_navigation_report);
  RUN_TEST(test_graph_screen_draws_both_series);
  RUN_TEST(test_graph_screen_without_samples_says_so);
  return UNITY_END();
}

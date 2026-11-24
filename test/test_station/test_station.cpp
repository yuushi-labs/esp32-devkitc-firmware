#include <unity.h>

#include <cstring>

#include "station/station_model.h"

void setUp() {}
void tearDown() {}

namespace {

using fw::PageLines;
using fw::PressureTrendTracker;
using fw::SensorSnapshot;
using fw::StationController;
using fw::StationPage;
using fw::StationView;

StationView makeView() {
  StationView view;
  view.version = "0.2.0";
  view.sensors.bmp280Present = true;
  view.sensors.bmp280Chip = "BMP280";
  view.sensors.bmp280Address = 0x76;
  view.sensors.temperatureValid = true;
  view.sensors.temperatureC = 23.44f;
  view.sensors.pressureValid = true;
  view.sensors.pressureHpa = 1006.532f;
  view.sensors.pressureTrend = 1;
  view.sensors.dhtPresent = true;
  view.sensors.dhtType = fw::DhtType::kDht22;
  view.sensors.humidityValid = true;
  view.sensors.humidityPct = 45.04f;
  view.sensors.dhtTemperatureC = 23.1f;

  view.loggingEnabled = true;
  view.logStorageReady = true;
  view.logRecordCount = 12;
  view.logIntervalMs = 60000;
  view.logIntervalRemainingMs = 42000;
  view.logFileBytes = 1234;
  view.oledAddress = 0x3C;
  std::strcpy(view.i2cAddresses, "0x3C,0x76");
  view.uptimeMs = 3723000;
  view.freeHeapBytes = 123456;
  view.dhtPin = 4;
  return view;
}

void assertContains(const char* haystack, const char* needle) {
  TEST_ASSERT_NOT_NULL_MESSAGE(std::strstr(haystack, needle), needle);
}

void test_page_names_and_rotation() {
  TEST_ASSERT_EQUAL_STRING("summary", fw::stationPageName(StationPage::kSummary));
  TEST_ASSERT_EQUAL_STRING("sensors", fw::stationPageName(StationPage::kSensorDetail));
  TEST_ASSERT_EQUAL_STRING("logs", fw::stationPageName(StationPage::kLogs));
  TEST_ASSERT_EQUAL_STRING("system", fw::stationPageName(StationPage::kSystem));

  TEST_ASSERT_EQUAL_INT(static_cast<int>(StationPage::kSensorDetail),
                        static_cast<int>(fw::nextStationPage(StationPage::kSummary)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StationPage::kSummary),
                        static_cast<int>(fw::nextStationPage(StationPage::kSystem)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StationPage::kSystem),
                        static_cast<int>(fw::previousStationPage(StationPage::kSummary)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StationPage::kLogs),
                        static_cast<int>(fw::previousStationPage(StationPage::kSystem)));
}

void test_summary_page_shows_the_readings() {
  PageLines page;
  fw::buildStationPage(StationPage::kSummary, makeView(), &page);

  TEST_ASSERT_EQUAL_UINT32(5, static_cast<uint32_t>(page.count));
  TEST_ASSERT_EQUAL_STRING("ENV STATION", page.lines[0].text);
  TEST_ASSERT_EQUAL_UINT8(2, page.lines[1].scale);
  assertContains(page.lines[1].text, "23.4");
  assertContains(page.lines[1].text, "C");
  assertContains(page.lines[2].text, "1006.5 hPa");
  assertContains(page.lines[3].text, "45.0 %");
  assertContains(page.lines[3].text, "DHT22");
  assertContains(page.lines[4].text, "REC 12");
  TEST_ASSERT_TRUE(page.height() <= 64);
}

void test_summary_page_marks_a_pause_and_a_missing_sensor() {
  StationView view;
  view.sensors.bmp280Present = false;
  view.loggingEnabled = false;
  view.logRecordCount = 3;

  PageLines page;
  fw::buildStationPage(StationPage::kSummary, view, &page);

  assertContains(page.lines[1].text, "--.-");
  TEST_ASSERT_EQUAL_STRING("---.- hPa", page.lines[2].text);
  TEST_ASSERT_EQUAL_STRING("humidity: none", page.lines[3].text);
  assertContains(page.lines[4].text, "PAUSED 3");
}

void test_detail_page_reports_the_bmp280_and_dht() {
  PageLines page;
  fw::buildStationPage(StationPage::kSensorDetail, makeView(), &page);

  TEST_ASSERT_EQUAL_STRING("BMP280 0x76", page.lines[0].text);
  assertContains(page.lines[1].text, "T 23.44");
  assertContains(page.lines[2].text, "P 1006.53 hPa");
  TEST_ASSERT_EQUAL_STRING("trend rising", page.lines[3].text);
  assertContains(page.lines[4].text, "DHT22 45.0 %");
  assertContains(page.lines[5].text, "poll 2.0 s");
  TEST_ASSERT_EQUAL_STRING("trend rising", page.lines[3].text);
}

void test_detail_page_handles_missing_parts() {
  StationView view;
  view.sensors.dhtPresent = true;  // answered but produced no valid frame
  view.sensors.temperatureValid = false;
  view.sensors.pressureValid = false;

  PageLines page;
  fw::buildStationPage(StationPage::kSensorDetail, view, &page);

  TEST_ASSERT_EQUAL_STRING("BMP280 not found", page.lines[0].text);
  TEST_ASSERT_EQUAL_STRING("T --.--", page.lines[1].text);
  TEST_ASSERT_EQUAL_STRING("P ------ hPa", page.lines[2].text);
  TEST_ASSERT_EQUAL_STRING("trend steady", page.lines[3].text);
  TEST_ASSERT_EQUAL_STRING("DHT no answer", page.lines[4].text);
}

void test_logs_page_shows_state_and_interval() {
  PageLines page;
  fw::buildStationPage(StationPage::kLogs, makeView(), &page);

  TEST_ASSERT_EQUAL_STRING("LOG littlefs", page.lines[0].text);
  TEST_ASSERT_EQUAL_STRING("state REC", page.lines[1].text);
  TEST_ASSERT_EQUAL_STRING("records 12", page.lines[2].text);
  TEST_ASSERT_EQUAL_STRING("every 1 min", page.lines[3].text);
  TEST_ASSERT_EQUAL_STRING("next 42 s", page.lines[4].text);
  TEST_ASSERT_EQUAL_STRING("size 1234 B", page.lines[5].text);
  TEST_ASSERT_EQUAL_STRING("/log.csv", page.lines[6].text);
  TEST_ASSERT_TRUE(page.height() <= 64);
}

void test_logs_page_handles_a_missing_filesystem() {
  StationView view;
  view.loggingEnabled = false;
  view.logStorageReady = false;

  PageLines page;
  fw::buildStationPage(StationPage::kLogs, view, &page);

  TEST_ASSERT_EQUAL_STRING("state PAUSED", page.lines[1].text);
  TEST_ASSERT_EQUAL_STRING("next -- s", page.lines[4].text);
  TEST_ASSERT_EQUAL_STRING("fs not ready", page.lines[6].text);
}

void test_system_page_lists_the_bus_and_the_build() {
  PageLines page;
  fw::buildStationPage(StationPage::kSystem, makeView(), &page);

  TEST_ASSERT_EQUAL_STRING("SYSTEM v0.2.0", page.lines[0].text);
  TEST_ASSERT_EQUAL_STRING("up 1h02m03s", page.lines[1].text);
  TEST_ASSERT_EQUAL_STRING("heap 123456 B", page.lines[2].text);
  TEST_ASSERT_EQUAL_STRING("i2c 0x3C,0x76", page.lines[3].text);
  TEST_ASSERT_EQUAL_STRING("oled 0x3C", page.lines[4].text);
  TEST_ASSERT_EQUAL_STRING("sensor BMP280", page.lines[5].text);
  TEST_ASSERT_EQUAL_STRING("log ready", page.lines[6].text);
}

void test_pressure_trend_uses_the_window() {
  PressureTrendTracker tracker;
  tracker.update(1000.0f, 0);
  TEST_ASSERT_EQUAL_INT8(0, tracker.trend());

  tracker.update(1000.5f, 10000);  // inside the window, nothing decided yet
  TEST_ASSERT_EQUAL_INT8(0, tracker.trend());

  tracker.update(1000.5f, 30000);
  TEST_ASSERT_EQUAL_INT8(1, tracker.trend());

  tracker.update(999.9f, 60000);
  TEST_ASSERT_EQUAL_INT8(-1, tracker.trend());

  tracker.update(999.95f, 90000);
  TEST_ASSERT_EQUAL_INT8(0, tracker.trend());

  tracker.reset();
  TEST_ASSERT_EQUAL_INT8(0, tracker.trend());
}

void test_controller_covers_every_page() {
  StationController controller;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StationPage::kSummary),
                        static_cast<int>(controller.page()));

  TEST_ASSERT_EQUAL_INT(static_cast<int>(StationPage::kSensorDetail),
                        static_cast<int>(controller.nextPage()));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StationPage::kLogs),
                        static_cast<int>(controller.nextPage()));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StationPage::kSensorDetail),
                        static_cast<int>(controller.previousPage()));

  TEST_ASSERT_TRUE(controller.selectPage(4));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StationPage::kSystem),
                        static_cast<int>(controller.page()));
  TEST_ASSERT_TRUE(controller.selectPage(1));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StationPage::kSummary),
                        static_cast<int>(controller.page()));

  TEST_ASSERT_FALSE(controller.selectPage(0));
  TEST_ASSERT_FALSE(controller.selectPage(5));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(StationPage::kSummary),
                        static_cast<int>(controller.page()));
}

void test_sensor_report_lists_every_value() {
  const StationView view = makeView();
  char out[512];
  const size_t needed = fw::formatStationSensorReport(view, out, sizeof(out));

  assertContains(out, "bmp280: BMP280 at 0x76");
  assertContains(out, "temperature: 23.44 C");
  assertContains(out, "pressure: 1006.53 hPa (rising)");
  assertContains(out, "humidity: 45.0 % (DHT22, 23.1 C)");
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(std::strlen(out)),
                           static_cast<uint32_t>(needed));
}

void test_sensor_report_handles_missing_parts() {
  StationView view;
  view.dhtPin = 4;
  view.sensors.dhtPresent = true;

  char out[512];
  fw::formatStationSensorReport(view, out, sizeof(out));
  assertContains(out, "bmp280: not found on 0x76/0x77");
  assertContains(out, "temperature: unavailable");
  assertContains(out, "pressure: unavailable");
  assertContains(out, "humidity: sensor answered but no valid frame");
}

void test_log_status_report() {
  const StationView view = makeView();
  char out[256];
  fw::formatStationLogStatus(view, out, sizeof(out));

  assertContains(out, "log: littlefs ready");
  assertContains(out, "state: recording");
  assertContains(out, "interval: 1 min");
  assertContains(out, "records: 12");
  assertContains(out, "size: 1234 B");
  assertContains(out, "file: /log.csv");
}

}  // namespace

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_page_names_and_rotation);
  RUN_TEST(test_summary_page_shows_the_readings);
  RUN_TEST(test_summary_page_marks_a_pause_and_a_missing_sensor);
  RUN_TEST(test_detail_page_reports_the_bmp280_and_dht);
  RUN_TEST(test_detail_page_handles_missing_parts);
  RUN_TEST(test_logs_page_shows_state_and_interval);
  RUN_TEST(test_logs_page_handles_a_missing_filesystem);
  RUN_TEST(test_system_page_lists_the_bus_and_the_build);
  RUN_TEST(test_pressure_trend_uses_the_window);
  RUN_TEST(test_controller_covers_every_page);
  RUN_TEST(test_sensor_report_lists_every_value);
  RUN_TEST(test_sensor_report_handles_missing_parts);
  RUN_TEST(test_log_status_report);
  return UNITY_END();
}

#include <unity.h>

#include <cstring>

#include "app_config.h"
#include "tester/tester_model.h"

void setUp() {}
void tearDown() {}

namespace {

using fw::Bmp280Status;
using fw::DhtType;
using fw::PageLines;
using fw::TesterInputs;
using fw::TesterPage;
using fw::TesterSensors;

void addDevice(fw::I2cScanResult& scan, uint8_t address, fw::I2cDeviceKind kind) {
  scan.entries[scan.count].address = address;
  scan.entries[scan.count].kind = kind;
  ++scan.count;
}

TesterSensors makeSensors() {
  TesterSensors sensors;
  addDevice(sensors.scan, 0x3C, fw::I2cDeviceKind::kSsd1306);
  addDevice(sensors.scan, 0x76, fw::I2cDeviceKind::kBmp280);
  sensors.oledPresent = true;
  sensors.oledAddress = 0x3C;
  sensors.bmp280Status = Bmp280Status::kOk;
  sensors.bmp280Address = 0x76;
  sensors.bmp280Chip = "BMP280";
  sensors.bmp280ReadingValid = true;
  sensors.temperatureC = 23.45f;
  sensors.pressureHpa = 1006.53f;
  sensors.dhtPresent = true;
  sensors.dhtType = DhtType::kDht22;
  sensors.dhtReadingValid = true;
  sensors.humidityPct = 45.0f;
  sensors.dhtTemperatureC = 23.1f;
  return sensors;
}

TesterInputs makeInputs() {
  TesterInputs inputs;
  inputs.encoderPosition = 5;
  inputs.pushPresses = 3;
  inputs.pushDown = true;
  inputs.backPresses = 0;
  inputs.confirmPresses = 1;
  inputs.confirmDown = true;
  return inputs;
}

void assertContains(const char* haystack, const char* needle) {
  TEST_ASSERT_NOT_NULL_MESSAGE(std::strstr(haystack, needle), needle);
}

void test_page_names_and_rotation() {
  TEST_ASSERT_EQUAL_STRING("inputs", fw::testerPageName(TesterPage::kInputs));
  TEST_ASSERT_EQUAL_STRING("i2c", fw::testerPageName(TesterPage::kScan));
  TEST_ASSERT_EQUAL_STRING("sensors", fw::testerPageName(TesterPage::kSensors));

  TEST_ASSERT_EQUAL_INT(static_cast<int>(TesterPage::kScan),
                        static_cast<int>(fw::nextTesterPage(TesterPage::kInputs)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(TesterPage::kInputs),
                        static_cast<int>(fw::nextTesterPage(TesterPage::kSensors)));
}

void test_button_names() {
  TEST_ASSERT_EQUAL_STRING("PSH", fw::testerButtonName(0));
  TEST_ASSERT_EQUAL_STRING("BAK", fw::testerButtonName(1));
  TEST_ASSERT_EQUAL_STRING("CON", fw::testerButtonName(2));
  TEST_ASSERT_EQUAL_STRING("?", fw::testerButtonName(3));
}

void test_input_page_shows_every_input() {
  PageLines page;
  fw::buildTesterPage(TesterPage::kInputs, makeSensors(), makeInputs(), &page);

  TEST_ASSERT_EQUAL_UINT32(8, static_cast<uint32_t>(page.count));
  TEST_ASSERT_EQUAL_STRING("TESTER v" FW_VERSION, page.lines[0].text);
  TEST_ASSERT_EQUAL_STRING("ENC +5", page.lines[1].text);
  TEST_ASSERT_EQUAL_STRING("PSH ON 3", page.lines[2].text);
  TEST_ASSERT_EQUAL_STRING("BAK -- 0", page.lines[3].text);
  TEST_ASSERT_EQUAL_STRING("CON ON 1", page.lines[4].text);
  assertContains(page.lines[5].text, "BMP 23.5");
  TEST_ASSERT_EQUAL_STRING("DHT22 45.0 %", page.lines[6].text);
  TEST_ASSERT_EQUAL_STRING("I2C 2 dev", page.lines[7].text);
  TEST_ASSERT_TRUE(page.height() <= 64);
}

void test_input_page_handles_negative_rotation_and_missing_parts() {
  TesterSensors sensors;
  TesterInputs inputs;
  inputs.encoderPosition = -12;

  PageLines page;
  fw::buildTesterPage(TesterPage::kInputs, sensors, inputs, &page);

  TEST_ASSERT_EQUAL_STRING("ENC -12", page.lines[1].text);
  TEST_ASSERT_EQUAL_STRING("BMP not found", page.lines[5].text);
  TEST_ASSERT_EQUAL_STRING("DHT no answer", page.lines[6].text);
  TEST_ASSERT_EQUAL_STRING("I2C 0 dev", page.lines[7].text);
}

void test_scan_page_lists_every_address() {
  PageLines page;
  fw::buildTesterPage(TesterPage::kScan, makeSensors(), makeInputs(), &page);

  TEST_ASSERT_EQUAL_STRING("I2C SCAN 2", page.lines[0].text);
  TEST_ASSERT_EQUAL_STRING("0x3C SSD1306 OLED", page.lines[1].text);
  TEST_ASSERT_EQUAL_STRING("0x76 BMP280", page.lines[2].text);
}

void test_scan_page_explains_an_empty_bus() {
  TesterSensors sensors;
  PageLines page;
  fw::buildTesterPage(TesterPage::kScan, sensors, makeInputs(), &page);

  TEST_ASSERT_EQUAL_STRING("I2C SCAN 0", page.lines[0].text);
  TEST_ASSERT_EQUAL_STRING("no device answered", page.lines[1].text);
  assertContains(page.lines[2].text, "SDA21/SCL22");
}

void test_scan_page_stops_at_the_panel_height() {
  TesterSensors sensors;
  for (uint8_t index = 0; index < 12; ++index) {
    addDevice(sensors.scan, static_cast<uint8_t>(0x40 + index),
              fw::I2cDeviceKind::kOther);
  }

  PageLines page;
  fw::buildTesterPage(TesterPage::kScan, sensors, makeInputs(), &page);
  TEST_ASSERT_EQUAL_UINT32(PageLines::kMaxLines, static_cast<uint32_t>(page.count));
}

void test_sensor_page_shows_readings() {
  PageLines page;
  fw::buildTesterPage(TesterPage::kSensors, makeSensors(), makeInputs(), &page);

  TEST_ASSERT_EQUAL_STRING("BMP280 0x76", page.lines[0].text);
  assertContains(page.lines[1].text, "T 23.45");
  assertContains(page.lines[2].text, "P 1006.53 hPa");
  TEST_ASSERT_EQUAL_STRING("DHT22 ok", page.lines[3].text);
  TEST_ASSERT_EQUAL_STRING("RH 45.0 %", page.lines[4].text);
  assertContains(page.lines[5].text, "T 23.1");
}

void test_sensor_page_reports_failures() {
  TesterSensors sensors;
  sensors.bmp280Status = Bmp280Status::kBadChipId;
  sensors.dhtPresent = true;

  PageLines page;
  fw::buildTesterPage(TesterPage::kSensors, sensors, makeInputs(), &page);

  TEST_ASSERT_EQUAL_STRING("BMP280 bad id", page.lines[0].text);
  TEST_ASSERT_EQUAL_STRING("T --.--", page.lines[1].text);
  TEST_ASSERT_EQUAL_STRING("DHT no answer", page.lines[3].text);
}

void test_serial_sensor_report() {
  char out[512];
  const size_t needed = fw::formatTesterSensorReport(makeSensors(), out, sizeof(out));

  assertContains(out, "sensors:\n");
  assertContains(out, "bmp280: BMP280 at 0x76");
  assertContains(out, "bmp280: T=23.45 C P=1006.53 hPa");
  assertContains(out, "dht: DHT22 RH=45.0 % T=23.1 C");
  assertContains(out, "oled: found");
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(std::strlen(out)),
                           static_cast<uint32_t>(needed));
}

void test_serial_sensor_report_without_parts() {
  TesterSensors sensors;
  char out[512];
  fw::formatTesterSensorReport(sensors, out, sizeof(out));

  assertContains(out, "bmp280: not found");
  assertContains(out, "dht: not found");
  assertContains(out, "oled: not found");
}

void test_serial_input_report() {
  char out[256];
  const size_t needed = fw::formatTesterInputReport(makeInputs(), out, sizeof(out));

  assertContains(out, "inputs:\n");
  assertContains(out, "encoder: +5");
  assertContains(out, "PSH: ON  presses=3");
  assertContains(out, "BAK: --  presses=0");
  assertContains(out, "CON: ON  presses=1");
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(std::strlen(out)),
                           static_cast<uint32_t>(needed));
}

}  // namespace

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_page_names_and_rotation);
  RUN_TEST(test_button_names);
  RUN_TEST(test_input_page_shows_every_input);
  RUN_TEST(test_input_page_handles_negative_rotation_and_missing_parts);
  RUN_TEST(test_scan_page_lists_every_address);
  RUN_TEST(test_scan_page_explains_an_empty_bus);
  RUN_TEST(test_scan_page_stops_at_the_panel_height);
  RUN_TEST(test_sensor_page_shows_readings);
  RUN_TEST(test_sensor_page_reports_failures);
  RUN_TEST(test_serial_sensor_report);
  RUN_TEST(test_serial_sensor_report_without_parts);
  RUN_TEST(test_serial_input_report);
  return UNITY_END();
}

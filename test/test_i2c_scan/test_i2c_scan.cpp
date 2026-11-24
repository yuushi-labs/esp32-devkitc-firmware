#include <unity.h>

#include <cstring>

#include "i2c_scan.h"
#include "test_support/fake_i2c.h"

void setUp() {}
void tearDown() {}

namespace {

using fw::I2cDeviceKind;
using fw::I2cScanResult;
using fw::test::FakeI2cTransport;

void test_empty_bus_reports_no_device() {
  FakeI2cTransport bus;
  const I2cScanResult scan = fw::scanI2cBus(bus);

  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(scan.count));
  TEST_ASSERT_TRUE(scan.empty());

  char out[128];
  const size_t needed = fw::formatI2cScan(scan, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("i2c scan: no device answered\n", out);
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(std::strlen(out)),
                           static_cast<uint32_t>(needed));
}

void test_scan_names_the_oled_and_the_bmp280() {
  FakeI2cTransport bus;
  bus.addDevice(0x3C);
  bus.addDevice(0x76);
  bus.setRegister(0x76, 0xD0, 0x58);

  const I2cScanResult scan = fw::scanI2cBus(bus);

  TEST_ASSERT_EQUAL_UINT32(2, static_cast<uint32_t>(scan.count));
  TEST_ASSERT_TRUE(scan.contains(0x3C));
  TEST_ASSERT_TRUE(scan.contains(0x76));
  TEST_ASSERT_TRUE(scan.containsKind(I2cDeviceKind::kSsd1306));
  TEST_ASSERT_TRUE(scan.containsKind(I2cDeviceKind::kBmp280));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(I2cDeviceKind::kSsd1306),
                        static_cast<int>(scan.entries[0].kind));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(I2cDeviceKind::kBmp280),
                        static_cast<int>(scan.entries[1].kind));

  char out[256];
  fw::formatI2cScan(scan, out, sizeof(out));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "i2c scan: 2 device(s)\n"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "  0x3C SSD1306 OLED\n"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "  0x76 BMP280\n"));
}

void test_alternate_addresses_are_recognised() {
  FakeI2cTransport bus;
  bus.addDevice(0x3D);
  bus.addDevice(0x77);
  bus.setRegister(0x77, 0xD0, 0x60);

  const I2cScanResult scan = fw::scanI2cBus(bus);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(I2cDeviceKind::kSsd1306),
                        static_cast<int>(scan.entries[0].kind));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(I2cDeviceKind::kBme280),
                        static_cast<int>(scan.entries[1].kind));
}

void test_unknown_device_is_reported_as_such() {
  FakeI2cTransport bus;
  bus.addDevice(0x68);  // RTC or IMU, not our parts
  bus.addDevice(0x76);  // answers but with an unexpected chip id
  bus.setRegister(0x76, 0xD0, 0x00);

  const I2cScanResult scan = fw::scanI2cBus(bus);
  TEST_ASSERT_EQUAL_UINT32(2, static_cast<uint32_t>(scan.count));
  TEST_ASSERT_EQUAL_STRING("unknown device",
                           fw::i2cDeviceKindName(scan.entries[0].kind));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(I2cDeviceKind::kOther),
                        static_cast<int>(scan.entries[1].kind));

  char out[128];
  fw::formatI2cEntry(scan.entries[0], out, sizeof(out));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "0x68"));
}

void test_scan_stops_at_the_entry_limit() {
  FakeI2cTransport bus;
  for (uint16_t address = 0x10; address < 0x40; ++address) {
    bus.addDevice(static_cast<uint8_t>(address));
  }

  const I2cScanResult scan = fw::scanI2cBus(bus);
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(I2cScanResult::kMaxEntries),
                           static_cast<uint32_t>(scan.count));
}

void test_address_list_is_compact() {
  FakeI2cTransport bus;
  bus.addDevice(0x3C);
  bus.addDevice(0x76);
  bus.setRegister(0x76, 0xD0, 0x58);
  const I2cScanResult scan = fw::scanI2cBus(bus);

  char out[48];
  fw::formatI2cAddressList(scan, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("0x3C,0x76", out);

  // The needed length is reported for a zero sized destination too.
  TEST_ASSERT_EQUAL_UINT32(9, static_cast<uint32_t>(
                                  fw::formatI2cAddressList(scan, nullptr, 0)));

  char empty[16];
  fw::formatI2cAddressList(I2cScanResult{}, empty, sizeof(empty));
  TEST_ASSERT_EQUAL_STRING("none", empty);
  TEST_ASSERT_EQUAL_UINT32(4, static_cast<uint32_t>(
                                  fw::formatI2cAddressList(I2cScanResult{}, nullptr, 0)));
}

void test_truncated_scan_output_is_nul_terminated() {
  FakeI2cTransport bus;
  bus.addDevice(0x3C);
  bus.addDevice(0x76);
  bus.setRegister(0x76, 0xD0, 0x58);
  const I2cScanResult scan = fw::scanI2cBus(bus);

  char out[12];
  const size_t needed = fw::formatI2cScan(scan, out, sizeof(out));
  TEST_ASSERT_TRUE(needed > sizeof(out) - 1);
  TEST_ASSERT_EQUAL_UINT32(sizeof(out) - 1, static_cast<uint32_t>(std::strlen(out)));
  TEST_ASSERT_EQUAL_STRING("i2c scan: 2", out);
}

}  // namespace

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_empty_bus_reports_no_device);
  RUN_TEST(test_scan_names_the_oled_and_the_bmp280);
  RUN_TEST(test_alternate_addresses_are_recognised);
  RUN_TEST(test_unknown_device_is_reported_as_such);
  RUN_TEST(test_scan_stops_at_the_entry_limit);
  RUN_TEST(test_address_list_is_compact);
  RUN_TEST(test_truncated_scan_output_is_nul_terminated);
  return UNITY_END();
}

#include <unity.h>

#include <cstring>
#include <string>

#include "app_config.h"
#include "command_handler.h"

void setUp() {}
void tearDown() {}

namespace {

fw::DeviceStatus makeStatus() {
  fw::DeviceStatus status{};
  status.version = FW_VERSION;
  status.chipModel = "ESP32-D0WD";
  status.chipRevision = 3;
  status.chipId = 0x001122334455ULL;
  status.uptimeMs = 1234;
  status.freeHeap = 23456;
  status.ledMode = fw::LedMode::kBlink;
  status.resetReason = "power-on";
  return status;
}

void assertContains(const char* haystack, const char* needle) {
  TEST_ASSERT_NOT_NULL_MESSAGE(std::strstr(haystack, needle), needle);
}

void test_led_mode_names() {
  TEST_ASSERT_EQUAL_STRING("blink", fw::ledModeName(fw::LedMode::kBlink));
  TEST_ASSERT_EQUAL_STRING("on", fw::ledModeName(fw::LedMode::kOn));
  TEST_ASSERT_EQUAL_STRING("off", fw::ledModeName(fw::LedMode::kOff));
}

void test_heartbeat_line_is_stable() {
  char out[128];
  const size_t needed = fw::formatHeartbeat(makeStatus(), out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING(
      "heartbeat uptime_ms=1234 free_heap=23456 led_mode=blink\n", out);
  TEST_ASSERT_EQUAL_UINT32(std::strlen(out), static_cast<uint32_t>(needed));
}

void test_info_reports_device_and_firmware_fields() {
  char out[384];
  fw::formatInfo(makeStatus(), out, sizeof(out));
  assertContains(out, "fw_version=" FW_VERSION "\n");
  assertContains(out, "board=" FW_BOARD "\n");
  assertContains(out, "chip_model=ESP32-D0WD chip_revision=3\n");
  assertContains(out, "chip_id=0x001122334455\n");
  assertContains(out, "reset_reason=power-on\n");
  assertContains(out, "uptime_ms=1234\n");
  assertContains(out, "free_heap=23456\n");
  assertContains(out, "led_mode=blink\n");
}

void test_info_tracks_the_active_led_mode() {
  fw::DeviceStatus status = makeStatus();
  status.ledMode = fw::LedMode::kOff;

  char out[384];
  fw::formatInfo(status, out, sizeof(out));
  assertContains(out, "led_mode=off\n");
}

void test_banner_reports_version_and_help_hint() {
  char out[256];
  fw::formatBanner(makeStatus(), out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING_LEN("esp32-devkitc-firmware v", out, 24);
  assertContains(out, "v" FW_VERSION " (" FW_BOARD ")");
  assertContains(out, "chip_model=ESP32-D0WD chip_revision=3 "
                      "chip_id=0x001122334455\n");
  assertContains(out, "reset_reason=power-on\n");
  assertContains(out, "type 'help' for commands\n");
}

void test_truncated_output_is_nul_terminated_and_reports_needed_length() {
  char out[8];
  const size_t needed = fw::formatHeartbeat(makeStatus(), out, sizeof(out));

  TEST_ASSERT_TRUE(needed > sizeof(out) - 1);
  TEST_ASSERT_EQUAL_UINT32(sizeof(out) - 1, static_cast<uint32_t>(std::strlen(out)));
  TEST_ASSERT_EQUAL_STRING_LEN("heartbe", out, 7);
}

void test_zero_sized_buffer_reports_length_without_writing() {
  const char* expected =
      "heartbeat uptime_ms=1234 free_heap=23456 led_mode=blink\n";
  const size_t needed = fw::formatHeartbeat(makeStatus(), nullptr, 0);
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(std::strlen(expected)),
                           static_cast<uint32_t>(needed));
}

void test_long_version_reports_the_untruncated_length() {
  std::string longVersion(500, 'v');
  fw::DeviceStatus status = makeStatus();
  status.version = longVersion.c_str();

  char out[1024];
  const size_t needed = fw::formatInfo(status, out, sizeof(out));
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(std::strlen(out)),
                           static_cast<uint32_t>(needed));
  TEST_ASSERT_TRUE(needed > 500);
  TEST_ASSERT_NOT_NULL(std::strstr(out, longVersion.c_str()));
}

}  // namespace

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_led_mode_names);
  RUN_TEST(test_heartbeat_line_is_stable);
  RUN_TEST(test_info_reports_device_and_firmware_fields);
  RUN_TEST(test_info_tracks_the_active_led_mode);
  RUN_TEST(test_banner_reports_version_and_help_hint);
  RUN_TEST(test_truncated_output_is_nul_terminated_and_reports_needed_length);
  RUN_TEST(test_zero_sized_buffer_reports_length_without_writing);
  RUN_TEST(test_long_version_reports_the_untruncated_length);
  return UNITY_END();
}

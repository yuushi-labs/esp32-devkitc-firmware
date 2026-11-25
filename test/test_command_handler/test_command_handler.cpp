#include <unity.h>

#include <cstring>

#include "command_handler.h"

void setUp() {}
void tearDown() {}

namespace {

using CommandType = fw::CommandType;
using LedMode = fw::LedMode;

void assertCommand(CommandType expected, const char* line) {
  const fw::Command command = fw::parseCommand(line);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(expected),
                        static_cast<int>(command.type));
}

void test_parses_simple_verbs() {
  assertCommand(CommandType::kHelp, "help");
  assertCommand(CommandType::kPing, "ping");
  assertCommand(CommandType::kInfo, "info");
  assertCommand(CommandType::kPinout, "pinout");
}

void test_parsing_is_case_insensitive_and_trims_whitespace() {
  assertCommand(CommandType::kPing, "PING");
  assertCommand(CommandType::kPing, "  PiNg  ");
  assertCommand(CommandType::kPing, "\tping\r");
  assertCommand(CommandType::kInfo, "Info\n");
  assertCommand(CommandType::kPinout, "PINOUT");
  assertCommand(CommandType::kPinout, "  PinOut \r\n");
}

void test_empty_input_is_ignored() {
  assertCommand(CommandType::kNone, "");
  assertCommand(CommandType::kNone, "   ");
  assertCommand(CommandType::kNone, "\r\n");
  assertCommand(CommandType::kNone, nullptr);
}

void test_led_commands_carry_the_mode() {
  const fw::Command on = fw::parseCommand("led on");
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandType::kLedOn),
                        static_cast<int>(on.type));
  TEST_ASSERT_TRUE(on.hasLedMode);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(LedMode::kOn),
                        static_cast<int>(on.ledMode));

  const fw::Command off = fw::parseCommand("LED    OFF");
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandType::kLedOff),
                        static_cast<int>(off.type));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(LedMode::kOff),
                        static_cast<int>(off.ledMode));

  const fw::Command blink = fw::parseCommand("led\tBlink");
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandType::kLedBlink),
                        static_cast<int>(blink.type));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(LedMode::kBlink),
                        static_cast<int>(blink.ledMode));
}

void test_malformed_led_commands_are_unknown() {
  assertCommand(CommandType::kUnknown, "led");
  assertCommand(CommandType::kUnknown, "led on now");
  assertCommand(CommandType::kUnknown, "led toggle");

  const fw::Command red = fw::parseCommand("led red");
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandType::kUnknown),
                        static_cast<int>(red.type));
  TEST_ASSERT_FALSE(red.hasLedMode);
}

void test_extra_arguments_are_rejected() {
  assertCommand(CommandType::kUnknown, "help me");
  assertCommand(CommandType::kUnknown, "ping now");
  assertCommand(CommandType::kUnknown, "info all");
  assertCommand(CommandType::kUnknown, "pinout all");
}

void test_unknown_commands() {
  assertCommand(CommandType::kUnknown, "bogus");
  assertCommand(CommandType::kUnknown,
                "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
}

void test_help_text_lists_every_command() {
  const char* help = fw::commandHelpText();
  TEST_ASSERT_NOT_NULL(std::strstr(help, "help"));
  TEST_ASSERT_NOT_NULL(std::strstr(help, "ping"));
  TEST_ASSERT_NOT_NULL(std::strstr(help, "info"));
  TEST_ASSERT_NOT_NULL(std::strstr(help, "pinout"));
  TEST_ASSERT_NOT_NULL(std::strstr(help, "led on"));
  TEST_ASSERT_NOT_NULL(std::strstr(help, "led off"));
  TEST_ASSERT_NOT_NULL(std::strstr(help, "led blink"));
}

void test_line_buffer_assembles_lf_terminated_lines() {
  char storage[16];
  fw::LineBuffer buffer(storage, sizeof(storage));

  for (const char* p = "ping"; *p != '\0'; ++p) {
    TEST_ASSERT_FALSE(buffer.feed(*p));
  }
  TEST_ASSERT_EQUAL_UINT32(4, static_cast<uint32_t>(buffer.length()));
  TEST_ASSERT_TRUE(buffer.feed('\n'));
  TEST_ASSERT_EQUAL_STRING("ping", buffer.line());
  TEST_ASSERT_FALSE(buffer.overflowed());
}

void test_line_buffer_strips_carriage_returns() {
  char storage[16];
  fw::LineBuffer buffer(storage, sizeof(storage));

  for (const char* p = "led on\r"; *p != '\0'; ++p) {
    buffer.feed(*p);
  }
  TEST_ASSERT_TRUE(buffer.feed('\n'));
  TEST_ASSERT_EQUAL_STRING("led on", buffer.line());

  const fw::Command command = fw::parseCommand(buffer.line());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandType::kLedOn),
                        static_cast<int>(command.type));
}

void test_line_buffer_flags_overflow_and_truncates() {
  char storage[5];
  fw::LineBuffer buffer(storage, sizeof(storage));

  for (const char* p = "abcdef"; *p != '\0'; ++p) {
    TEST_ASSERT_FALSE(buffer.feed(*p));
  }
  TEST_ASSERT_TRUE(buffer.overflowed());
  TEST_ASSERT_TRUE(buffer.feed('\n'));
  TEST_ASSERT_EQUAL_STRING("abcd", buffer.line());
}

void test_line_buffer_clear_resets_state() {
  char storage[8];
  fw::LineBuffer buffer(storage, sizeof(storage));

  for (const char* p = "info"; *p != '\0'; ++p) {
    buffer.feed(*p);
  }
  TEST_ASSERT_TRUE(buffer.feed('\n'));
  buffer.clear();

  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(buffer.length()));
  TEST_ASSERT_FALSE(buffer.overflowed());
  TEST_ASSERT_EQUAL_STRING("", buffer.line());
}

void test_line_buffer_accepts_an_empty_line() {
  char storage[8];
  fw::LineBuffer buffer(storage, sizeof(storage));

  TEST_ASSERT_TRUE(buffer.feed('\n'));
  TEST_ASSERT_EQUAL_STRING("", buffer.line());
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(CommandType::kNone),
      static_cast<int>(fw::parseCommand(buffer.line()).type));
}

void test_long_input_does_not_overflow_the_normaliser() {
  // 126 characters then " b" used to write past the internal buffer.
  char line[160];
  std::memset(line, 'a', 126);
  line[126] = ' ';
  line[127] = 'b';
  line[128] = '\0';
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandType::kUnknown),
                        static_cast<int>(fw::parseCommand(line).type));

  std::memset(line, 'x', 159);
  line[159] = '\0';
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandType::kUnknown),
                        static_cast<int>(fw::parseCommand(line).type));
}

void test_long_token_with_known_prefix_is_unknown() {
  char line[80];
  std::memcpy(line, "led ", 4);
  std::memset(line + 4, 'z', 70);
  line[74] = '\0';
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandType::kUnknown),
                        static_cast<int>(fw::parseCommand(line).type));
}


void test_hardware_commands_parse() {
  assertCommand(CommandType::kScan, "scan");
  assertCommand(CommandType::kScan, "SCAN");
  assertCommand(CommandType::kSensors, "sensors");
  assertCommand(CommandType::kInputs, "inputs");
  assertCommand(CommandType::kScan, " scan ");
}

void test_log_commands_parse_with_their_argument() {
  assertCommand(CommandType::kLogStatus, "log");
  assertCommand(CommandType::kLogStatus, "LOG");
  assertCommand(CommandType::kLogOn, "log on");
  assertCommand(CommandType::kLogOff, "log OFF");
  assertCommand(CommandType::kLogNow, "log now");
  assertCommand(CommandType::kLogClear, "log clear");

  assertCommand(CommandType::kUnknown, "log maybe");
  assertCommand(CommandType::kUnknown, "log on now");
}

void test_page_commands_carry_the_page_number() {
  const fw::Command next = fw::parseCommand("page");
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandType::kPageNext),
                        static_cast<int>(next.type));

  const fw::Command select = fw::parseCommand("page 3");
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandType::kPageSelect),
                        static_cast<int>(select.type));
  TEST_ASSERT_EQUAL_UINT8(3, select.page);

  TEST_ASSERT_EQUAL_UINT8(1, fw::parseCommand("PAGE 1").page);
  TEST_ASSERT_EQUAL_UINT8(4, fw::parseCommand("page 4").page);
  TEST_ASSERT_EQUAL_UINT8(5, fw::parseCommand("page 5").page);
  TEST_ASSERT_EQUAL_UINT8(6, fw::parseCommand("page 6").page);

  assertCommand(CommandType::kUnknown, "page 0");
  assertCommand(CommandType::kUnknown, "page 7");
  assertCommand(CommandType::kUnknown, "page 12");
  assertCommand(CommandType::kUnknown, "page x");
  assertCommand(CommandType::kUnknown, "page 2 3");
}

void test_wifi_commands_parse() {
  assertCommand(CommandType::kWifiStatus, "wifi");
  assertCommand(CommandType::kWifiStatus, "WIFI status");
  assertCommand(CommandType::kWifiAp, "wifi ap");
  assertCommand(CommandType::kWifiClear, "wifi CLEAR");
  assertCommand(CommandType::kUnknown, "wifi maybe");
  assertCommand(CommandType::kUnknown, "wifi ap now");
}

void test_ble_commands_parse() {
  assertCommand(CommandType::kBleStatus, "ble");
  assertCommand(CommandType::kBleStatus, "BLE status");
  assertCommand(CommandType::kBleHidOn, "ble hid on");
  assertCommand(CommandType::kBleHidOff, "ble HID OFF");
  assertCommand(CommandType::kUnknown, "ble hid");
  assertCommand(CommandType::kUnknown, "ble hid toggle");
  assertCommand(CommandType::kUnknown, "ble on");
}

void test_timer_commands_parse() {
  assertCommand(CommandType::kTimerStatus, "timer");
  assertCommand(CommandType::kTimerStart, "timer start");
  assertCommand(CommandType::kTimerPause, "timer pause");
  assertCommand(CommandType::kTimerPause, "timer stop");
  assertCommand(CommandType::kTimerReset, "timer reset");

  const fw::Command set = fw::parseCommand("timer 25");
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandType::kTimerSet),
                        static_cast<int>(set.type));
  TEST_ASSERT_EQUAL_UINT8(25, set.timerMinutes);
  TEST_ASSERT_EQUAL_UINT8(1, fw::parseCommand("timer 1").timerMinutes);
  TEST_ASSERT_EQUAL_UINT8(60, fw::parseCommand("timer 60").timerMinutes);

  assertCommand(CommandType::kUnknown, "timer 0");
  assertCommand(CommandType::kUnknown, "timer 61");
  assertCommand(CommandType::kUnknown, "timer 5x");
  assertCommand(CommandType::kUnknown, "timer 5 6");
}

void test_mode_commands_parse() {
  assertCommand(CommandType::kModeStatus, "mode");
  assertCommand(CommandType::kModeStatus, "MODE");

  const fw::Command select = fw::parseCommand("mode wifi");
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandType::kModeSelect),
                        static_cast<int>(select.type));
  TEST_ASSERT_EQUAL_STRING("wifi", select.mode);

  const fw::Command menu = fw::parseCommand("mode MENU");
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandType::kModeSelect),
                        static_cast<int>(menu.type));
  TEST_ASSERT_EQUAL_STRING("menu", menu.mode);

  assertCommand(CommandType::kUnknown, "mode wifi extra");
  assertCommand(CommandType::kUnknown,
                "mode aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
}

void test_help_text_lists_the_commands_of_each_firmware() {
  const char* station = fw::commandHelpText(fw::FirmwareTarget::kStation);
  TEST_ASSERT_NOT_NULL(std::strstr(station, "scan"));
  TEST_ASSERT_NOT_NULL(std::strstr(station, "sensors"));
  TEST_ASSERT_NOT_NULL(std::strstr(station, "log on"));
  TEST_ASSERT_NOT_NULL(std::strstr(station, "log clear"));
  TEST_ASSERT_NOT_NULL(std::strstr(station, "page 1..6"));
  TEST_ASSERT_NOT_NULL(std::strstr(station, "wifi ap"));
  TEST_ASSERT_NOT_NULL(std::strstr(station, "ble hid"));
  TEST_ASSERT_NOT_NULL(std::strstr(station, "timer"));
  TEST_ASSERT_NOT_NULL(std::strstr(station, "mode NAME"));
  TEST_ASSERT_NULL(std::strstr(station, "inputs"));

  const char* tester = fw::commandHelpText(fw::FirmwareTarget::kTester);
  TEST_ASSERT_NOT_NULL(std::strstr(tester, "scan"));
  TEST_ASSERT_NOT_NULL(std::strstr(tester, "sensors"));
  TEST_ASSERT_NOT_NULL(std::strstr(tester, "inputs"));
  TEST_ASSERT_NULL(std::strstr(tester, "log on"));

  // The default argument follows the build target.
  TEST_ASSERT_EQUAL_STRING(fw::commandHelpText(fw::kFirmwareTarget),
                           fw::commandHelpText());
}

}  // namespace

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_parses_simple_verbs);
  RUN_TEST(test_parsing_is_case_insensitive_and_trims_whitespace);
  RUN_TEST(test_empty_input_is_ignored);
  RUN_TEST(test_led_commands_carry_the_mode);
  RUN_TEST(test_malformed_led_commands_are_unknown);
  RUN_TEST(test_extra_arguments_are_rejected);
  RUN_TEST(test_unknown_commands);
  RUN_TEST(test_help_text_lists_every_command);
  RUN_TEST(test_line_buffer_assembles_lf_terminated_lines);
  RUN_TEST(test_line_buffer_strips_carriage_returns);
  RUN_TEST(test_line_buffer_flags_overflow_and_truncates);
  RUN_TEST(test_line_buffer_clear_resets_state);
  RUN_TEST(test_line_buffer_accepts_an_empty_line);
  RUN_TEST(test_long_input_does_not_overflow_the_normaliser);
  RUN_TEST(test_long_token_with_known_prefix_is_unknown);
  RUN_TEST(test_hardware_commands_parse);
  RUN_TEST(test_log_commands_parse_with_their_argument);
  RUN_TEST(test_page_commands_carry_the_page_number);
  RUN_TEST(test_wifi_commands_parse);
  RUN_TEST(test_ble_commands_parse);
  RUN_TEST(test_timer_commands_parse);
  RUN_TEST(test_mode_commands_parse);
  RUN_TEST(test_help_text_lists_the_commands_of_each_firmware);
  return UNITY_END();
}

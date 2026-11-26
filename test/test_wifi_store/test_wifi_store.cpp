#include <unity.h>

#include <algorithm>
#include <cstring>
#include <string>

#include "net/wifi_credentials.h"
#include "net/wifi_store.h"

void setUp() {}
void tearDown() {}

namespace {

using namespace fw;

WifiCredentials pair(const char* ssid, const char* password) {
  WifiCredentials credentials;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(WifiValidation::kOk),
                        static_cast<int>(makeWifiCredentials(ssid, password, &credentials)));
  return credentials;
}

void test_config_json_round_trips() {
  const WifiCredentials credentials = pair("Home 5G", "hunter2hunter2");
  char json[kWifiConfigMaxBytes];
  const size_t needed = formatWifiConfigJson(credentials, json, sizeof(json));
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(std::strlen(json)),
                           static_cast<uint32_t>(needed));
  TEST_ASSERT_NOT_NULL(std::strstr(json, "\"version\":1"));
  TEST_ASSERT_NOT_NULL(std::strstr(json, "\"ssid\":\"Home 5G\""));
  TEST_ASSERT_NOT_NULL(std::strstr(json, "\"password\":\"hunter2hunter2\""));
  TEST_ASSERT_EQUAL_CHAR('\n', json[needed - 1]);

  WifiCredentials parsed;
  TEST_ASSERT_TRUE(parseWifiConfigJson(json, &parsed));
  TEST_ASSERT_TRUE(parsed.valid);
  TEST_ASSERT_EQUAL_STRING(credentials.ssid, parsed.ssid);
  TEST_ASSERT_EQUAL_STRING(credentials.password, parsed.password);
}

void test_config_json_escapes_quotes_backslashes_and_controls() {
  // Quotes, backslashes and a control character all survive the file.
  const WifiCredentials credentials = pair("My \"Net\"\\5G", "pa\\ss\"word\nend");
  char json[kWifiConfigMaxBytes];
  const size_t needed = formatWifiConfigJson(credentials, json, sizeof(json));
  TEST_ASSERT_TRUE(needed > 0 && needed < sizeof(json));
  TEST_ASSERT_NOT_NULL(std::strstr(json, "\\\"Net\\\""));
  TEST_ASSERT_NOT_NULL(std::strstr(json, "\\\\ss"));
  TEST_ASSERT_NOT_NULL(std::strstr(json, "\\n"));
  // No raw newline inside the string: the file stays one line of JSON with a
  // single terminating newline.
  TEST_ASSERT_EQUAL_INT(1, static_cast<int>(std::count(json, json + needed, '\n')));
  TEST_ASSERT_EQUAL_CHAR('\n', json[needed - 1]);

  WifiCredentials parsed;
  TEST_ASSERT_TRUE(parseWifiConfigJson(json, &parsed));
  TEST_ASSERT_EQUAL_STRING(credentials.ssid, parsed.ssid);
  TEST_ASSERT_EQUAL_STRING(credentials.password, parsed.password);
}

void test_config_json_keeps_utf8_network_names() {
  // A Japanese SSID written by the setup form is stored as its UTF-8 bytes.
  const WifiCredentials credentials = pair("ゆうしの家", "password123");
  char json[kWifiConfigMaxBytes];
  formatWifiConfigJson(credentials, json, sizeof(json));
  WifiCredentials parsed;
  TEST_ASSERT_TRUE(parseWifiConfigJson(json, &parsed));
  TEST_ASSERT_EQUAL_STRING(credentials.ssid, parsed.ssid);
}

void test_config_json_accepts_any_key_order_and_unknown_keys() {
  const char* text =
      "{ \"generated\": \"v0.4.1\", \"password\": \"12345678\", \"ssid\": \"Home\","
      " \"nested\": {\"a\": [1, 2]}, \"version\": 1 }";
  WifiCredentials parsed;
  TEST_ASSERT_TRUE(parseWifiConfigJson(text, &parsed));
  TEST_ASSERT_EQUAL_STRING("Home", parsed.ssid);
  TEST_ASSERT_EQUAL_STRING("12345678", parsed.password);

  // No version is fine too: an older file without it is still usable.
  TEST_ASSERT_TRUE(parseWifiConfigJson("{\"ssid\":\"Home\",\"password\":\"12345678\"}",
                                       &parsed));
}

void test_config_json_rejects_broken_files() {
  WifiCredentials parsed;
  WifiValidation error = WifiValidation::kOk;

  TEST_ASSERT_FALSE(parseWifiConfigJson("", &parsed, &error));
  TEST_ASSERT_FALSE(parseWifiConfigJson("{", &parsed));
  TEST_ASSERT_FALSE(parseWifiConfigJson("{\"ssid\":\"Home\",\"password\":\"1234", &parsed));
  TEST_ASSERT_FALSE(parseWifiConfigJson("{\"password\":\"12345678\"}", &parsed));
  TEST_ASSERT_FALSE(parseWifiConfigJson("{\"ssid\":\"Home\"} trailing", &parsed));
  TEST_ASSERT_FALSE(parseWifiConfigJson(
      "{\"version\":2,\"ssid\":\"Home\",\"password\":\"12345678\"}", &parsed));
  TEST_ASSERT_FALSE(parseWifiConfigJson(
      "{\"ssid\":\"Home\",\"password\":\"short\"}", &parsed, &error));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(WifiValidation::kPasswordTooShort),
                        static_cast<int>(error));
  // An unknown escape must not be guessed.
  TEST_ASSERT_FALSE(parseWifiConfigJson(
      "{\"ssid\":\"Ho\\qme\",\"password\":\"12345678\"}", &parsed));
  // \u0000 would silently end the name.
  TEST_ASSERT_FALSE(parseWifiConfigJson(
      "{\"ssid\":\"Ho\\u0000me\",\"password\":\"12345678\"}", &parsed, &error));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(WifiValidation::kEmbeddedNul),
                        static_cast<int>(error));
}

void test_config_json_rejects_a_value_that_does_not_fit() {
  // 33 characters: the file holds a name the SSID field cannot take.
  std::string text = "{\"ssid\":\"";
  text += std::string(33, 'a');
  text += "\",\"password\":\"12345678\"}";
  WifiCredentials parsed;
  WifiValidation error = WifiValidation::kOk;
  TEST_ASSERT_FALSE(parseWifiConfigJson(text.c_str(), &parsed, &error));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(WifiValidation::kSsidTooLong),
                        static_cast<int>(error));
}

void test_format_reports_a_short_buffer_and_invalid_pairs() {
  const WifiCredentials credentials = pair("Home", "12345678");
  char small[16];
  const size_t needed = formatWifiConfigJson(credentials, small, sizeof(small));
  TEST_ASSERT_TRUE(needed > sizeof(small));
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(std::strlen(small)),
                           static_cast<uint32_t>(sizeof(small) - 1));

  WifiCredentials invalid;
  char json[kWifiConfigMaxBytes];
  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(
                                  formatWifiConfigJson(invalid, json, sizeof(json))));
  TEST_ASSERT_EQUAL_STRING("", json);

  // The worst case pair still fits the file buffer the manager uses.
  const WifiCredentials worst = pair("SSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSS",
                                     "PPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPP");
  TEST_ASSERT_TRUE(formatWifiConfigJson(worst, json, sizeof(json)) < sizeof(json));
}

void test_file_copy_wins_over_nvs() {
  const WifiCredentials filePair = pair("FromFile", "filepass1");
  char json[kWifiConfigMaxBytes];
  formatWifiConfigJson(filePair, json, sizeof(json));

  const WifiConfigChoice choice =
      chooseWifiConfig(json, true, "FromNvs", "nvspass1");
  TEST_ASSERT_EQUAL_INT(static_cast<int>(WifiConfigSource::kLittleFs),
                        static_cast<int>(choice.source));
  TEST_ASSERT_FALSE(choice.migrate);
  TEST_ASSERT_FALSE(choice.fileRejected);
  TEST_ASSERT_EQUAL_STRING("FromFile", choice.credentials.ssid);
  TEST_ASSERT_EQUAL_STRING("filepass1", choice.credentials.password);
}

void test_nvs_is_used_and_migrated_when_the_file_is_missing() {
  const WifiConfigChoice choice = chooseWifiConfig(nullptr, true, "FromNvs", "nvspass1");
  TEST_ASSERT_EQUAL_INT(static_cast<int>(WifiConfigSource::kNvs),
                        static_cast<int>(choice.source));
  TEST_ASSERT_TRUE(choice.migrate);
  TEST_ASSERT_FALSE(choice.fileRejected);
  TEST_ASSERT_EQUAL_STRING("FromNvs", choice.credentials.ssid);

  // An empty file (a zero byte file left by an interrupted write) is the same.
  const WifiConfigChoice empty = chooseWifiConfig("", true, "FromNvs", "nvspass1");
  TEST_ASSERT_EQUAL_INT(static_cast<int>(WifiConfigSource::kNvs),
                        static_cast<int>(empty.source));
  TEST_ASSERT_TRUE(empty.migrate);
}

void test_corrupt_file_falls_back_to_nvs_and_is_flagged() {
  const WifiConfigChoice choice =
      chooseWifiConfig("{\"ssid\":", true, "FromNvs", "nvspass1");
  TEST_ASSERT_EQUAL_INT(static_cast<int>(WifiConfigSource::kNvs),
                        static_cast<int>(choice.source));
  TEST_ASSERT_TRUE(choice.migrate);
  TEST_ASSERT_TRUE(choice.fileRejected);
  TEST_ASSERT_EQUAL_STRING("FromNvs", choice.credentials.ssid);
}

void test_nothing_stored_means_no_credentials() {
  const WifiConfigChoice none = chooseWifiConfig(nullptr, false, "", "");
  TEST_ASSERT_EQUAL_INT(static_cast<int>(WifiConfigSource::kNone),
                        static_cast<int>(none.source));
  TEST_ASSERT_FALSE(none.credentials.valid);
  TEST_ASSERT_FALSE(none.migrate);

  // A corrupt file without NVS is reported but leaves nothing to connect to.
  const WifiConfigChoice broken = chooseWifiConfig("{}", false, "", "");
  TEST_ASSERT_EQUAL_INT(static_cast<int>(WifiConfigSource::kNone),
                        static_cast<int>(broken.source));
  TEST_ASSERT_TRUE(broken.fileRejected);

  // An NVS SSID whose password broke the WPA2 rule is not used either.
  const WifiConfigChoice shortPassword = chooseWifiConfig(nullptr, true, "Home", "short");
  TEST_ASSERT_EQUAL_INT(static_cast<int>(WifiConfigSource::kNone),
                        static_cast<int>(shortPassword.source));
  TEST_ASSERT_FALSE(shortPassword.credentials.valid);
}

void test_source_names() {
  TEST_ASSERT_EQUAL_STRING("littlefs", wifiConfigSourceName(WifiConfigSource::kLittleFs));
  TEST_ASSERT_EQUAL_STRING("nvs", wifiConfigSourceName(WifiConfigSource::kNvs));
  TEST_ASSERT_EQUAL_STRING("none", wifiConfigSourceName(WifiConfigSource::kNone));
  TEST_ASSERT_EQUAL_STRING("/wifi.json", kWifiConfigPath);
}

}  // namespace

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_config_json_round_trips);
  RUN_TEST(test_config_json_escapes_quotes_backslashes_and_controls);
  RUN_TEST(test_config_json_keeps_utf8_network_names);
  RUN_TEST(test_config_json_accepts_any_key_order_and_unknown_keys);
  RUN_TEST(test_config_json_rejects_broken_files);
  RUN_TEST(test_config_json_rejects_a_value_that_does_not_fit);
  RUN_TEST(test_format_reports_a_short_buffer_and_invalid_pairs);
  RUN_TEST(test_file_copy_wins_over_nvs);
  RUN_TEST(test_nvs_is_used_and_migrated_when_the_file_is_missing);
  RUN_TEST(test_corrupt_file_falls_back_to_nvs_and_is_flagged);
  RUN_TEST(test_nothing_stored_means_no_credentials);
  RUN_TEST(test_source_names);
  return UNITY_END();
}

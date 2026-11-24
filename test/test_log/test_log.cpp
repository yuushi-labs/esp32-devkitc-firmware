#include <unity.h>

#include <algorithm>
#include <cstring>
#include <string>

#include "logging/log_record.h"
#include "test_support/fake_i2c.h"

void setUp() {}
void tearDown() {}

namespace {

using fw::LogRecorder;
using fw::LogSample;
using fw::SensorSnapshot;
using fw::test::FakeLogStorage;

const uint32_t kIntervals[] = {10000, 60000, 300000};

LogSample fullSample() {
  LogSample sample;
  sample.uptimeMs = 1234;
  sample.hasTemperature = true;
  sample.temperatureC = 23.456f;
  sample.hasPressure = true;
  sample.pressureHpa = 1006.532f;
  sample.hasHumidity = true;
  sample.humidityPct = 45.04f;
  return sample;
}

void test_header_is_stable() {
  char out[64];
  const size_t needed = fw::formatLogHeader(out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("uptime_ms,temp_c,pressure_hpa,humidity_pct\n", out);
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(std::strlen(out)),
                           static_cast<uint32_t>(needed));
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(std::strlen(fw::kLogHeaderLine)),
                           static_cast<uint32_t>(std::strlen(out) - 1));
}

void test_line_formats_every_column() {
  char out[96];
  const size_t needed = fw::formatLogLine(fullSample(), out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("1234,23.46,1006.53,45.0\n", out);
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(std::strlen(out)),
                           static_cast<uint32_t>(needed));
}

void test_missing_values_keep_the_columns_empty() {
  LogSample sample;
  sample.uptimeMs = 42;
  sample.hasTemperature = true;
  sample.temperatureC = 20.0f;

  char out[96];
  fw::formatLogLine(sample, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("42,20.00,,\n", out);
}

void test_truncated_line_reports_the_needed_length() {
  char out[8];
  const size_t needed = fw::formatLogLine(fullSample(), out, sizeof(out));
  TEST_ASSERT_TRUE(needed > sizeof(out) - 1);
  TEST_ASSERT_EQUAL_UINT32(sizeof(out) - 1, static_cast<uint32_t>(std::strlen(out)));
}

void test_interval_labels() {
  char out[16];
  fw::formatIntervalLabel(10000, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("10 s", out);
  fw::formatIntervalLabel(60000, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("1 min", out);
  fw::formatIntervalLabel(300000, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("5 min", out);
  fw::formatIntervalLabel(2500, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("2500 ms", out);
}

void test_uptime_labels() {
  char out[24];
  fw::formatUptime(45000, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("45s", out);
  fw::formatUptime(754000, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("12m34s", out);
  fw::formatUptime(3723000, out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("1h02m03s", out);
}

void test_sample_prefers_the_bmp280_temperature() {
  SensorSnapshot snapshot;
  snapshot.temperatureValid = true;
  snapshot.temperatureC = 21.5f;
  snapshot.pressureValid = true;
  snapshot.pressureHpa = 1000.0f;
  snapshot.dhtPresent = true;
  snapshot.dhtType = fw::DhtType::kDht22;
  snapshot.humidityValid = true;
  snapshot.humidityPct = 40.0f;
  snapshot.dhtTemperatureC = 20.0f;

  const LogSample sample = fw::makeLogSample(snapshot, 500);
  TEST_ASSERT_TRUE(sample.hasTemperature);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 21.5f, sample.temperatureC);
  TEST_ASSERT_TRUE(sample.hasPressure);
  TEST_ASSERT_TRUE(sample.hasHumidity);

  SensorSnapshot dhtOnly;
  dhtOnly.dhtPresent = true;
  dhtOnly.dhtType = fw::DhtType::kDht22;
  dhtOnly.humidityValid = true;
  dhtOnly.humidityPct = 55.0f;
  dhtOnly.dhtTemperatureC = 19.0f;

  const LogSample fallback = fw::makeLogSample(dhtOnly, 600);
  TEST_ASSERT_TRUE(fallback.hasTemperature);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 19.0f, fallback.temperatureC);
  TEST_ASSERT_FALSE(fallback.hasPressure);
}

void test_recorder_creates_the_header_once() {
  FakeLogStorage storage;
  LogRecorder recorder(storage, kIntervals, 3);

  TEST_ASSERT_TRUE(recorder.begin(0));
  TEST_ASSERT_TRUE(recorder.storageReady());
  TEST_ASSERT_EQUAL_STRING("uptime_ms,temp_c,pressure_hpa,humidity_pct\n",
                           storage.content.c_str());
  TEST_ASSERT_EQUAL_UINT32(0, recorder.recordCount());

  // A second begin() on the same file must not duplicate the header.
  LogRecorder second(storage, kIntervals, 3);
  TEST_ASSERT_TRUE(second.begin(10));
  TEST_ASSERT_EQUAL_UINT32(1, static_cast<uint32_t>(std::count(
                                  storage.content.begin(), storage.content.end(), '\n')));
}

void test_recorder_appends_and_counts_records() {
  FakeLogStorage storage;
  LogRecorder recorder(storage, kIntervals, 3);
  recorder.begin(0);

  TEST_ASSERT_TRUE(recorder.record(fullSample(), 1000));
  TEST_ASSERT_EQUAL_UINT32(1, recorder.recordCount());
  TEST_ASSERT_TRUE(recorder.lastWriteOk());
  TEST_ASSERT_NOT_NULL(std::strstr(storage.content.c_str(), "\n1234,23.46,1006.53,45.0\n"));

  LogSample second;
  second.uptimeMs = 2000;
  TEST_ASSERT_TRUE(recorder.record(second, 2000));
  TEST_ASSERT_EQUAL_UINT32(2, recorder.recordCount());
}

void test_interval_gating_and_pause() {
  FakeLogStorage storage;
  LogRecorder recorder(storage, kIntervals, 3);
  recorder.begin(0);

  const LogSample sample = fullSample();
  TEST_ASSERT_TRUE(recorder.due(0));  // nothing recorded yet
  TEST_ASSERT_TRUE(recorder.poll(sample, 0));
  TEST_ASSERT_FALSE(recorder.due(9999));
  TEST_ASSERT_FALSE(recorder.poll(sample, 9999));
  TEST_ASSERT_TRUE(recorder.due(10000));
  TEST_ASSERT_EQUAL_UINT32(5000, recorder.intervalRemainingMs(5000));

  recorder.setEnabled(false);
  TEST_ASSERT_FALSE(recorder.due(20000));
  TEST_ASSERT_FALSE(recorder.poll(sample, 20000));
  TEST_ASSERT_EQUAL_UINT32(1, recorder.recordCount());
  TEST_ASSERT_EQUAL_UINT32(0, recorder.intervalRemainingMs(20000));

  TEST_ASSERT_TRUE(recorder.toggleEnabled());
  TEST_ASSERT_TRUE(recorder.due(20000));

  // A manual record ignores the pause.
  recorder.setEnabled(false);
  TEST_ASSERT_TRUE(recorder.record(sample, 21000));
  TEST_ASSERT_EQUAL_UINT32(2, recorder.recordCount());
}

void test_interval_cycles_through_the_table() {
  FakeLogStorage storage;
  LogRecorder recorder(storage, kIntervals, 3);
  recorder.begin(0);

  TEST_ASSERT_EQUAL_UINT32(0, static_cast<uint32_t>(recorder.intervalIndex()));
  TEST_ASSERT_EQUAL_UINT32(10000, recorder.intervalMs());
  recorder.nextInterval();
  TEST_ASSERT_EQUAL_UINT32(60000, recorder.intervalMs());
  recorder.nextInterval();
  TEST_ASSERT_EQUAL_UINT32(300000, recorder.intervalMs());
  recorder.nextInterval();
  TEST_ASSERT_EQUAL_UINT32(10000, recorder.intervalMs());
}

void test_write_failure_is_reported() {
  FakeLogStorage storage;
  LogRecorder recorder(storage, kIntervals, 3);
  recorder.begin(0);

  storage.failWrites = true;
  TEST_ASSERT_FALSE(recorder.record(fullSample(), 0));
  TEST_ASSERT_FALSE(recorder.lastWriteOk());
  TEST_ASSERT_EQUAL_UINT32(0, recorder.recordCount());
}

void test_header_failure_blocks_data_until_it_is_rewritten() {
  FakeLogStorage storage;
  LogRecorder recorder(storage, kIntervals, 3);

  storage.failWrites = true;
  TEST_ASSERT_TRUE(recorder.begin(0));  // the filesystem is mounted
  TEST_ASSERT_FALSE(recorder.record(fullSample(), 0));
  TEST_ASSERT_EQUAL_UINT32(0, recorder.recordCount());

  // Once writes work again the recorder writes the header before the data.
  storage.failWrites = false;
  TEST_ASSERT_TRUE(recorder.record(fullSample(), 1000));
  TEST_ASSERT_EQUAL_UINT32(1, recorder.recordCount());
  TEST_ASSERT_EQUAL_STRING("uptime_ms,temp_c,pressure_hpa,humidity_pct\n"
                           "1234,23.46,1006.53,45.0\n",
                           storage.content.c_str());
}

void test_a_failed_write_is_not_retried_on_every_loop() {
  FakeLogStorage storage;
  LogRecorder recorder(storage, kIntervals, 3);
  recorder.begin(0);
  recorder.setEnabled(true);

  storage.failWrites = true;
  TEST_ASSERT_TRUE(recorder.due(0));
  TEST_ASSERT_FALSE(recorder.poll(fullSample(), 0));
  // Wait a full interval before hammering the filesystem again.
  TEST_ASSERT_FALSE(recorder.due(1000));
  TEST_ASSERT_FALSE(recorder.due(9999));
  TEST_ASSERT_TRUE(recorder.due(10000));

  storage.failWrites = false;
  TEST_ASSERT_TRUE(recorder.poll(fullSample(), 10000));
  TEST_ASSERT_EQUAL_UINT32(1, recorder.recordCount());
}

void test_mount_failure_is_reported() {
  FakeLogStorage storage;
  storage.mountOk = false;
  LogRecorder recorder(storage, kIntervals, 3);

  TEST_ASSERT_FALSE(recorder.begin(0));
  TEST_ASSERT_FALSE(recorder.storageReady());
  TEST_ASSERT_FALSE(recorder.record(fullSample(), 0));
}

void test_clear_restarts_the_file() {
  FakeLogStorage storage;
  LogRecorder recorder(storage, kIntervals, 3);
  recorder.begin(0);
  recorder.record(fullSample(), 0);
  recorder.record(fullSample(), 1000);
  TEST_ASSERT_EQUAL_UINT32(2, recorder.recordCount());

  TEST_ASSERT_TRUE(recorder.clear(2000));
  TEST_ASSERT_EQUAL_UINT32(0, recorder.recordCount());
  TEST_ASSERT_EQUAL_STRING("uptime_ms,temp_c,pressure_hpa,humidity_pct\n",
                           storage.content.c_str());
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(std::strlen(fw::kLogHeaderLine)) + 1,
                           recorder.fileSize());
}

}  // namespace

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_header_is_stable);
  RUN_TEST(test_line_formats_every_column);
  RUN_TEST(test_missing_values_keep_the_columns_empty);
  RUN_TEST(test_truncated_line_reports_the_needed_length);
  RUN_TEST(test_interval_labels);
  RUN_TEST(test_uptime_labels);
  RUN_TEST(test_sample_prefers_the_bmp280_temperature);
  RUN_TEST(test_recorder_creates_the_header_once);
  RUN_TEST(test_recorder_appends_and_counts_records);
  RUN_TEST(test_interval_gating_and_pause);
  RUN_TEST(test_interval_cycles_through_the_table);
  RUN_TEST(test_write_failure_is_reported);
  RUN_TEST(test_header_failure_blocks_data_until_it_is_rewritten);
  RUN_TEST(test_a_failed_write_is_not_retried_on_every_loop);
  RUN_TEST(test_mount_failure_is_reported);
  RUN_TEST(test_clear_restarts_the_file);
  return UNITY_END();
}

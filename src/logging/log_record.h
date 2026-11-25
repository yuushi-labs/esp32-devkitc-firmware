// CSV log: one line per sample in /log.csv.
//
// Formatting, interval handling and the on/off state live here (pure C++ and
// unit tested); writing the bytes is the job of a LogStorage implementation
// (LittleFS on the device, an in-memory fake in the tests).
#pragma once

#include <cstddef>
#include <cstdint>

#include "sensors/sensor_snapshot.h"

namespace fw {

struct LogSample {
  // "YYYY-MM-DD HH:MM:SS" in local time, empty until NTP has synced.
  char timestamp[20] = {0};
  uint32_t uptimeMs = 0;
  bool hasTemperature = false;
  float temperatureC = 0.0f;
  bool hasPressure = false;
  float pressureHpa = 0.0f;
  bool hasHumidity = false;
  float humidityPct = 0.0f;
};

// Builds a sample from the latest sensor values.
LogSample makeLogSample(const SensorSnapshot& snapshot, uint32_t uptimeMs);

// "timestamp,uptime_ms,temp_c,pressure_hpa,humidity_pct"
constexpr const char* kLogHeaderLine =
    "timestamp,uptime_ms,temp_c,pressure_hpa,humidity_pct";

// Both always NUL terminate and return the untruncated length (snprintf
// semantics), including the trailing newline.
size_t formatLogHeader(char* out, size_t outSize);
size_t formatLogLine(const LogSample& sample, char* out, size_t outSize);

// True for the CSV header row.
bool isLogHeaderLine(const char* line);
// Parses one data row. Accepts the v0.2.0 four column layout as well, so an
// existing /log.csv still shows up in /api/logs.
bool parseLogLine(const char* line, LogSample* out);

// "10 s" / "1 min" / "5 min" style label for the display.
size_t formatIntervalLabel(uint32_t intervalMs, char* out, size_t outSize);
// "45s", "12m34s" or "1h02m03s".
size_t formatUptime(uint32_t uptimeMs, char* out, size_t outSize);

class LogStorage {
 public:
  virtual ~LogStorage() = default;

  // Mounts the filesystem. True when logging can work.
  virtual bool begin() = 0;
  // Appends one line (which already ends with '\n').
  virtual bool appendLine(const char* line) = 0;
  // Sample lines, excluding the CSV header.
  virtual uint32_t countDataLines() = 0;
  virtual uint32_t fileSize() = 0;
  virtual bool removeFile() = 0;
};

class LogRecorder {
 public:
  LogRecorder(LogStorage& storage, const uint32_t* intervals, size_t intervalCount);

  // Mounts the filesystem and writes the header when the log is new.
  bool begin(uint32_t nowMs);

  bool storageReady() const { return storageReady_; }
  bool enabled() const { return enabled_; }
  void setEnabled(bool enabled) { enabled_ = enabled; }
  bool toggleEnabled();

  size_t intervalIndex() const { return intervalIndex_; }
  uint32_t intervalMs() const;
  void nextInterval();
  void previousInterval();

  // True when a new sample is due at `nowMs` (enabled and one interval passed
  // since the last attempt, so a failing write is not retried on every loop).
  bool due(uint32_t nowMs) const;
  uint32_t intervalRemainingMs(uint32_t nowMs) const;

  // Writes one sample regardless of the on/off state (manual record).
  bool record(const LogSample& sample, uint32_t nowMs);
  // Writes one sample only when enabled and due.
  bool poll(const LogSample& sample, uint32_t nowMs);

  // Deletes /log.csv and starts a fresh one.
  bool clear(uint32_t nowMs);

  uint32_t recordCount() const { return recordCount_; }
  uint32_t fileSize() const { return fileSize_; }
  bool lastWriteOk() const { return lastWriteOk_; }

 private:
  bool appendLine(const char* line);
  bool writeHeader();

  LogStorage& storage_;
  const uint32_t* intervals_;
  size_t intervalCount_;
  size_t intervalIndex_ = 0;
  bool storageReady_ = false;
  bool headerReady_ = false;
  bool enabled_ = true;
  bool lastWriteOk_ = false;
  bool hasAttempted_ = false;
  uint32_t recordCount_ = 0;
  uint32_t lastAttemptMs_ = 0;
  uint32_t fileSize_ = 0;
};

}  // namespace fw

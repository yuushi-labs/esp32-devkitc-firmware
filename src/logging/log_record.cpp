#include "logging/log_record.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace fw {
namespace {

int formatInto(char* out, size_t outSize, const char* format, ...) {
  char scratch = '\0';
  va_list args;
  va_start(args, format);
  const int needed =
      std::vsnprintf(outSize > 0 ? out : &scratch, outSize, format, args);
  va_end(args);
  return needed;
}

}  // namespace

LogSample makeLogSample(const SensorSnapshot& snapshot, uint32_t uptimeMs) {
  LogSample sample;
  sample.uptimeMs = uptimeMs;
  if (snapshot.hasTemperature()) {
    sample.hasTemperature = true;
    sample.temperatureC = snapshot.displayTemperatureC();
  }
  if (snapshot.hasPressure()) {
    sample.hasPressure = true;
    sample.pressureHpa = snapshot.pressureHpa;
  }
  if (snapshot.hasHumidity()) {
    sample.hasHumidity = true;
    sample.humidityPct = snapshot.humidityPct;
  }
  return sample;
}

size_t formatLogHeader(char* out, size_t outSize) {
  const int needed = formatInto(out, outSize, "%s\n", kLogHeaderLine);
  return needed > 0 ? static_cast<size_t>(needed) : 0;
}

size_t formatLogLine(const LogSample& sample, char* out, size_t outSize) {
  char buffer[96];
  char tempText[16] = "";
  char pressureText[16] = "";
  char humidityText[16] = "";
  if (sample.hasTemperature) {
    std::snprintf(tempText, sizeof(tempText), "%.2f", static_cast<double>(sample.temperatureC));
  }
  if (sample.hasPressure) {
    std::snprintf(pressureText, sizeof(pressureText), "%.2f",
                  static_cast<double>(sample.pressureHpa));
  }
  if (sample.hasHumidity) {
    std::snprintf(humidityText, sizeof(humidityText), "%.1f",
                  static_cast<double>(sample.humidityPct));
  }

  const int total = std::snprintf(buffer, sizeof(buffer), "%lu,%s,%s,%s\n",
                                  static_cast<unsigned long>(sample.uptimeMs), tempText,
                                  pressureText, humidityText);
  if (out != nullptr && outSize > 0) {
    std::snprintf(out, outSize, "%s", buffer);
  }
  return total > 0 ? static_cast<size_t>(total) : 0;
}

size_t formatIntervalLabel(uint32_t intervalMs, char* out, size_t outSize) {
  int needed = 0;
  if (intervalMs % 60000u == 0 && intervalMs >= 60000u) {
    needed = formatInto(out, outSize, "%lu min",
                        static_cast<unsigned long>(intervalMs / 60000u));
  } else if (intervalMs % 1000u == 0) {
    needed = formatInto(out, outSize, "%lu s",
                        static_cast<unsigned long>(intervalMs / 1000u));
  } else {
    needed = formatInto(out, outSize, "%lu ms", static_cast<unsigned long>(intervalMs));
  }
  return needed > 0 ? static_cast<size_t>(needed) : 0;
}

size_t formatUptime(uint32_t uptimeMs, char* out, size_t outSize) {
  const uint32_t totalSeconds = uptimeMs / 1000u;
  const uint32_t hours = totalSeconds / 3600u;
  const uint32_t minutes = (totalSeconds / 60u) % 60u;
  const uint32_t seconds = totalSeconds % 60u;

  int needed = 0;
  if (hours > 0) {
    needed = formatInto(out, outSize, "%luh%02lum%02lus",
                        static_cast<unsigned long>(hours),
                        static_cast<unsigned long>(minutes),
                        static_cast<unsigned long>(seconds));
  } else if (minutes > 0) {
    needed = formatInto(out, outSize, "%lum%02lus", static_cast<unsigned long>(minutes),
                        static_cast<unsigned long>(seconds));
  } else {
    needed = formatInto(out, outSize, "%lus", static_cast<unsigned long>(seconds));
  }
  return needed > 0 ? static_cast<size_t>(needed) : 0;
}

LogRecorder::LogRecorder(LogStorage& storage, const uint32_t* intervals,
                         size_t intervalCount)
    : storage_(storage), intervals_(intervals), intervalCount_(intervalCount) {
  if (intervalCount_ == 0) {
    intervalCount_ = 1;
  }
}

bool LogRecorder::begin(uint32_t nowMs) {
  storageReady_ = storage_.begin();
  lastWriteOk_ = false;
  if (!storageReady_) {
    return false;
  }

  fileSize_ = storage_.fileSize();
  // An existing file is assumed to start with the header; a new one gets it
  // written before any data row is allowed.
  headerReady_ = fileSize_ > 0 ? true : writeHeader();
  recordCount_ = storage_.countDataLines();
  fileSize_ = storage_.fileSize();
  lastAttemptMs_ = nowMs;
  hasAttempted_ = false;
  return true;
}

bool LogRecorder::writeHeader() {
  char header[64];
  const size_t needed = formatLogHeader(header, sizeof(header));
  if (needed >= sizeof(header)) {
    return false;
  }
  return appendLine(header);
}

bool LogRecorder::toggleEnabled() {
  enabled_ = !enabled_;
  return enabled_;
}

uint32_t LogRecorder::intervalMs() const {
  if (intervals_ == nullptr || intervalCount_ == 0) {
    return 0;
  }
  const size_t index = intervalIndex_ < intervalCount_ ? intervalIndex_ : 0;
  return intervals_[index];
}

void LogRecorder::nextInterval() {
  if (intervalCount_ == 0) {
    return;
  }
  intervalIndex_ = (intervalIndex_ + 1) % intervalCount_;
}

bool LogRecorder::due(uint32_t nowMs) const {
  if (!enabled_) {
    return false;
  }
  if (!hasAttempted_) {
    return true;
  }
  return (nowMs - lastAttemptMs_) >= intervalMs();
}

uint32_t LogRecorder::intervalRemainingMs(uint32_t nowMs) const {
  if (!enabled_) {
    return 0;
  }
  if (!hasAttempted_) {
    return 0;
  }
  const uint32_t elapsed = nowMs - lastAttemptMs_;
  const uint32_t interval = intervalMs();
  return elapsed >= interval ? 0 : interval - elapsed;
}

bool LogRecorder::appendLine(const char* line) {
  if (!storageReady_ || line == nullptr) {
    lastWriteOk_ = false;
    return false;
  }
  lastWriteOk_ = storage_.appendLine(line);
  if (lastWriteOk_) {
    fileSize_ += static_cast<uint32_t>(std::strlen(line));
  }
  return lastWriteOk_;
}

bool LogRecorder::record(const LogSample& sample, uint32_t nowMs) {
  hasAttempted_ = true;
  lastAttemptMs_ = nowMs;

  if (!headerReady_) {
    headerReady_ = writeHeader();
    if (!headerReady_) {
      lastWriteOk_ = false;
      return false;
    }
  }

  char line[96];
  const size_t needed = formatLogLine(sample, line, sizeof(line));
  if (needed >= sizeof(line)) {
    lastWriteOk_ = false;
    return false;
  }
  if (!appendLine(line)) {
    return false;
  }
  ++recordCount_;
  return true;
}

bool LogRecorder::poll(const LogSample& sample, uint32_t nowMs) {
  if (!due(nowMs)) {
    return false;
  }
  return record(sample, nowMs);
}

bool LogRecorder::clear(uint32_t nowMs) {
  if (!storageReady_) {
    lastWriteOk_ = false;
    return false;
  }
  if (!storage_.removeFile()) {
    lastWriteOk_ = false;
    return false;
  }
  fileSize_ = 0;
  headerReady_ = writeHeader();
  recordCount_ = 0;
  hasAttempted_ = false;
  lastAttemptMs_ = nowMs;
  return headerReady_;
}

}  // namespace fw

// NTP time helpers: Unix seconds to a civil date and a monotonic clock that
// keeps the last synchronised time while the radio is offline.
//
// Pure C++ (no Arduino, no <ctime>) so the conversion and the CSV timestamp
// format are unit tested on the host. The ESP32 side calls configTime() and
// hands the epoch to NtpClock.
#pragma once

#include <cstddef>
#include <cstdint>

namespace fw {

struct CivilTime {
  int year = 1970;
  uint8_t month = 1;
  uint8_t day = 1;
  uint8_t hour = 0;
  uint8_t minute = 0;
  uint8_t second = 0;
  // 0 = Sunday ... 6 = Saturday.
  uint8_t weekday = 4;
};

// Converts Unix seconds (UTC) to a civil date. Negative values are supported.
CivilTime civilTimeFromUnix(int64_t epochSeconds);

// "YYYY-MM-DD HH:MM:SS" in the given offset (for example 9 * 3600 for JST).
// Always NUL terminates and returns the untruncated length.
size_t formatTimestamp(int64_t epochSeconds, int32_t offsetSeconds, char* out,
                       size_t outSize);

// "HH:MM" in the given offset.
size_t formatClockTime(int64_t epochSeconds, int32_t offsetSeconds, char* out,
                       size_t outSize);

// "YYYY-MM-DD" in the given offset.
size_t formatDate(int64_t epochSeconds, int32_t offsetSeconds, char* out,
                  size_t outSize);

class NtpClock {
 public:
  void markSynced(int64_t epochSeconds, uint32_t nowMs);
  void clear();

  bool synced() const { return synced_; }
  // Epoch at `nowMs`, extrapolated with millis(); 0 when never synced.
  int64_t epochAt(uint32_t nowMs) const;
  int64_t lastSyncEpoch() const { return lastSyncEpoch_; }
  uint32_t lastSyncMs() const { return lastSyncMs_; }

 private:
  bool synced_ = false;
  int64_t lastSyncEpoch_ = 0;
  uint32_t lastSyncMs_ = 0;
};

}  // namespace fw

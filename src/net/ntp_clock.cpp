#include "net/ntp_clock.h"

#include <cstdarg>
#include <cstdio>

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

CivilTime civilTimeFromUnix(int64_t epochSeconds) {
  // Split into whole days and the seconds inside the day, keeping the
  // remainder non-negative for dates before 1970.
  int64_t days = epochSeconds / 86400;
  int64_t seconds = epochSeconds % 86400;
  if (seconds < 0) {
    seconds += 86400;
    --days;
  }

  CivilTime civil;
  civil.hour = static_cast<uint8_t>(seconds / 3600);
  civil.minute = static_cast<uint8_t>((seconds / 60) % 60);
  civil.second = static_cast<uint8_t>(seconds % 60);
  civil.weekday = static_cast<uint8_t>(((days % 7) + 4 + 7) % 7);

  // Howard Hinnant's days -> civil algorithm (proleptic Gregorian calendar).
  const int64_t z = days + 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const int64_t doe = z - era * 146097;
  const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int64_t year = yoe + era * 400;
  const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const int64_t mp = (5 * doy + 2) / 153;
  const int64_t day = doy - (153 * mp + 2) / 5 + 1;
  const int64_t month = mp + (mp < 10 ? 3 : -9);

  civil.year = static_cast<int>(year + (month <= 2 ? 1 : 0));
  civil.month = static_cast<uint8_t>(month);
  civil.day = static_cast<uint8_t>(day);
  return civil;
}

size_t formatTimestamp(int64_t epochSeconds, int32_t offsetSeconds, char* out,
                       size_t outSize) {
  const CivilTime civil = civilTimeFromUnix(epochSeconds + offsetSeconds);
  const int needed = formatInto(out, outSize, "%04d-%02u-%02u %02u:%02u:%02u",
                                civil.year, static_cast<unsigned>(civil.month),
                                static_cast<unsigned>(civil.day),
                                static_cast<unsigned>(civil.hour),
                                static_cast<unsigned>(civil.minute),
                                static_cast<unsigned>(civil.second));
  return needed > 0 ? static_cast<size_t>(needed) : 0;
}

size_t formatClockTime(int64_t epochSeconds, int32_t offsetSeconds, char* out,
                       size_t outSize) {
  const CivilTime civil = civilTimeFromUnix(epochSeconds + offsetSeconds);
  const int needed =
      formatInto(out, outSize, "%02u:%02u", static_cast<unsigned>(civil.hour),
                 static_cast<unsigned>(civil.minute));
  return needed > 0 ? static_cast<size_t>(needed) : 0;
}

size_t formatDate(int64_t epochSeconds, int32_t offsetSeconds, char* out,
                  size_t outSize) {
  const CivilTime civil = civilTimeFromUnix(epochSeconds + offsetSeconds);
  const int needed = formatInto(out, outSize, "%04d-%02u-%02u", civil.year,
                                static_cast<unsigned>(civil.month),
                                static_cast<unsigned>(civil.day));
  return needed > 0 ? static_cast<size_t>(needed) : 0;
}

void NtpClock::markSynced(int64_t epochSeconds, uint32_t nowMs) {
  synced_ = true;
  lastSyncEpoch_ = epochSeconds;
  lastSyncMs_ = nowMs;
}

void NtpClock::clear() {
  synced_ = false;
  lastSyncEpoch_ = 0;
  lastSyncMs_ = 0;
}

int64_t NtpClock::epochAt(uint32_t nowMs) const {
  if (!synced_) {
    return 0;
  }
  const uint32_t elapsed = nowMs - lastSyncMs_;
  return lastSyncEpoch_ + static_cast<int64_t>(elapsed / 1000u);
}

void NtpClock::rebase(uint32_t nowMs) {
  if (!synced_) {
    return;
  }
  const uint32_t elapsed = nowMs - lastSyncMs_;
  const uint32_t wholeSeconds = elapsed / 1000u;
  if (wholeSeconds == 0) {
    return;
  }
  lastSyncEpoch_ += static_cast<int64_t>(wholeSeconds);
  // Advance the base by exactly the milliseconds those seconds stand for, so the
  // leftover fraction stays on the clock instead of being dropped.
  lastSyncMs_ += wholeSeconds * 1000u;
}

}  // namespace fw

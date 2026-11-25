#include "station/timer_model.h"

#include <cstdarg>
#include <cstdio>

namespace fw {
namespace {

constexpr uint32_t kMinuteMs = 60000u;

int formatInto(char* out, size_t outSize, const char* format, ...) {
  char scratch = '\0';
  va_list args;
  va_start(args, format);
  const int needed =
      std::vsnprintf(outSize > 0 ? out : &scratch, outSize, format, args);
  va_end(args);
  return needed;
}

uint8_t clampMinutes(int value) {
  if (value < static_cast<int>(CountdownTimer::kMinMinutes)) {
    return CountdownTimer::kMinMinutes;
  }
  if (value > static_cast<int>(CountdownTimer::kMaxMinutes)) {
    return CountdownTimer::kMaxMinutes;
  }
  return static_cast<uint8_t>(value);
}

}  // namespace

const char* timerPhaseName(TimerPhase phase) {
  switch (phase) {
    case TimerPhase::kWork:
      return "work";
    case TimerPhase::kBreak:
      return "break";
    case TimerPhase::kIdle:
      break;
  }
  return "idle";
}

CountdownTimer::CountdownTimer(uint8_t defaultMinutes)
    : minutes_(clampMinutes(defaultMinutes)),
      manualMinutes_(clampMinutes(defaultMinutes)) {
  applyDuration(minutes_);
}

void CountdownTimer::applyDuration(uint8_t minutes) {
  totalMs_ = static_cast<uint32_t>(minutes) * kMinuteMs;
  remainingMs_ = totalMs_;
}

void CountdownTimer::adjust(int delta) {
  if (running_) {
    return;
  }
  pomodoro_ = false;
  setMinutes(static_cast<uint8_t>(clampMinutes(static_cast<int>(minutes_) + delta)));
}

void CountdownTimer::setMinutes(uint8_t minutes) {
  minutes_ = clampMinutes(minutes);
  if (!pomodoro_) {
    manualMinutes_ = minutes_;
  }
  if (!running_) {
    applyDuration(minutes_);
    finished_ = false;
  }
}

void CountdownTimer::setPomodoro(bool enabled) {
  if (pomodoro_ == enabled) {
    return;
  }
  pomodoro_ = enabled;
  running_ = false;
  finished_ = false;
  phase_ = TimerPhase::kIdle;
  if (pomodoro_) {
    manualMinutes_ = minutes_;
    minutes_ = kPomodoroWorkMinutes;
  } else {
    minutes_ = manualMinutes_;
  }
  applyDuration(minutes_);
}

void CountdownTimer::togglePomodoro() { setPomodoro(!pomodoro_); }

void CountdownTimer::start(uint32_t nowMs) {
  if (running_) {
    return;
  }
  if (remainingMs_ == 0) {
    applyDuration(minutes_);
  }
  if (phase_ == TimerPhase::kIdle) {
    phase_ = TimerPhase::kWork;
  }
  running_ = true;
  finished_ = false;
  lastTickMs_ = nowMs;
}

void CountdownTimer::pause(uint32_t nowMs) {
  if (!running_) {
    return;
  }
  update(nowMs);
  running_ = false;
}

void CountdownTimer::toggle(uint32_t nowMs) {
  if (running_) {
    pause(nowMs);
  } else {
    start(nowMs);
  }
}

void CountdownTimer::reset() {
  running_ = false;
  finished_ = false;
  phase_ = TimerPhase::kIdle;
  if (pomodoro_) {
    minutes_ = kPomodoroWorkMinutes;
  }
  applyDuration(minutes_);
}

void CountdownTimer::update(uint32_t nowMs) {
  if (!running_) {
    return;
  }
  const uint32_t elapsed = nowMs - lastTickMs_;
  lastTickMs_ = nowMs;
  if (elapsed >= remainingMs_) {
    remainingMs_ = 0;
    finishPhase();
    return;
  }
  remainingMs_ -= elapsed;
}

void CountdownTimer::finishPhase() {
  running_ = false;
  finished_ = true;
  if (!pomodoro_) {
    phase_ = TimerPhase::kWork;
    return;
  }
  // Pomodoro alternates work and break, ready for the next start.
  if (phase_ == TimerPhase::kBreak) {
    phase_ = TimerPhase::kWork;
    minutes_ = kPomodoroWorkMinutes;
  } else {
    phase_ = TimerPhase::kBreak;
    minutes_ = kPomodoroBreakMinutes;
  }
  applyDuration(minutes_);
}

uint8_t CountdownTimer::progressPercent() const {
  if (totalMs_ == 0) {
    return 0;
  }
  const uint32_t elapsed = totalMs_ - remainingMs_;
  return static_cast<uint8_t>((elapsed * 100u) / totalMs_);
}

bool CountdownTimer::consumeFinished() {
  const bool value = finished_;
  finished_ = false;
  return value;
}

size_t CountdownTimer::formatRemaining(char* out, size_t outSize) const {
  const uint32_t seconds = (remainingMs_ + 999u) / 1000u;
  const int needed = formatInto(out, outSize, "%02lu:%02lu",
                                static_cast<unsigned long>(seconds / 60u),
                                static_cast<unsigned long>(seconds % 60u));
  return needed > 0 ? static_cast<size_t>(needed) : 0;
}

size_t CountdownTimer::formatStatus(char* out, size_t outSize) const {
  char remaining[8];
  formatRemaining(remaining, sizeof(remaining));
  const int needed = formatInto(
      out, outSize,
      "timer: %s (%s) %s\n"
      "  mode: %s\n"
      "  phase: %s\n"
      "  progress: %u%%\n",
      remaining, timerPhaseName(phase_), running_ ? "running" : "stopped",
      pomodoro_ ? "pomodoro" : "countdown", timerPhaseName(phase_),
      static_cast<unsigned>(progressPercent()));
  return needed > 0 ? static_cast<size_t>(needed) : 0;
}

}  // namespace fw

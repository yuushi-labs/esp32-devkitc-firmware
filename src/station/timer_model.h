// Countdown and Pomodoro timer state machine.
//
// Time is passed in by the caller so every transition (start, pause, work ->
// break, finished latch) is unit tested without sleeping.
#pragma once

#include <cstddef>
#include <cstdint>

namespace fw {

enum class TimerPhase { kIdle, kWork, kBreak };

const char* timerPhaseName(TimerPhase phase);

class CountdownTimer {
 public:
  static constexpr uint8_t kMinMinutes = 1;
  static constexpr uint8_t kMaxMinutes = 60;
  static constexpr uint8_t kPomodoroWorkMinutes = 25;
  static constexpr uint8_t kPomodoroBreakMinutes = 5;

  explicit CountdownTimer(uint8_t defaultMinutes = kPomodoroWorkMinutes);

  // Encoder adjustment; only allowed while stopped. Turning the encoder leaves
  // the Pomodoro preset.
  void adjust(int delta);
  void setMinutes(uint8_t minutes);
  uint8_t minutes() const { return minutes_; }

  void setPomodoro(bool enabled);
  void togglePomodoro();
  bool pomodoro() const { return pomodoro_; }

  void start(uint32_t nowMs);
  void pause(uint32_t nowMs);
  void toggle(uint32_t nowMs);
  void reset();
  // Advances the countdown; fires the finished latch at 00:00.
  void update(uint32_t nowMs);

  bool running() const { return running_; }
  TimerPhase phase() const { return phase_; }
  uint32_t totalMs() const { return totalMs_; }
  uint32_t remainingMs() const { return remainingMs_; }
  uint8_t progressPercent() const;

  // True exactly once per finished phase.
  bool consumeFinished();

  // "25:00" (rounded up so the display never shows 00:00 early).
  size_t formatRemaining(char* out, size_t outSize) const;
  // Multi line `timer` report. NUL terminated, snprintf semantics.
  size_t formatStatus(char* out, size_t outSize) const;

 private:
  void applyDuration(uint8_t minutes);
  void finishPhase();

  uint8_t minutes_;
  uint8_t manualMinutes_;
  bool pomodoro_ = false;
  TimerPhase phase_ = TimerPhase::kIdle;
  bool running_ = false;
  bool finished_ = false;
  uint32_t totalMs_;
  uint32_t remainingMs_;
  uint32_t lastTickMs_ = 0;
};

}  // namespace fw

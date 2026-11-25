#include <unity.h>

#include <cstring>

#include "station/timer_model.h"

void setUp() {}
void tearDown() {}

namespace {

using namespace fw;

void test_default_timer_is_ready() {
  CountdownTimer timer;
  TEST_ASSERT_EQUAL_UINT8(25, timer.minutes());
  TEST_ASSERT_FALSE(timer.running());
  TEST_ASSERT_FALSE(timer.pomodoro());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(TimerPhase::kIdle),
                        static_cast<int>(timer.phase()));
  TEST_ASSERT_EQUAL_UINT32(25u * 60000u, timer.totalMs());
  TEST_ASSERT_EQUAL_UINT8(0, timer.progressPercent());

  char out[8];
  timer.formatRemaining(out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("25:00", out);
}

void test_minutes_clamp_and_adjust() {
  CountdownTimer timer;
  timer.setMinutes(10);
  TEST_ASSERT_EQUAL_UINT8(10, timer.minutes());
  TEST_ASSERT_EQUAL_UINT32(10u * 60000u, timer.totalMs());

  timer.setMinutes(0);
  TEST_ASSERT_EQUAL_UINT8(1, timer.minutes());
  timer.setMinutes(100);
  TEST_ASSERT_EQUAL_UINT8(60, timer.minutes());

  timer.setMinutes(10);
  timer.adjust(-5);
  TEST_ASSERT_EQUAL_UINT8(5, timer.minutes());
  timer.adjust(3);
  TEST_ASSERT_EQUAL_UINT8(8, timer.minutes());

  timer.start(0);
  timer.adjust(-3);  // ignored while running
  TEST_ASSERT_EQUAL_UINT8(8, timer.minutes());
}

void test_countdown_start_pause_and_progress() {
  CountdownTimer timer;
  timer.setMinutes(10);
  timer.start(1000);
  TEST_ASSERT_TRUE(timer.running());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(TimerPhase::kWork),
                        static_cast<int>(timer.phase()));

  timer.update(61000);
  TEST_ASSERT_EQUAL_UINT32(540000, timer.remainingMs());
  TEST_ASSERT_EQUAL_UINT8(10, timer.progressPercent());

  timer.pause(61000);
  TEST_ASSERT_FALSE(timer.running());
  TEST_ASSERT_EQUAL_UINT32(540000, timer.remainingMs());

  timer.toggle(62000);
  TEST_ASSERT_TRUE(timer.running());
  timer.toggle(62000);
  TEST_ASSERT_FALSE(timer.running());
}

void test_finish_latches_once() {
  CountdownTimer timer;
  timer.setMinutes(1);
  timer.start(0);
  timer.update(30000);
  TEST_ASSERT_EQUAL_UINT8(50, timer.progressPercent());

  timer.update(60000);
  TEST_ASSERT_EQUAL_UINT32(0, timer.remainingMs());
  TEST_ASSERT_FALSE(timer.running());
  TEST_ASSERT_EQUAL_UINT8(100, timer.progressPercent());
  TEST_ASSERT_TRUE(timer.consumeFinished());
  TEST_ASSERT_FALSE(timer.consumeFinished());

  char out[8];
  timer.formatRemaining(out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("00:00", out);

  timer.reset();
  TEST_ASSERT_EQUAL_INT(static_cast<int>(TimerPhase::kIdle),
                        static_cast<int>(timer.phase()));
  TEST_ASSERT_EQUAL_UINT32(60000, timer.remainingMs());
}

void test_pomodoro_alternates_work_and_break() {
  CountdownTimer timer;
  timer.togglePomodoro();
  TEST_ASSERT_TRUE(timer.pomodoro());
  TEST_ASSERT_EQUAL_UINT8(25, timer.minutes());

  timer.start(0);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(TimerPhase::kWork),
                        static_cast<int>(timer.phase()));
  timer.update(25u * 60000u);
  TEST_ASSERT_TRUE(timer.consumeFinished());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(TimerPhase::kBreak),
                        static_cast<int>(timer.phase()));
  TEST_ASSERT_EQUAL_UINT8(5, timer.minutes());
  TEST_ASSERT_EQUAL_UINT32(5u * 60000u, timer.totalMs());

  timer.start(1000);
  timer.update(1000 + 5u * 60000u);
  TEST_ASSERT_TRUE(timer.consumeFinished());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(TimerPhase::kWork),
                        static_cast<int>(timer.phase()));
  TEST_ASSERT_EQUAL_UINT8(25, timer.minutes());

  timer.togglePomodoro();
  TEST_ASSERT_FALSE(timer.pomodoro());
  TEST_ASSERT_EQUAL_UINT8(25, timer.minutes());
}

void test_encoder_adjust_leaves_pomodoro() {
  CountdownTimer timer;
  timer.togglePomodoro();
  TEST_ASSERT_TRUE(timer.pomodoro());
  timer.adjust(5);
  TEST_ASSERT_FALSE(timer.pomodoro());
  TEST_ASSERT_EQUAL_UINT8(30, timer.minutes());
}

void test_remaining_rounds_up() {
  CountdownTimer timer;
  timer.setMinutes(1);
  timer.start(0);
  timer.update(1000);
  char out[8];
  timer.formatRemaining(out, sizeof(out));
  TEST_ASSERT_EQUAL_STRING("00:59", out);
}

void test_status_report() {
  CountdownTimer timer;
  timer.togglePomodoro();
  char out[256];
  timer.formatStatus(out, sizeof(out));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "timer: 25:00 (idle) stopped"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "mode: pomodoro"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "phase: idle"));
  TEST_ASSERT_NOT_NULL(std::strstr(out, "progress: 0%"));
}

}  // namespace

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_default_timer_is_ready);
  RUN_TEST(test_minutes_clamp_and_adjust);
  RUN_TEST(test_countdown_start_pause_and_progress);
  RUN_TEST(test_finish_latches_once);
  RUN_TEST(test_pomodoro_alternates_work_and_break);
  RUN_TEST(test_encoder_adjust_leaves_pomodoro);
  RUN_TEST(test_remaining_rounds_up);
  RUN_TEST(test_status_report);
  return UNITY_END();
}

#include <unity.h>

#include <cstdio>
#include <cstring>

#include "display/framebuffer.h"
#include "net/ntp_clock.h"
#include "pet/pet_model.h"
#include "pet/pet_screen.h"

void setUp() {}
void tearDown() {}

namespace {

using namespace fw;

SensorSnapshot comfortable() {
  SensorSnapshot sensors;
  sensors.temperatureValid = true;
  sensors.temperatureC = 22.5f;
  sensors.humidityValid = true;
  sensors.humidityPct = 50.0f;
  sensors.dhtPresent = true;
  return sensors;
}

CivilTime civilAt(uint8_t hour, uint8_t minute, uint8_t second,
                  uint8_t day = 26) {
  CivilTime now;
  now.year = 2026;
  now.month = 11;
  now.day = day;
  now.hour = hour;
  now.minute = minute;
  now.second = second;
  return now;
}

// Drives the pet from `fromMs` to `toMs` in 100 ms steps, as the app loop does.
void runRange(VirtualPet* pet, uint32_t fromMs, uint32_t toMs, const CivilTime& now,
              const SensorSnapshot& sensors, bool timeSynced = true) {
  for (uint32_t t = fromMs; t <= toMs; t += 100) {
    pet->update(t, timeSynced, now, sensors);
  }
}

void run(VirtualPet* pet, uint32_t untilMs, const CivilTime& now,
         const SensorSnapshot& sensors, bool timeSynced = true) {
  runRange(pet, 0, untilMs, now, sensors, timeSynced);
}

bool anyLit(const Framebuffer& framebuffer, int x0, int y0, int x1, int y1) {
  for (int y = y0; y <= y1; ++y) {
    for (int x = x0; x <= x1; ++x) {
      if (framebuffer.pixel(x, y)) {
        return true;
      }
    }
  }
  return false;
}

void test_mood_follows_the_sensors() {
  SensorSnapshot sensors = comfortable();
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PetMood::kComfortable),
                        static_cast<int>(petMoodFromSensors(sensors)));

  sensors.temperatureC = 31.0f;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PetMood::kHot),
                        static_cast<int>(petMoodFromSensors(sensors)));

  sensors.temperatureC = 4.0f;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PetMood::kCold),
                        static_cast<int>(petMoodFromSensors(sensors)));

  sensors = comfortable();
  sensors.humidityPct = 30.0f;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PetMood::kDry),
                        static_cast<int>(petMoodFromSensors(sensors)));

  sensors.humidityPct = 80.0f;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PetMood::kHumid),
                        static_cast<int>(petMoodFromSensors(sensors)));

  sensors = comfortable();
  sensors.temperatureC = 27.0f;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PetMood::kWarm),
                        static_cast<int>(petMoodFromSensors(sensors)));

  SensorSnapshot empty;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PetMood::kUnknown),
                        static_cast<int>(petMoodFromSensors(empty)));
}

void test_mood_names_and_speech() {
  TEST_ASSERT_EQUAL_STRING("hot", petMoodName(PetMood::kHot));
  TEST_ASSERT_EQUAL_STRING("hot!", petMoodLabel(PetMood::kHot));
  TEST_ASSERT_EQUAL_STRING("dry!", petMoodLabel(PetMood::kDry));
  TEST_ASSERT_EQUAL_STRING("nice", petMoodLabel(PetMood::kComfortable));
  TEST_ASSERT_EQUAL_STRING("--", petMoodLabel(PetMood::kUnknown));
  TEST_ASSERT_EQUAL_STRING("so hot...", petMoodSpeech(PetMood::kHot));
  TEST_ASSERT_EQUAL_STRING("hello", petMoodSpeech(PetMood::kUnknown));
  TEST_ASSERT_EQUAL_STRING("walk", petActivityName(PetActivity::kWalk));
  TEST_ASSERT_EQUAL_STRING("clock", petActivityName(PetActivity::kClock));
}

void test_cat_walks_and_sits_on_a_cycle() {
  VirtualPet pet;
  const SensorSnapshot sensors = comfortable();
  const CivilTime now = civilAt(12, 30, 0);

  run(&pet, 3000, now, sensors);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PetActivity::kWalk),
                        static_cast<int>(pet.scene().activity));
  // 18 px per second from the left edge: 8 + 54 after three seconds.
  TEST_ASSERT_EQUAL_INT(VirtualPet::kWalkLeft + 54, pet.scene().spriteX);

  // Sitting starts at 10 s and stops the cat where the walk left it.
  runRange(&pet, 3100, 10400, now, sensors);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PetActivity::kSit),
                        static_cast<int>(pet.scene().activity));
  const int16_t sitting = pet.scene().spriteX;
  runRange(&pet, 10500, 15000, now, sensors);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PetActivity::kSit),
                        static_cast<int>(pet.scene().activity));
  TEST_ASSERT_EQUAL_INT(sitting, pet.scene().spriteX);
}

void test_walk_speed_does_not_depend_on_the_update_interval() {
  const SensorSnapshot sensors = comfortable();
  const CivilTime now = civilAt(12, 30, 0);
  int16_t positions[3] = {0, 0, 0};
  const uint32_t steps[3] = {10, 100, 250};
  for (size_t index = 0; index < 3; ++index) {
    VirtualPet pet;
    for (uint32_t t = 0; t <= 3000; t += steps[index]) {
      pet.update(t, true, now, sensors);
    }
    positions[index] = pet.scene().spriteX;
  }
  // 18 px/s for three seconds is 54 px at every update rate.
  TEST_ASSERT_EQUAL_INT(VirtualPet::kWalkLeft + 54, positions[0]);
  TEST_ASSERT_EQUAL_INT(positions[0], positions[1]);
  TEST_ASSERT_EQUAL_INT(positions[0], positions[2]);
}

void test_cat_stays_on_its_track() {
  VirtualPet pet;
  const SensorSnapshot sensors = comfortable();
  const CivilTime now = civilAt(12, 30, 0);
  for (uint32_t t = 0; t <= 60000; t += 100) {
    pet.update(t, true, now, sensors);
    TEST_ASSERT_TRUE(pet.scene().spriteX >= VirtualPet::kWalkLeft);
    TEST_ASSERT_TRUE(pet.scene().spriteX <= VirtualPet::kWalkRight);
  }
}

void test_blink_closes_the_eyes_for_a_moment() {
  VirtualPet pet;
  const SensorSnapshot sensors = comfortable();
  const CivilTime now = civilAt(12, 30, 0);
  pet.update(100, true, now, sensors);
  TEST_ASSERT_TRUE(pet.scene().eyesClosed);
  pet.update(400, true, now, sensors);
  TEST_ASSERT_FALSE(pet.scene().eyesClosed);
}

void test_cat_sleeps_at_night() {
  VirtualPet pet;
  const SensorSnapshot sensors = comfortable();
  const CivilTime night = civilAt(2, 15, 0);
  run(&pet, 1000, night, sensors);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PetActivity::kSleep),
                        static_cast<int>(pet.scene().activity));
  TEST_ASSERT_TRUE(pet.scene().eyesClosed);
  TEST_ASSERT_EQUAL_UINT8(2, pet.scene().tailFrame);
  TEST_ASSERT_EQUAL_STRING("zzz...", pet.scene().speech);

  // Without a synced clock there is no night to detect, so the cat stays up.
  VirtualPet unsynced;
  run(&unsynced, 1000, night, sensors, false);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PetActivity::kWalk),
                        static_cast<int>(unsynced.scene().activity));
}

void test_clock_chime_fires_once_an_hour() {
  VirtualPet pet;
  const SensorSnapshot sensors = comfortable();
  const CivilTime seven = civilAt(7, 0, 5);

  pet.update(0, true, seven, sensors);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PetActivity::kClock),
                        static_cast<int>(pet.scene().activity));
  TEST_ASSERT_TRUE(pet.scene().bubbleVisible);
  TEST_ASSERT_TRUE(pet.scene().clockVisible);
  TEST_ASSERT_EQUAL_STRING("07:00", pet.scene().clockText);
  TEST_ASSERT_EQUAL_STRING("07:00 !", pet.scene().bubbleText);
  TEST_ASSERT_EQUAL_STRING("07:00", pet.scene().clock);
  TEST_ASSERT_EQUAL_INT(7, pet.lastChimeHour());
  TEST_ASSERT_TRUE(pet.consumeChimeEvent());
  TEST_ASSERT_FALSE(pet.consumeChimeEvent());

  // Still inside the same hour: no second chime.
  pet.update(5000, true, civilAt(7, 0, 10), sensors);
  TEST_ASSERT_FALSE(pet.consumeChimeEvent());

  // The chime stops after its eight seconds.
  pet.update(20000, true, civilAt(7, 0, 25), sensors);
  TEST_ASSERT_FALSE(pet.chimeActive());
  TEST_ASSERT_FALSE(pet.scene().bubbleVisible);
  TEST_ASSERT_EQUAL_STRING("07:00", pet.scene().clock);

  // The next hour and the same hour on the next day both fire again.
  pet.update(30000, true, civilAt(8, 0, 1), sensors);
  TEST_ASSERT_TRUE(pet.consumeChimeEvent());
  pet.update(40000, true, civilAt(7, 0, 1, 27), sensors);
  TEST_ASSERT_TRUE(pet.consumeChimeEvent());
}

void test_chime_needs_a_synced_clock_and_the_first_seconds() {
  VirtualPet pet;
  const SensorSnapshot sensors = comfortable();

  pet.update(0, false, civilAt(7, 0, 1), sensors);
  TEST_ASSERT_FALSE(pet.chimeActive());
  TEST_ASSERT_FALSE(pet.consumeChimeEvent());
  TEST_ASSERT_EQUAL_INT(-1, pet.lastChimeHour());

  // Booting at 07:00:45 must not ring late with a stale-looking clock.
  VirtualPet late;
  late.update(0, true, civilAt(7, 0, 45), sensors);
  TEST_ASSERT_FALSE(late.chimeActive());

  VirtualPet minute;
  minute.update(0, true, civilAt(7, 20, 1), sensors);
  TEST_ASSERT_FALSE(minute.chimeActive());
}

void test_test_chime_replays_with_the_given_time() {
  VirtualPet pet;
  const SensorSnapshot sensors = comfortable();
  run(&pet, 1000, civilAt(12, 30, 0), sensors);
  TEST_ASSERT_FALSE(pet.chimeActive());

  pet.playClockChime(2000, "12:30");
  TEST_ASSERT_TRUE(pet.chimeActive());
  TEST_ASSERT_EQUAL_STRING("12:30 !", pet.scene().bubbleText);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PetActivity::kClock),
                        static_cast<int>(pet.scene().activity));

  pet.update(4000, true, civilAt(12, 30, 4), sensors);
  TEST_ASSERT_TRUE(pet.chimeActive());
  pet.update(11000, true, civilAt(12, 30, 11), sensors);
  TEST_ASSERT_FALSE(pet.chimeActive());
}

void test_scene_carries_the_readings_and_speech() {
  VirtualPet pet;
  SensorSnapshot sensors = comfortable();
  sensors.temperatureC = 24.0f;
  sensors.humidityPct = 41.0f;
  run(&pet, 500, civilAt(9, 5, 0), sensors);
  TEST_ASSERT_EQUAL_STRING("24.0C 41%", pet.scene().sensors);
  TEST_ASSERT_EQUAL_STRING("nice day", pet.scene().speech);
  TEST_ASSERT_EQUAL_STRING("09:05", pet.scene().clock);

  SensorSnapshot empty;
  VirtualPet bare;
  run(&bare, 500, civilAt(9, 5, 0), empty);
  TEST_ASSERT_EQUAL_STRING("", bare.scene().sensors);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PetMood::kUnknown),
                        static_cast<int>(bare.scene().mood));
}

void test_screen_draws_the_cat_on_the_ground() {
  Framebuffer framebuffer;
  PetScene scene;
  scene.spriteX = 40;
  scene.activity = PetActivity::kWalk;
  scene.mood = PetMood::kComfortable;
  std::snprintf(scene.clock, sizeof(scene.clock), "12:34");
  std::snprintf(scene.speech, sizeof(scene.speech), "nice day");
  std::snprintf(scene.sensors, sizeof(scene.sensors), "22.5C 50%%");

  drawPetScreen(scene, &framebuffer);
  // Ground dashes: two pixels on, two pixels off.
  TEST_ASSERT_TRUE(framebuffer.pixel(0, 55));
  TEST_ASSERT_TRUE(framebuffer.pixel(1, 55));
  TEST_ASSERT_FALSE(framebuffer.pixel(2, 55));
  // The clock sits on the top line, the reading line under the ground.
  TEST_ASSERT_TRUE(anyLit(framebuffer, 0, 0, 34, 6));
  TEST_ASSERT_TRUE(anyLit(framebuffer, 0, 56, 127, 63));
  // The cat's head fills the sprite box.
  TEST_ASSERT_TRUE(framebuffer.pixel(40 + 8, 34 + 3));
  // Nothing of the cat outside its box.
  TEST_ASSERT_FALSE(framebuffer.pixel(40 - 1, 40));
  TEST_ASSERT_FALSE(framebuffer.pixel(40 + 24, 40));
}

void test_screen_draws_the_chime_bubble_with_its_clock() {
  Framebuffer framebuffer;
  PetScene scene;
  scene.spriteX = 52;
  scene.activity = PetActivity::kClock;
  scene.clockVisible = true;
  scene.bubbleVisible = true;
  std::snprintf(scene.clock, sizeof(scene.clock), "07:00");
  std::snprintf(scene.clockText, sizeof(scene.clockText), "07:00");
  std::snprintf(scene.bubbleText, sizeof(scene.bubbleText), "07:00 !");

  drawPetScreen(scene, &framebuffer);
  // Bubble frame.
  TEST_ASSERT_TRUE(framebuffer.pixel(2, 7));
  // v0.4.1 shortened the bubble by two pixels so the cat can hop under it.
  TEST_ASSERT_TRUE(framebuffer.pixel(125, 28));
  TEST_ASSERT_FALSE(framebuffer.pixel(125, 29));
  TEST_ASSERT_FALSE(framebuffer.pixel(60, 20));
  // The clock face sits at the left end of the bubble.
  TEST_ASSERT_TRUE(framebuffer.pixel(6 + 8, 11 + 1));
  TEST_ASSERT_TRUE(framebuffer.pixel(6 + 8, 11 + 14));
  // The bubble replaces the speech line, so the click sits straight on the cat.
  TEST_ASSERT_FALSE(framebuffer.pixel(52 + 8, 34 + 6));
  TEST_ASSERT_TRUE(framebuffer.pixel(52 + 8, 34 + 3));
}

void test_eyes_close_over_the_sprite_holes() {
  Framebuffer open;
  Framebuffer closed;
  drawPetSprite(&open, 0, 10, 10, false);
  drawPetSprite(&closed, 0, 10, 10, true);
  // The eye holes of the art are at x 8..9 and 14..15 of rows 6..7.
  TEST_ASSERT_FALSE(open.pixel(10 + 8, 10 + 7));
  TEST_ASSERT_FALSE(open.pixel(10 + 15, 10 + 7));
  TEST_ASSERT_TRUE(closed.pixel(10 + 8, 10 + 7));
  TEST_ASSERT_TRUE(closed.pixel(10 + 15, 10 + 7));

  // The three tail frames differ but keep the head.
  Framebuffer up;
  Framebuffer down;
  drawPetSprite(&up, 0, 10, 10, false);
  drawPetSprite(&down, 2, 10, 10, false);
  TEST_ASSERT_TRUE(up.pixel(10 + 8, 10 + 3));
  TEST_ASSERT_TRUE(down.pixel(10 + 8, 10 + 3));
  int differences = 0;
  for (int y = 0; y < 20; ++y) {
    for (int x = 0; x < 24; ++x) {
      if (up.pixel(10 + x, 10 + y) != down.pixel(10 + x, 10 + y)) {
        ++differences;
      }
    }
  }
  TEST_ASSERT_TRUE(differences > 0);
}


uint32_t scheduleStart(PetActivity activity, size_t occurrence = 0) {
  uint32_t elapsed = 0;
  size_t seen = 0;
  for (size_t index = 0; index < kPetDayScheduleCount; ++index) {
    if (kPetDaySchedule[index].activity == activity) {
      if (seen == occurrence) {
        return elapsed;
      }
      ++seen;
    }
    elapsed += kPetDaySchedule[index].durationMs;
  }
  return 0;
}

// First lit row below the top clock line and the speech bubble.
int lowestLitRow(const Framebuffer& framebuffer, int x0, int x1) {
  for (int y = 29; y < Framebuffer::kHeight; ++y) {
    if (anyLit(framebuffer, x0, y, x1, y)) {
      return y;
    }
  }
  return -1;
}

size_t litPixels(const Framebuffer& framebuffer, int x0, int y0, int x1, int y1) {
  size_t count = 0;
  for (int y = y0; y <= y1; ++y) {
    for (int x = x0; x <= x1; ++x) {
      count += framebuffer.pixel(x, y) ? 1 : 0;
    }
  }
  return count;
}

void test_day_schedule_covers_every_move() {
  TEST_ASSERT_EQUAL_UINT32(101400, petDayCycleMs());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PetActivity::kWalk),
                        static_cast<int>(petScheduledActivity(0)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PetActivity::kSit),
                        static_cast<int>(petScheduledActivity(10000)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PetActivity::kWalk),
                        static_cast<int>(petScheduledActivity(16000)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PetActivity::kStretch),
                        static_cast<int>(petScheduledActivity(32000)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PetActivity::kGroom),
                        static_cast<int>(petScheduledActivity(51000)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PetActivity::kJump),
                        static_cast<int>(petScheduledActivity(71000)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PetActivity::kLook),
                        static_cast<int>(petScheduledActivity(89400)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PetActivity::kNap),
                        static_cast<int>(petScheduledActivity(93400)));
  // The routine repeats after the cycle.
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PetActivity::kWalk),
                        static_cast<int>(petScheduledActivity(petDayCycleMs())));

  // Every step is reachable and every activity appears at least once.
  uint32_t elapsed = 0;
  bool seenWalk = false;
  bool seenStretch = false;
  bool seenGroom = false;
  bool seenJump = false;
  bool seenLook = false;
  bool seenNap = false;
  for (size_t index = 0; index < kPetDayScheduleCount; ++index) {
    uint32_t inStep = 0;
    uint32_t length = 0;
    const PetActivity activity = petScheduledActivity(elapsed, &inStep, &length);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(kPetDaySchedule[index].activity),
                          static_cast<int>(activity));
    TEST_ASSERT_EQUAL_UINT32(0, inStep);
    TEST_ASSERT_EQUAL_UINT32(kPetDaySchedule[index].durationMs, length);
    seenWalk = seenWalk || activity == PetActivity::kWalk;
    seenStretch = seenStretch || activity == PetActivity::kStretch;
    seenGroom = seenGroom || activity == PetActivity::kGroom;
    seenJump = seenJump || activity == PetActivity::kJump;
    seenLook = seenLook || activity == PetActivity::kLook;
    seenNap = seenNap || activity == PetActivity::kNap;
    elapsed += kPetDaySchedule[index].durationMs;
  }
  TEST_ASSERT_TRUE(seenWalk);
  TEST_ASSERT_TRUE(seenStretch);
  TEST_ASSERT_TRUE(seenGroom);
  TEST_ASSERT_TRUE(seenJump);
  TEST_ASSERT_TRUE(seenLook);
  TEST_ASSERT_TRUE(seenNap);
}

void test_cat_stretches_grooms_hops_looks_and_naps() {
  VirtualPet pet;
  const SensorSnapshot sensors = comfortable();
  const CivilTime noon = civilAt(12, 30, 0);

  // Stretch: eyes shut, motion lines on both sides, a proud line.
  pet.update(scheduleStart(PetActivity::kStretch), true, noon, sensors);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PetActivity::kStretch),
                        static_cast<int>(pet.scene().activity));
  TEST_ASSERT_TRUE(pet.scene().eyesClosed);
  TEST_ASSERT_EQUAL_STRING("streeetch", pet.scene().speech);
  TEST_ASSERT_EQUAL_STRING("stretch", petActivityName(pet.scene().activity));

  // Groom: a raised paw, and the tongue comes and goes.
  pet.update(scheduleStart(PetActivity::kGroom), true, noon, sensors);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PetActivity::kGroom),
                        static_cast<int>(pet.scene().activity));
  TEST_ASSERT_TRUE(pet.scene().pawRaised);
  TEST_ASSERT_FALSE(pet.scene().tongueOut);
  pet.update(scheduleStart(PetActivity::kGroom) + 1000, true, noon, sensors);
  TEST_ASSERT_TRUE(pet.scene().pawRaised);
  TEST_ASSERT_TRUE(pet.scene().tongueOut);
  TEST_ASSERT_EQUAL_STRING("lick lick", pet.scene().speech);

  // Jump: off the ground, eyes open, a cheerful line.
  pet.update(scheduleStart(PetActivity::kJump) + 1000, true, noon, sensors);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PetActivity::kJump),
                        static_cast<int>(pet.scene().activity));
  TEST_ASSERT_TRUE(pet.scene().hop > 0);
  TEST_ASSERT_FALSE(pet.scene().eyesClosed);
  TEST_ASSERT_EQUAL_STRING("hop!", pet.scene().speech);

  // Look around: the pupils follow the gaze, which changes during the step.
  pet.update(scheduleStart(PetActivity::kLook), true, noon, sensors);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PetActivity::kLook),
                        static_cast<int>(pet.scene().activity));
  TEST_ASSERT_EQUAL_INT(-1, pet.scene().gaze);
  pet.update(scheduleStart(PetActivity::kLook) + 700, true, noon, sensors);
  TEST_ASSERT_EQUAL_INT(0, pet.scene().gaze);
  pet.update(scheduleStart(PetActivity::kLook) + 1400, true, noon, sensors);
  TEST_ASSERT_EQUAL_INT(1, pet.scene().gaze);
  TEST_ASSERT_EQUAL_STRING("hmm?", pet.scene().speech);

  // Nap: a yawn first, then a quiet doze.
  pet.update(scheduleStart(PetActivity::kNap) + 600, true, noon, sensors);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PetActivity::kNap),
                        static_cast<int>(pet.scene().activity));
  TEST_ASSERT_TRUE(pet.scene().yawning);
  TEST_ASSERT_TRUE(pet.scene().eyesClosed);
  TEST_ASSERT_EQUAL_STRING("zzz...", pet.scene().speech);
  pet.update(scheduleStart(PetActivity::kNap) + 2000, true, noon, sensors);
  TEST_ASSERT_FALSE(pet.scene().yawning);
  TEST_ASSERT_EQUAL_UINT8(2, pet.scene().tailFrame);
}

void test_patting_the_cat_makes_it_happy() {
  VirtualPet pet;
  const SensorSnapshot sensors = comfortable();
  const CivilTime noon = civilAt(12, 30, 0);
  run(&pet, 1000, noon, sensors);

  pet.interact(1200);
  TEST_ASSERT_TRUE(pet.reacting());
  TEST_ASSERT_EQUAL_UINT8(1, pet.reactionCount());
  pet.update(1600, true, noon, sensors);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PetActivity::kHappy),
                        static_cast<int>(pet.scene().activity));
  TEST_ASSERT_EQUAL_STRING("yay!", pet.scene().speech);
  TEST_ASSERT_TRUE(pet.scene().heartVisible);
  TEST_ASSERT_TRUE(pet.scene().noteVisible);
  TEST_ASSERT_TRUE(pet.scene().starVisible);
  TEST_ASSERT_TRUE(pet.scene().hop > 0);
  TEST_ASSERT_FALSE(pet.scene().eyesClosed);

  // The next pat gets the other happy line.
  pet.interact(1700);
  pet.update(2000, true, noon, sensors);
  TEST_ASSERT_EQUAL_UINT8(2, pet.reactionCount());
  TEST_ASSERT_EQUAL_STRING("purr!", pet.scene().speech);

  // The reaction ends after its two and a half seconds and the routine resumes.
  pet.update(1700 + VirtualPet::kReactionDurationMs + 100, true, noon, sensors);
  TEST_ASSERT_FALSE(pet.reacting());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PetActivity::kWalk),
                        static_cast<int>(pet.scene().activity));
  TEST_ASSERT_FALSE(pet.scene().heartVisible);

  // A chime that starts at the same moment wins over the pat.
  pet.playClockChime(20000, "20:00");
  pet.interact(20000);
  pet.update(20200, true, noon, sensors);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PetActivity::kClock),
                        static_cast<int>(pet.scene().activity));
  TEST_ASSERT_TRUE(pet.scene().bubbleVisible);
}

void test_clock_chime_is_a_little_celebration() {
  VirtualPet pet;
  const SensorSnapshot sensors = comfortable();
  const CivilTime seven = civilAt(7, 0, 5);

  pet.update(0, true, seven, sensors);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PetActivity::kClock),
                        static_cast<int>(pet.scene().activity));
  TEST_ASSERT_EQUAL_STRING("07:00 !", pet.scene().bubbleText);

  // The chime hops, throws hearts, notes and sparkles, and wags its tail fast.
  pet.update(250, true, seven, sensors);
  TEST_ASSERT_TRUE(pet.scene().hop > 0);
  TEST_ASSERT_TRUE(pet.scene().heartVisible);
  TEST_ASSERT_TRUE(pet.scene().noteVisible);
  TEST_ASSERT_TRUE(pet.scene().starVisible);
  TEST_ASSERT_FALSE(pet.scene().eyesClosed);
  const uint8_t tail = pet.scene().tailFrame;
  const uint8_t frame = pet.scene().actionFrame;
  pet.update(400, true, seven, sensors);
  TEST_ASSERT_NOT_EQUAL(tail, pet.scene().tailFrame);

  // The hop comes back down: the four frames keep bouncing.
  pet.update(700, true, seven, sensors);
  TEST_ASSERT_NOT_EQUAL(frame, pet.scene().actionFrame);
  TEST_ASSERT_EQUAL_UINT8(3, pet.scene().actionFrame);
  TEST_ASSERT_TRUE(pet.scene().hop > 0);
  pet.update(880, true, seven, sensors);
  TEST_ASSERT_EQUAL_UINT8(0, pet.scene().actionFrame);
  TEST_ASSERT_EQUAL_UINT8(0, pet.scene().hop);

  // After the eight seconds everything settles back into the routine.
  pet.update(9000, true, seven, sensors);
  TEST_ASSERT_FALSE(pet.chimeActive());
  TEST_ASSERT_FALSE(pet.scene().heartVisible);
  TEST_ASSERT_FALSE(pet.scene().bubbleVisible);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(PetActivity::kWalk),
                        static_cast<int>(pet.scene().activity));
}

void test_screen_lifts_the_cat_and_scatters_icons() {
  VirtualPet pet;
  const SensorSnapshot sensors = comfortable();
  const CivilTime seven = civilAt(7, 0, 5);
  pet.update(0, true, seven, sensors);
  pet.update(500, true, seven, sensors);

  const PetScene& scene = pet.scene();
  TEST_ASSERT_EQUAL_UINT8(5, scene.hop);
  Framebuffer framebuffer;
  drawPetScreen(scene, &framebuffer);

  // The sprite's topmost row is the raised position: the cat really jumped.
  const int spriteTop = lowestLitRow(framebuffer, scene.spriteX, scene.spriteX + 23);
  TEST_ASSERT_EQUAL_INT(34 - 5, spriteTop);
  // The bubble ends above the hop, so nothing is covered.
  TEST_ASSERT_TRUE(framebuffer.pixel(125, 28));
  // Heart, note and sparkle are drawn beside the cat.
  TEST_ASSERT_TRUE(anyLit(framebuffer, scene.spriteX + 24, 29, 127, 48));
  TEST_ASSERT_TRUE(anyLit(framebuffer, 0, 29, scene.spriteX - 1, 48));

  // Two celebration frames differ, so the panel visibly moves.
  VirtualPet later;
  later.update(0, true, seven, sensors);
  later.update(900, true, seven, sensors);
  Framebuffer other;
  drawPetScreen(later.scene(), &other);
  size_t differences = 0;
  for (int y = 0; y < Framebuffer::kHeight; ++y) {
    for (int x = 0; x < Framebuffer::kWidth; ++x) {
      differences += framebuffer.pixel(x, y) != other.pixel(x, y) ? 1 : 0;
    }
  }
  TEST_ASSERT_TRUE(differences > 40);
}

void test_screen_draws_the_new_poses() {
  PetScene stretch;
  stretch.spriteX = 40;
  stretch.activity = PetActivity::kStretch;
  stretch.eyesClosed = true;
  std::snprintf(stretch.clock, sizeof(stretch.clock), "12:34");
  Framebuffer framebuffer;
  drawPetScreen(stretch, &framebuffer);
  // Motion dashes on both sides of the cat.
  TEST_ASSERT_TRUE(framebuffer.pixel(40 - 4, 34 + 15));
  TEST_ASSERT_TRUE(framebuffer.pixel(40 + 25, 34 + 15));

  PetScene groom;
  groom.spriteX = 40;
  groom.activity = PetActivity::kGroom;
  groom.pawRaised = true;
  groom.tongueOut = true;
  drawPetScreen(groom, &framebuffer);
  // The raised paw stands beside the head, the tongue sits in the mouth hole.
  TEST_ASSERT_TRUE(framebuffer.pixel(40 + 21, 34 + 7));
  TEST_ASSERT_TRUE(framebuffer.pixel(40 + 23, 34 + 9));
  TEST_ASSERT_TRUE(framebuffer.pixel(40 + 12, 34 + 10));

  PetScene nap;
  nap.spriteX = 40;
  nap.activity = PetActivity::kNap;
  nap.eyesClosed = true;
  nap.yawning = true;
  drawPetScreen(nap, &framebuffer);
  // The yawn widens the dark mouth, so the muzzle centre goes dark.
  TEST_ASSERT_FALSE(framebuffer.pixel(40 + 11, 34 + 9));
  TEST_ASSERT_FALSE(framebuffer.pixel(40 + 12, 34 + 10));
  TEST_ASSERT_TRUE(anyLit(framebuffer, 40 + 16, 16, 40 + 30, 33));
}

void test_pupils_follow_the_gaze() {
  Framebuffer left;
  Framebuffer right;
  drawPetSprite(&left, 0, 10, 10, false, -1);
  drawPetSprite(&right, 0, 10, 10, false, 1);
  // The eye holes are at columns 8..9 and 14..15 of row 7; the pupil is one
  // lit pixel inside the hole and moves with the gaze.
  TEST_ASSERT_TRUE(left.pixel(10 + 8, 10 + 7));
  TEST_ASSERT_FALSE(left.pixel(10 + 9, 10 + 7));
  TEST_ASSERT_TRUE(right.pixel(10 + 9, 10 + 7));
  TEST_ASSERT_FALSE(right.pixel(10 + 8, 10 + 7));

  // Straight ahead keeps the plain hole, so the default art is unchanged.
  Framebuffer straight;
  drawPetSprite(&straight, 0, 10, 10, false, 0);
  TEST_ASSERT_FALSE(straight.pixel(10 + 8, 10 + 7));
  TEST_ASSERT_FALSE(straight.pixel(10 + 9, 10 + 7));
}

}  // namespace

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_mood_follows_the_sensors);
  RUN_TEST(test_mood_names_and_speech);
  RUN_TEST(test_cat_walks_and_sits_on_a_cycle);
  RUN_TEST(test_walk_speed_does_not_depend_on_the_update_interval);
  RUN_TEST(test_cat_stays_on_its_track);
  RUN_TEST(test_blink_closes_the_eyes_for_a_moment);
  RUN_TEST(test_cat_sleeps_at_night);
  RUN_TEST(test_clock_chime_fires_once_an_hour);
  RUN_TEST(test_chime_needs_a_synced_clock_and_the_first_seconds);
  RUN_TEST(test_test_chime_replays_with_the_given_time);
  RUN_TEST(test_scene_carries_the_readings_and_speech);
  RUN_TEST(test_screen_draws_the_cat_on_the_ground);
  RUN_TEST(test_screen_draws_the_chime_bubble_with_its_clock);
  RUN_TEST(test_eyes_close_over_the_sprite_holes);
  RUN_TEST(test_day_schedule_covers_every_move);
  RUN_TEST(test_cat_stretches_grooms_hops_looks_and_naps);
  RUN_TEST(test_patting_the_cat_makes_it_happy);
  RUN_TEST(test_clock_chime_is_a_little_celebration);
  RUN_TEST(test_screen_lifts_the_cat_and_scatters_icons);
  RUN_TEST(test_screen_draws_the_new_poses);
  RUN_TEST(test_pupils_follow_the_gaze);
  return UNITY_END();
}

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
  // 18 px per second from the left edge: 8 + 30 after three seconds.
  TEST_ASSERT_EQUAL_INT(VirtualPet::kWalkLeft + 30, pet.scene().spriteX);

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
  TEST_ASSERT_TRUE(framebuffer.pixel(125, 30));
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

}  // namespace

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_mood_follows_the_sensors);
  RUN_TEST(test_mood_names_and_speech);
  RUN_TEST(test_cat_walks_and_sits_on_a_cycle);
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
  return UNITY_END();
}

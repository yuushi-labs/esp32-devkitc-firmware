// VirtualPet: the boot screen of the station.
//
// Everything here is pure C++ and a function of the monotonic millisecond
// counter plus the civil time the NTP clock reports, so the walk cycle, the
// stretch/groom/hop/look/nap routine, the blink, the night pose, the once-an-
// hour clock chime and the reaction to a button are unit tested on the host.
// The renderer in pet_screen.cpp only turns a PetScene into pixels.
#pragma once

#include <cstddef>
#include <cstdint>

#include "net/ntp_clock.h"
#include "sensors/sensor_snapshot.h"

namespace fw {

// What the cat is doing.
enum class PetActivity {
  kWalk = 0,
  kSit,
  kSleep,
  // The hourly chime: the cat holds up a clock, hops and throws hearts.
  kClock,
  // v0.4.1 everyday moves, mixed into the walk/sit routine.
  kStretch,
  kJump,
  kGroom,
  kLook,
  kNap,
  // The reaction to the encoder or PSH on the pet screen.
  kHappy,
};

const char* petActivityName(PetActivity activity);

// One step of the daytime routine. The cat walks for a while, sits, and every
// so often stretches, grooms, hops, looks around or dozes off, so the screen
// never repeats the same walk/sit loop for long.
struct PetScheduleStep {
  PetActivity activity;
  uint32_t durationMs;
};

constexpr PetScheduleStep kPetDaySchedule[] = {
    {PetActivity::kWalk, 10000},    {PetActivity::kSit, 6000},
    {PetActivity::kWalk, 10000},    {PetActivity::kSit, 6000},
    {PetActivity::kStretch, 3000},  {PetActivity::kWalk, 10000},
    {PetActivity::kSit, 6000},      {PetActivity::kGroom, 4000},
    {PetActivity::kWalk, 10000},    {PetActivity::kSit, 6000},
    {PetActivity::kJump, 2400},     {PetActivity::kWalk, 10000},
    {PetActivity::kSit, 6000},      {PetActivity::kLook, 4000},
    {PetActivity::kNap, 8000},
};
constexpr size_t kPetDayScheduleCount =
    sizeof(kPetDaySchedule) / sizeof(kPetDaySchedule[0]);

// Total length of the routine, in milliseconds.
uint32_t petDayCycleMs();
// The step `elapsedMs` falls into, with the time already spent in it.
PetActivity petScheduledActivity(uint32_t elapsedMs,
                                 uint32_t* elapsedInStep = nullptr,
                                 uint32_t* stepLength = nullptr);

// One word for the environment the cat is sitting in.
enum class PetMood {
  kUnknown = 0,
  kCold,
  kComfortable,
  kWarm,
  kHot,
  kDry,
  kHumid,
};

// "cold" / "nice" / "warm" / "hot" / "dry" / "humid" / "unknown".
const char* petMoodName(PetMood mood);
// Short label for the top right of the panel: "cold", "nice", "warm", "hot!",
// "dry!", "humid", "--".
const char* petMoodLabel(PetMood mood);
// What the cat says about it: "brr...", "nice day", "so hot", "dry air"...
const char* petMoodSpeech(PetMood mood);
PetMood petMoodFromSensors(const SensorSnapshot& sensors);

struct PetScene {
  PetActivity activity = PetActivity::kWalk;
  PetMood mood = PetMood::kUnknown;
  // Left edge of the sprite, in pixels, and the walk bob (0 or 1 pixel up).
  int16_t spriteX = 0;
  uint8_t bob = 0;
  // Pixels off the ground while stretching up, hopping or celebrating.
  uint8_t hop = 0;
  // 0..3 phase of the current action: moves the floating icons so nothing on
  // the panel stands still.
  uint8_t actionFrame = 0;
  // Hearts, notes and sparkles around the cat while it celebrates (the hourly
  // chime and the reaction to a button).
  bool heartVisible = false;
  bool noteVisible = false;
  bool starVisible = false;
  // Grooming: a raised paw and the tongue going in and out.
  bool pawRaised = false;
  bool tongueOut = false;
  // Napping: the first moments are a yawn with the mouth open.
  bool yawning = false;
  // Looking around: -1 left, 0 straight, +1 right (the pupils follow).
  int8_t gaze = 0;
  // 0..2: tail up, middle, down (see pet_sprites.h).
  uint8_t tailFrame = 0;
  bool eyesClosed = false;
  // The clock the cat brings for the hourly chime.
  bool clockVisible = false;
  // Current time for the top line, "--:--" until NTP has synced.
  char clock[8] = "--:--";
  // The hour the chime announces, "07:00".
  char clockText[8] = "--:--";
  bool bubbleVisible = false;
  // "07:00 !" while the chime plays.
  char bubbleText[16] = {0};
  // Speech line under the clock: the mood phrase, "zzz..." while asleep.
  char speech[20] = {0};
  // Compact sensor line for the bottom of the panel, empty without sensors.
  char sensors[20] = {0};
};

// The animated pet. All time is passed in, so the tests drive it exactly.
class VirtualPet {
 public:
  // Sprite size and the horizontal track it walks on.
  static constexpr int kSpriteWidth = 24;
  static constexpr int kSpriteHeight = 20;
  static constexpr int kWalkLeft = 8;
  static constexpr int kWalkRight = 128 - kSpriteWidth - 8;
  // One left-to-right sweep at this speed.
  static constexpr int kWalkPixelsPerSecond = 18;

  // The first two steps of the routine, kept as named constants because the
  // walk phase is what the tests measure the speed against.
  static constexpr uint32_t kWalkPhaseMs = 10000;
  static constexpr uint32_t kSitPhaseMs = 6000;
  static constexpr uint32_t kActivityCycleMs = kWalkPhaseMs + kSitPhaseMs;
  // Blink: eyes closed for the first 180 ms of every 3200 ms.
  static constexpr uint32_t kBlinkPeriodMs = 3200;
  static constexpr uint32_t kBlinkDurationMs = 180;
  // Tail sways every 400 ms.
  static constexpr uint32_t kTailPeriodMs = 400;
  // The celebration (chime and reaction) wags the tail and moves the icons
  // faster than the everyday 400 ms sway.
  static constexpr uint32_t kCelebrationFrameMs = 220;
  // Noise-free hour range in which the cat sleeps (23:00-05:59).
  static constexpr uint8_t kNightStartHour = 23;
  static constexpr uint8_t kNightEndHour = 6;

  // The chime fires on the first update in the first 20 seconds of an hour and
  // plays for 8 s.
  static constexpr uint8_t kChimeWindowSeconds = 20;
  static constexpr uint32_t kChimeDurationMs = 8000;
  // How long the cat stays happy after the encoder or PSH is used.
  static constexpr uint32_t kReactionDurationMs = 2400;

  VirtualPet();

  // Advances the animation to `nowMs`. `timeSynced` gates the chime and the
  // night pose: without NTP there is no hour to announce. The sensor snapshot
  // drives the pet's mood and the small readings line.
  void update(uint32_t nowMs, bool timeSynced, const CivilTime& now,
              const SensorSnapshot& sensors);

  // The cat notices the person: a hop, hearts and a happy line for a few
  // seconds. Called from the encoder and PSH on the pet screen.
  void interact(uint32_t nowMs);
  bool reacting() const { return reactionActive_; }
  uint8_t reactionCount() const { return reactionCount_; }

  const PetScene& scene() const { return scene_; }
  PetActivity activity() const { return scene_.activity; }
  bool chimeActive() const { return chimeActive_; }
  // Hour of the last chime (0..23), or -1 before the first one.
  int lastChimeHour() const { return lastChimeHour_; }
  // True once for every automatic chime, so the app can log it.
  bool consumeChimeEvent();

  // Plays the chime now with the supplied "HH:MM" text: the web remote and the
  // `pet clock` command use this to check the animation on the device.
  void playClockChime(uint32_t nowMs, const char* clockText);

  void reset();

 private:
  void startChime(uint32_t nowMs, uint8_t hour);
  void moveSprite(uint32_t nowMs, bool walking);
  const char* reactionSpeech() const;

  PetScene scene_;
  int16_t spriteX_ = kWalkLeft;
  // Leftover fraction of a pixel (in 1/1000 px), so the walk speed does not
  // depend on how often update() is called.
  int32_t walkRemainder_ = 0;
  int8_t direction_ = 1;
  uint32_t lastUpdateMs_ = 0;
  uint32_t chimeUntilMs_ = 0;
  bool chimeActive_ = false;
  bool chimeEvent_ = false;
  int lastChimeHour_ = -1;
  int32_t lastChimeStamp_ = -1;
  uint32_t reactionStartMs_ = 0;
  uint32_t reactionUntilMs_ = 0;
  bool reactionActive_ = false;
  uint8_t reactionCount_ = 0;
  PetMood mood_ = PetMood::kUnknown;
};

}  // namespace fw

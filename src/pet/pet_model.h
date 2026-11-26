// VirtualPet: the boot screen of the station.
//
// Everything here is pure C++ and a function of the monotonic millisecond
// counter plus the civil time the NTP clock reports, so the walk cycle, the
// blink, the night pose and the once-an-hour clock chime are unit tested on the
// host. The renderer in pet_screen.cpp only turns a PetScene into pixels.
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
  // The hourly chime: the cat holds up a clock and says the time.
  kClock,
};

const char* petActivityName(PetActivity activity);

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
  // Speech line under the clock: the mood phrase, "zzz..." at night.
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

  // Walk for this long, then sit for this long, then walk again.
  static constexpr uint32_t kWalkPhaseMs = 10000;
  static constexpr uint32_t kSitPhaseMs = 6000;
  static constexpr uint32_t kActivityCycleMs = kWalkPhaseMs + kSitPhaseMs;
  // Blink: eyes closed for the first 180 ms of every 3200 ms.
  static constexpr uint32_t kBlinkPeriodMs = 3200;
  static constexpr uint32_t kBlinkDurationMs = 180;
  // Tail sways every 400 ms.
  static constexpr uint32_t kTailPeriodMs = 400;
  // Noise-free hour range in which the cat sleeps (23:00-05:59).
  static constexpr uint8_t kNightStartHour = 23;
  static constexpr uint8_t kNightEndHour = 6;

  // The chime fires on the first update in the first 20 seconds of an hour and
  // plays for 8 s.
  static constexpr uint8_t kChimeWindowSeconds = 20;
  static constexpr uint32_t kChimeDurationMs = 8000;

  VirtualPet();

  // Advances the animation to `nowMs`. `timeSynced` gates the chime and the
  // night pose: without NTP there is no hour to announce. The sensor snapshot
  // drives the pet's mood and the small readings line.
  void update(uint32_t nowMs, bool timeSynced, const CivilTime& now,
              const SensorSnapshot& sensors);

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

  PetScene scene_;
  int16_t spriteX_ = kWalkLeft;
  int8_t direction_ = 1;
  uint32_t lastUpdateMs_ = 0;
  uint32_t chimeUntilMs_ = 0;
  bool chimeActive_ = false;
  bool chimeEvent_ = false;
  int lastChimeHour_ = -1;
  int32_t lastChimeStamp_ = -1;
  PetMood mood_ = PetMood::kUnknown;
};

}  // namespace fw

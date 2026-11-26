#include "pet/pet_model.h"

#include <cstdio>

namespace fw {
namespace {

// A chime stamp of (month, day, hour) so the next day's same hour fires again.
int32_t chimeStamp(const CivilTime& now) {
  return ((static_cast<int32_t>(now.month) * 32) + now.day) * 24 + now.hour;
}

bool isNightHour(uint8_t hour) {
  return hour >= VirtualPet::kNightStartHour || hour < VirtualPet::kNightEndHour;
}

// Hop heights in pixels for the four celebration frames. The chime keeps the
// cat just under the speech bubble (the sprite must not cover it).
constexpr uint8_t kChimeHops[4] = {0, 3, 5, 2};
constexpr uint8_t kReactionHops[4] = {0, 4, 5, 2};
constexpr uint8_t kJumpHops[4] = {0, 3, 5, 2};
// The look-around gaze: left, straight, right, straight.
constexpr int8_t kGazePattern[4] = {-1, 0, 1, 0};

}  // namespace

const char* petActivityName(PetActivity activity) {
  switch (activity) {
    case PetActivity::kWalk:
      return "walk";
    case PetActivity::kSit:
      return "sit";
    case PetActivity::kSleep:
      return "sleep";
    case PetActivity::kClock:
      return "clock";
    case PetActivity::kStretch:
      return "stretch";
    case PetActivity::kJump:
      return "jump";
    case PetActivity::kGroom:
      return "groom";
    case PetActivity::kLook:
      return "look";
    case PetActivity::kNap:
      return "nap";
    case PetActivity::kHappy:
      return "happy";
  }
  return "walk";
}

uint32_t petDayCycleMs() {
  uint32_t total = 0;
  for (size_t index = 0; index < kPetDayScheduleCount; ++index) {
    total += kPetDaySchedule[index].durationMs;
  }
  return total;
}

PetActivity petScheduledActivity(uint32_t elapsedMs, uint32_t* elapsedInStep,
                                 uint32_t* stepLength) {
  const uint32_t cycle = petDayCycleMs();
  uint32_t offset = cycle > 0 ? elapsedMs % cycle : 0;
  for (size_t index = 0; index < kPetDayScheduleCount; ++index) {
    const PetScheduleStep& step = kPetDaySchedule[index];
    if (offset < step.durationMs) {
      if (elapsedInStep != nullptr) {
        *elapsedInStep = offset;
      }
      if (stepLength != nullptr) {
        *stepLength = step.durationMs;
      }
      return step.activity;
    }
    offset -= step.durationMs;
  }
  if (elapsedInStep != nullptr) {
    *elapsedInStep = 0;
  }
  if (stepLength != nullptr) {
    *stepLength = 0;
  }
  return PetActivity::kWalk;
}

const char* petMoodName(PetMood mood) {
  switch (mood) {
    case PetMood::kCold:
      return "cold";
    case PetMood::kComfortable:
      return "nice";
    case PetMood::kWarm:
      return "warm";
    case PetMood::kHot:
      return "hot";
    case PetMood::kDry:
      return "dry";
    case PetMood::kHumid:
      return "humid";
    case PetMood::kUnknown:
      break;
  }
  return "unknown";
}

const char* petMoodLabel(PetMood mood) {
  switch (mood) {
    case PetMood::kCold:
      return "cold";
    case PetMood::kComfortable:
      return "nice";
    case PetMood::kWarm:
      return "warm";
    case PetMood::kHot:
      return "hot!";
    case PetMood::kDry:
      return "dry!";
    case PetMood::kHumid:
      return "humid";
    case PetMood::kUnknown:
      break;
  }
  return "--";
}

const char* petMoodSpeech(PetMood mood) {
  switch (mood) {
    case PetMood::kCold:
      return "brr...";
    case PetMood::kComfortable:
      return "nice day";
    case PetMood::kWarm:
      return "warm";
    case PetMood::kHot:
      return "so hot...";
    case PetMood::kDry:
      return "dry air";
    case PetMood::kHumid:
      return "humid...";
    case PetMood::kUnknown:
      break;
  }
  return "hello";
}

PetMood petMoodFromSensors(const SensorSnapshot& sensors) {
  const bool hasTemperature = sensors.hasTemperature();
  const bool hasHumidity = sensors.hasHumidity();
  if (!hasTemperature && !hasHumidity) {
    return PetMood::kUnknown;
  }
  const float temperature = sensors.displayTemperatureC();
  if (hasTemperature && temperature >= 30.0f) {
    return PetMood::kHot;
  }
  if (hasTemperature && temperature <= 10.0f) {
    return PetMood::kCold;
  }
  if (hasHumidity && sensors.humidityPct < 35.0f) {
    return PetMood::kDry;
  }
  if (hasHumidity && sensors.humidityPct > 70.0f) {
    return PetMood::kHumid;
  }
  if (hasTemperature && temperature >= 26.0f) {
    return PetMood::kWarm;
  }
  return PetMood::kComfortable;
}

VirtualPet::VirtualPet() { reset(); }

void VirtualPet::reset() {
  scene_ = PetScene();
  spriteX_ = kWalkLeft;
  direction_ = 1;
  walkRemainder_ = 0;
  lastUpdateMs_ = 0;
  chimeUntilMs_ = 0;
  chimeActive_ = false;
  chimeEvent_ = false;
  lastChimeHour_ = -1;
  lastChimeStamp_ = -1;
  reactionStartMs_ = 0;
  reactionUntilMs_ = 0;
  reactionActive_ = false;
  reactionCount_ = 0;
  mood_ = PetMood::kUnknown;
  std::snprintf(scene_.clockText, sizeof(scene_.clockText), "--:--");
  std::snprintf(scene_.speech, sizeof(scene_.speech), "%s", petMoodSpeech(mood_));
}

bool VirtualPet::consumeChimeEvent() {
  const bool fired = chimeEvent_;
  chimeEvent_ = false;
  return fired;
}

void VirtualPet::startChime(uint32_t nowMs, uint8_t hour) {
  chimeActive_ = true;
  chimeUntilMs_ = nowMs + kChimeDurationMs;
  chimeEvent_ = true;
  lastChimeHour_ = hour;
  std::snprintf(scene_.clockText, sizeof(scene_.clockText), "%02u:00",
                static_cast<unsigned>(hour % 24));
  std::snprintf(scene_.bubbleText, sizeof(scene_.bubbleText), "%s !",
                scene_.clockText);
  scene_.clockVisible = true;
  scene_.bubbleVisible = true;
  scene_.eyesClosed = false;
}

void VirtualPet::playClockChime(uint32_t nowMs, const char* clockText) {
  chimeActive_ = true;
  chimeUntilMs_ = nowMs + kChimeDurationMs;
  if (clockText != nullptr && clockText[0] != '\0') {
    std::snprintf(scene_.clockText, sizeof(scene_.clockText), "%s", clockText);
  }
  std::snprintf(scene_.bubbleText, sizeof(scene_.bubbleText), "%s !",
                scene_.clockText);
  scene_.clockVisible = true;
  scene_.bubbleVisible = true;
  scene_.activity = PetActivity::kClock;
  scene_.eyesClosed = false;
}

const char* VirtualPet::reactionSpeech() const {
  // Every other pat gets the other happy line, so repeated turns of the
  // encoder do not show the same bubble four times in a row.
  return (reactionCount_ % 2) == 0 ? "purr!" : "yay!";
}

void VirtualPet::interact(uint32_t nowMs) {
  reactionStartMs_ = nowMs;
  reactionUntilMs_ = nowMs + kReactionDurationMs;
  reactionActive_ = true;
  ++reactionCount_;
  if (chimeActive_) {
    // The chime is already the big celebration; the cat keeps hopping and the
    // count still advances so the next pat gets the other line.
    return;
  }
  scene_.activity = PetActivity::kHappy;
  scene_.hop = 0;
  scene_.actionFrame = 0;
  scene_.heartVisible = true;
  scene_.noteVisible = true;
  scene_.starVisible = true;
  scene_.eyesClosed = false;
  scene_.gaze = 0;
  std::snprintf(scene_.speech, sizeof(scene_.speech), "%s", reactionSpeech());
}

void VirtualPet::moveSprite(uint32_t elapsedMs, bool walking) {
  if (!walking || elapsedMs == 0) {
    return;
  }
  // The fraction of a pixel is carried to the next update, so 18 px/s holds
  // whether loop() calls update() every 10 ms or every 250 ms.
  walkRemainder_ += static_cast<int32_t>(kWalkPixelsPerSecond) *
                    static_cast<int32_t>(elapsedMs);
  const int32_t whole = walkRemainder_ / 1000;
  walkRemainder_ -= whole * 1000;
  if (whole == 0) {
    return;
  }
  int32_t position = spriteX_ + (direction_ * whole);
  if (position >= kWalkRight) {
    position = kWalkRight;
    direction_ = -1;
  } else if (position <= kWalkLeft) {
    position = kWalkLeft;
    direction_ = 1;
  }
  spriteX_ = static_cast<int16_t>(position);
}

void VirtualPet::update(uint32_t nowMs, bool timeSynced, const CivilTime& now,
                        const SensorSnapshot& sensors) {
  mood_ = petMoodFromSensors(sensors);
  uint32_t elapsedMs = nowMs - lastUpdateMs_;
  lastUpdateMs_ = nowMs;
  // A paused loop (deep sleep, a long flash write) must not teleport the cat.
  if (elapsedMs > 2000) {
    elapsedMs = 2000;
  }

  if (chimeActive_ && static_cast<int32_t>(nowMs - chimeUntilMs_) >= 0) {
    chimeActive_ = false;
  }
  if (reactionActive_ && static_cast<int32_t>(nowMs - reactionUntilMs_) >= 0) {
    reactionActive_ = false;
  }

  // The chime fires once, in the first seconds of an hour, when the clock has
  // a real time to announce. The stamp keeps the next day's same hour working.
  if (timeSynced && now.minute == 0 && now.second < kChimeWindowSeconds &&
      chimeStamp(now) != lastChimeStamp_) {
    lastChimeStamp_ = chimeStamp(now);
    startChime(nowMs, now.hour);
    // The hour announcement is the bigger moment: a pat that arrived in the
    // same second gives way to it.
    reactionActive_ = false;
  }

  uint32_t stepElapsedMs = 0;
  const PetActivity scheduled =
      petScheduledActivity(nowMs, &stepElapsedMs, nullptr);
  PetActivity activity = scheduled;
  if (chimeActive_) {
    activity = PetActivity::kClock;
  } else if (reactionActive_) {
    activity = PetActivity::kHappy;
  } else if (timeSynced && isNightHour(now.hour)) {
    activity = PetActivity::kSleep;
  }
  moveSprite(elapsedMs, activity == PetActivity::kWalk);

  uint8_t hop = 0;
  uint8_t frame = 0;
  switch (activity) {
    case PetActivity::kClock:
      // Four bouncing frames for the whole eight seconds; the tail wags along.
      frame = static_cast<uint8_t>((nowMs / kCelebrationFrameMs) % 4);
      hop = kChimeHops[frame];
      break;
    case PetActivity::kHappy:
      frame = static_cast<uint8_t>(
          ((nowMs - reactionStartMs_) / kCelebrationFrameMs) % 4);
      hop = kReactionHops[frame];
      break;
    case PetActivity::kJump:
      frame = static_cast<uint8_t>((stepElapsedMs / 200) % 4);
      hop = kJumpHops[frame];
      break;
    case PetActivity::kStretch:
      // Motion lines on both sides while the cat pushes its front paws out.
      frame = static_cast<uint8_t>((stepElapsedMs / 300) % 4);
      break;
    case PetActivity::kGroom:
      frame = static_cast<uint8_t>((stepElapsedMs / 240) % 4);
      break;
    case PetActivity::kLook:
      frame = static_cast<uint8_t>((stepElapsedMs / 700) % 4);
      break;
    case PetActivity::kNap:
      frame = static_cast<uint8_t>((stepElapsedMs / 400) % 4);
      break;
    case PetActivity::kWalk:
    case PetActivity::kSit:
    case PetActivity::kSleep:
      frame = static_cast<uint8_t>((nowMs / kCelebrationFrameMs) % 4);
      break;
  }

  scene_.activity = activity;
  scene_.mood = mood_;
  scene_.spriteX = spriteX_;
  scene_.bob = activity == PetActivity::kWalk ? (nowMs / 200) % 2 : 0;
  scene_.hop = hop;
  scene_.actionFrame = frame;

  if (activity == PetActivity::kSleep || activity == PetActivity::kNap) {
    scene_.tailFrame = 2;
  } else if (activity == PetActivity::kClock || activity == PetActivity::kHappy) {
    // A happy cat wags its tail much faster than the everyday 400 ms sway.
    scene_.tailFrame = static_cast<uint8_t>((nowMs / 150) % 3);
  } else if (activity == PetActivity::kJump) {
    scene_.tailFrame = 0;
  } else {
    scene_.tailFrame = static_cast<uint8_t>((nowMs / kTailPeriodMs) % 3);
  }

  const bool blink = (nowMs % kBlinkPeriodMs) < kBlinkDurationMs;
  const bool celebrating =
      activity == PetActivity::kClock || activity == PetActivity::kHappy;
  scene_.eyesClosed = activity == PetActivity::kSleep ||
                      activity == PetActivity::kNap ||
                      activity == PetActivity::kStretch ||
                      (!celebrating && blink);

  scene_.heartVisible = celebrating;
  scene_.noteVisible = celebrating;
  scene_.starVisible = celebrating;
  scene_.pawRaised = activity == PetActivity::kGroom;
  scene_.tongueOut =
      activity == PetActivity::kGroom && ((stepElapsedMs / 240) % 3 != 0);
  scene_.yawning = activity == PetActivity::kNap && stepElapsedMs < 900;
  scene_.gaze = activity == PetActivity::kLook
                    ? kGazePattern[(stepElapsedMs / 700) % 4]
                    : 0;

  scene_.clockVisible = chimeActive_;
  scene_.bubbleVisible = chimeActive_;
  if (timeSynced) {
    std::snprintf(scene_.clock, sizeof(scene_.clock), "%02u:%02u",
                  static_cast<unsigned>(now.hour % 24),
                  static_cast<unsigned>(now.minute % 60));
  } else {
    std::snprintf(scene_.clock, sizeof(scene_.clock), "--:--");
  }
  if (!chimeActive_) {
    scene_.clockText[0] = '\0';
    std::snprintf(scene_.clockText, sizeof(scene_.clockText), "--:--");
    scene_.bubbleText[0] = '\0';
  }

  const char* speech = petMoodSpeech(mood_);
  switch (activity) {
    case PetActivity::kSleep:
    case PetActivity::kNap:
      speech = "zzz...";
      break;
    case PetActivity::kHappy:
      speech = reactionSpeech();
      break;
    case PetActivity::kStretch:
      speech = "streeetch";
      break;
    case PetActivity::kJump:
      speech = "hop!";
      break;
    case PetActivity::kGroom:
      speech = "lick lick";
      break;
    case PetActivity::kLook:
      speech = "hmm?";
      break;
    default:
      break;
  }
  std::snprintf(scene_.speech, sizeof(scene_.speech), "%s", speech);

  char line[20] = {0};
  if (sensors.hasTemperature() && sensors.hasHumidity()) {
    std::snprintf(line, sizeof(line), "%.1fC %.0f%%",
                  static_cast<double>(sensors.displayTemperatureC()),
                  static_cast<double>(sensors.humidityPct));
  } else if (sensors.hasTemperature()) {
    std::snprintf(line, sizeof(line), "%.1fC",
                  static_cast<double>(sensors.displayTemperatureC()));
  } else if (sensors.hasHumidity()) {
    std::snprintf(line, sizeof(line), "%.0f%%",
                  static_cast<double>(sensors.humidityPct));
  }
  std::snprintf(scene_.sensors, sizeof(scene_.sensors), "%s", line);
}

}  // namespace fw

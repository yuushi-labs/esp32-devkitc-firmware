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
  }
  return "walk";
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
  lastUpdateMs_ = 0;
  chimeUntilMs_ = 0;
  chimeActive_ = false;
  chimeEvent_ = false;
  lastChimeHour_ = -1;
  lastChimeStamp_ = -1;
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

void VirtualPet::moveSprite(uint32_t elapsedMs, bool walking) {
  if (!walking || elapsedMs == 0) {
    return;
  }
  int32_t x = spriteX_ + direction_ *
                             static_cast<int32_t>(kWalkPixelsPerSecond *
                                                  elapsedMs / 1000);
  // A step shorter than a millisecond would otherwise leave the cat standing.
  if (x == spriteX_) {
    x = spriteX_ + direction_;
  }
  if (x >= kWalkRight) {
    x = kWalkRight;
    direction_ = -1;
  } else if (x <= kWalkLeft) {
    x = kWalkLeft;
    direction_ = 1;
  }
  spriteX_ = static_cast<int16_t>(x);
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

  // The chime fires once, in the first seconds of an hour, when the clock has
  // a real time to announce. The stamp keeps the next day's same hour working.
  if (timeSynced && now.minute == 0 && now.second < kChimeWindowSeconds &&
      chimeStamp(now) != lastChimeStamp_) {
    lastChimeStamp_ = chimeStamp(now);
    startChime(nowMs, now.hour);
  }

  PetActivity activity = PetActivity::kWalk;
  if (chimeActive_) {
    activity = PetActivity::kClock;
  } else if (timeSynced && isNightHour(now.hour)) {
    activity = PetActivity::kSleep;
  } else if ((nowMs % kActivityCycleMs) >= kWalkPhaseMs) {
    activity = PetActivity::kSit;
  }
  moveSprite(elapsedMs, activity == PetActivity::kWalk);

  scene_.activity = activity;
  scene_.mood = mood_;
  scene_.spriteX = spriteX_;
  scene_.bob = activity == PetActivity::kWalk ? (nowMs / 200) % 2 : 0;
  if (activity == PetActivity::kSleep) {
    scene_.tailFrame = 2;
  } else if (activity == PetActivity::kClock) {
    scene_.tailFrame = 0;
  } else {
    scene_.tailFrame = static_cast<uint8_t>((nowMs / kTailPeriodMs) % 3);
  }
  scene_.eyesClosed = activity == PetActivity::kSleep ||
                      (activity != PetActivity::kClock &&
                       (nowMs % kBlinkPeriodMs) < kBlinkDurationMs);

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
  if (activity == PetActivity::kSleep) {
    std::snprintf(scene_.speech, sizeof(scene_.speech), "zzz...");
  } else {
    std::snprintf(scene_.speech, sizeof(scene_.speech), "%s",
                  petMoodSpeech(mood_));
  }

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

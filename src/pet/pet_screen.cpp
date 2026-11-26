#include "pet/pet_screen.h"

#include <cmath>

#include "pet/pet_sprites.h"

namespace fw {
namespace {

// Vertical layout of the panel, top to bottom.
constexpr int kClockLineY = 0;
constexpr int kBubbleX = 2;
constexpr int kBubbleY = 7;
constexpr int kBubbleWidth = 124;
// The bubble is two pixels shorter than in v0.4.0 so the celebrating cat can
// hop up to five pixels without its ears touching the frame.
constexpr int kBubbleHeight = 22;
constexpr int kBubbleClockX = 6;
constexpr int kBubbleClockY = 11;
constexpr int kBubbleTextX = 26;
constexpr int kBubbleTextY = 12;
constexpr int kSpeechY = 22;
constexpr int kSpriteTop = 34;
constexpr int kGroundY = 55;
constexpr int kSensorsY = 56;
// Where the celebration icons float, to the right and left of the cat.
constexpr int kIconRightY = 30;
constexpr int kIconLeftY = 40;
constexpr int kIconSparkleY = 44;

constexpr uint8_t kSpriteEyeLeftX = 8;
constexpr uint8_t kSpriteEyeRightX = 14;
constexpr uint8_t kSpriteEyeY = 7;

const PetSprite* spriteForTail(uint8_t tailFrame) {
  switch (tailFrame) {
    case 1:
      return &kCatTailMid;
    case 2:
      return &kCatTailDown;
    default:
      return &kCatTailUp;
  }
}

void drawSprite(Framebuffer& framebuffer, const PetSprite& sprite, int x, int y) {
  for (uint8_t row = 0; row < sprite.height; ++row) {
    const uint32_t bits = sprite.rows[row];
    for (uint8_t column = 0; column < sprite.width; ++column) {
      if ((bits & (1u << column)) != 0) {
        framebuffer.setPixel(x + column, y + row, true);
      }
    }
  }
}

int clampInt(int value, int low, int high) {
  if (value < low) {
    return low;
  }
  if (value > high) {
    return high;
  }
  return value;
}

// Draws a floating icon, keeping the whole bitmap on the panel.
void drawIcon(Framebuffer& framebuffer, const PetSprite& icon, int x, int y) {
  const int left = clampInt(x, 0, Framebuffer::kWidth - icon.width);
  const int top = clampInt(y, 0, Framebuffer::kHeight - icon.height);
  drawSprite(framebuffer, icon, left, top);
}

// Centres `text` at `scale` on the panel and draws it at `y`.
void drawCentered(Framebuffer& framebuffer, const char* text, int y,
                  uint8_t scale) {
  if (text == nullptr || text[0] == '\0') {
    return;
  }
  const size_t width = Framebuffer::textWidth(text, scale);
  const int x = width < Framebuffer::kWidth
                    ? static_cast<int>((Framebuffer::kWidth - width) / 2)
                    : 0;
  framebuffer.drawText(x, y, text, scale);
}

// Right aligns `text` so it ends at the panel edge.
void drawRightAligned(Framebuffer& framebuffer, const char* text, int y) {
  if (text == nullptr || text[0] == '\0') {
    return;
  }
  const size_t width = Framebuffer::textWidth(text);
  const int x = width < Framebuffer::kWidth
                    ? static_cast<int>(Framebuffer::kWidth - width)
                    : 0;
  framebuffer.drawText(x, y, text);
}

void drawGround(Framebuffer& framebuffer) {
  for (int x = 0; x < Framebuffer::kWidth; x += 4) {
    framebuffer.drawHLine(x, kGroundY, 2, true);
  }
}

void drawSleepMarks(Framebuffer& framebuffer, int spriteX) {
  framebuffer.drawText(spriteX + 16, 26, "z");
  framebuffer.drawText(spriteX + 21, 21, "z");
  framebuffer.drawText(spriteX + 26, 16, "z");
}

// Hearts, notes and sparkles that move with the action frames: the panel is
// monochrome, so the motion is what makes the celebration read as joy.
void drawCelebrationIcons(Framebuffer& framebuffer, const PetScene& scene) {
  const int frame = scene.actionFrame & 3;
  const int leftX = scene.spriteX - 7;
  const int rightX = scene.spriteX + VirtualPet::kSpriteWidth;
  if (scene.heartVisible) {
    drawIcon(framebuffer, kIconHeart, rightX, kIconRightY + frame * 2);
  }
  if (scene.noteVisible) {
    drawIcon(framebuffer, kIconNote, leftX, kIconLeftY - frame * 2);
  }
  if (scene.starVisible) {
    drawIcon(framebuffer, kIconSparkle, rightX, kIconSparkleY - frame * 2);
  }
}

// Motion dashes under a stretching cat: the front paws push forward and the
// body pulls back, which the 24x20 sprite cannot show by itself.
void drawStretchMarks(Framebuffer& framebuffer, int spriteX, int spriteY) {
  const int y = spriteY + 15;
  framebuffer.drawHLine(spriteX - 4, y, 3, true);
  framebuffer.drawHLine(spriteX + VirtualPet::kSpriteWidth + 1, y, 3, true);
}

}  // namespace

void drawPetSprite(Framebuffer* out, uint8_t tailFrame, int x, int y,
                   bool eyesClosed, int8_t gaze) {
  if (out == nullptr) {
    return;
  }
  drawSprite(*out, *spriteForTail(tailFrame), x, y);
  if (eyesClosed) {
    // The eye holes are 2x2; a lit row across the lower row reads as a blink.
    out->fillRect(x + kSpriteEyeLeftX, y + kSpriteEyeY, 2, 1, true);
    out->fillRect(x + kSpriteEyeRightX, y + kSpriteEyeY, 2, 1, true);
    return;
  }
  if (gaze != 0) {
    // A single lit pixel inside each eye hole is the pupil; moving it left or
    // right inside the 2 pixel hole is what makes the cat look around.
    const int offset = gaze > 0 ? 1 : 0;
    out->setPixel(x + kSpriteEyeLeftX + offset, y + kSpriteEyeY, true);
    out->setPixel(x + kSpriteEyeRightX + offset, y + kSpriteEyeY, true);
  }
}

void drawPetClock(Framebuffer* out, int x, int y) {
  if (out == nullptr) {
    return;
  }
  constexpr int kCentreX = 8;
  constexpr int kCentreY = 8;
  constexpr int kRadius = 7;
  // Face outline.
  for (int angle = 0; angle < 360; angle += 6) {
    const float radians = static_cast<float>(angle) * 3.14159265f / 180.0f;
    const int px = kCentreX + static_cast<int>(kRadius * std::cos(radians) + 0.5f);
    const int py = kCentreY + static_cast<int>(kRadius * std::sin(radians) + 0.5f);
    out->setPixel(x + px, y + py, true);
  }
  // Twelve, three, six and nine o'clock ticks.
  out->setPixel(x + kCentreX, y + 1, true);
  out->setPixel(x + kCentreX, y + 2, true);
  out->setPixel(x + 14, y + kCentreY, true);
  out->setPixel(x + 13, y + kCentreY, true);
  out->setPixel(x + kCentreX, y + 14, true);
  out->setPixel(x + kCentreX, y + 13, true);
  out->setPixel(x + 1, y + kCentreY, true);
  out->setPixel(x + 2, y + kCentreY, true);
  // Hands: the long hand up, the short hand to the right.
  out->drawVLine(x + kCentreX, y + 4, 4, true);
  out->drawHLine(x + 5, y + kCentreY + 2, 3, true);
  out->drawVLine(x + kCentreX, y + 5, 3, true);
  out->drawHLine(x + kCentreX, y + kCentreY + 2, 3, true);
  out->setPixel(x + kCentreX, y + kCentreY, true);
  out->setPixel(x + kCentreX - 1, y + kCentreY - 1, true);
}

void drawPetScreen(const PetScene& scene, Framebuffer* out) {
  if (out == nullptr) {
    return;
  }
  out->clear();

  // Top line: the clock on the left, the mood on the right.
  out->drawText(0, kClockLineY, scene.clock);
  drawRightAligned(*out, petMoodLabel(scene.mood), kClockLineY);

  if (scene.bubbleVisible) {
    out->drawFrame(kBubbleX, kBubbleY, kBubbleWidth, kBubbleHeight, true);
    drawPetClock(out, kBubbleClockX, kBubbleClockY);
    out->drawText(kBubbleTextX, kBubbleTextY, scene.bubbleText, 2);
  } else if (scene.activity != PetActivity::kSleep) {
    drawCentered(*out, scene.speech, kSpeechY, 1);
  }

  drawGround(*out);

  const int spriteX = scene.spriteX;
  const int spriteY = kSpriteTop - scene.bob - scene.hop;
  drawPetSprite(out, scene.tailFrame, spriteX, spriteY, scene.eyesClosed,
                scene.gaze);
  if (scene.pawRaised) {
    // A raised front paw beside the muzzle (white on the black background, one
    // pixel clear of the head), and a white tongue inside the dark mouth hole.
    out->fillRect(spriteX + 21, spriteY + 7, 3, 3, true);
    out->fillRect(spriteX + 20, spriteY + 9, 1, 2, true);
    if (scene.tongueOut) {
      out->fillRect(spriteX + 12, spriteY + 10, 2, 2, true);
    }
  }
  if (scene.yawning) {
    // A yawn opens the mouth: the dark muzzle hole is made bigger, which is
    // what reads as an open mouth on a white cat.
    out->fillRect(spriteX + 10, spriteY + 9, 4, 2, false);
  }
  if (scene.activity == PetActivity::kStretch) {
    drawStretchMarks(*out, spriteX, spriteY);
  }
  if (scene.activity == PetActivity::kSleep || scene.activity == PetActivity::kNap) {
    drawSleepMarks(*out, spriteX);
  }
  if (scene.heartVisible || scene.noteVisible || scene.starVisible) {
    drawCelebrationIcons(*out, scene);
  }

  drawCentered(*out, scene.sensors, kSensorsY, 1);
}

}  // namespace fw

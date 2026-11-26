// Draws the VirtualPet scene on the 128x64 SSD1306 panel.
#pragma once

#include "display/framebuffer.h"
#include "pet/pet_model.h"

namespace fw {

// Full pet screen: clock and mood on top, the cat walking above the ground
// line, the hour chime in a speech bubble and the readings underneath.
void drawPetScreen(const PetScene& scene, Framebuffer* out);

// One sprite frame at (x, y) with the eyes either open (the bitmap holes) or
// closed (a lit line over the holes). Exposed for the unit tests.
void drawPetSprite(Framebuffer* out, uint8_t tailFrame, int x, int y,
                   bool eyesClosed);

// A 16x16 clock face, used inside the chime bubble.
void drawPetClock(Framebuffer* out, int x, int y);

}  // namespace fw

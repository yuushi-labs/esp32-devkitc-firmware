// Draws a QrCode onto the OLED framebuffer.
#pragma once

#include "display/framebuffer.h"
#include "graphics/qr_code.h"

namespace fw {

// Fits the code into maxWidth x maxHeight with a four module quiet zone,
// centred. Returns the module scale that was used (0 when the code is empty).
int drawQrCode(Framebuffer& framebuffer, const QrCode& qr, int maxWidth,
               int maxHeight);

}  // namespace fw

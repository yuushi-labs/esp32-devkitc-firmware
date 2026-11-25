// Draws a QrCode onto the OLED framebuffer.
#pragma once

#include "display/framebuffer.h"
#include "graphics/qr_code.h"

namespace fw {

// Fits the code into maxWidth x maxHeight, centred, as dark modules on a lit
// background. Returns the module scale that was used, or 0 when the code is
// empty or cannot be drawn at one pixel per module; the caller then has to show
// something else instead.
int drawQrCode(Framebuffer& framebuffer, const QrCode& qr, int maxWidth,
               int maxHeight);

}  // namespace fw

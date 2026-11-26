#include "graphics/qr_render.h"

#include <algorithm>

namespace fw {
namespace {

// ISO/IEC 18004 asks for a four module quiet zone around the symbol, but a
// 128x64 panel cannot hold a large code with that border at two pixels per
// module. The renderer lights the whole panel, so the lit area around the
// symbol is the quiet zone; what it must keep is a small light border so the
// code does not touch the bezel.
constexpr int kMinimumMarginPixels = 2;
constexpr int kMaxScale = 8;

}  // namespace

int drawQrCode(Framebuffer& framebuffer, const QrCode& qr, int maxWidth,
               int maxHeight) {
  if (!qr.valid() || maxWidth <= 0 || maxHeight <= 0) {
    return 0;
  }

  const int shortSide = std::min(maxWidth, maxHeight);
  // Largest whole-pixel scale that still leaves the light border. The margin is
  // measured in pixels, not modules: at scale 2 a four module quiet zone would
  // cost 16 of the 64 rows and leave a large code at one pixel per module,
  // which no phone camera resolves.
  int scale = 0;
  for (int candidate = kMaxScale; candidate >= 1; --candidate) {
    const int extent = qr.size() * candidate;
    if (extent > maxWidth || extent > maxHeight ||
        extent + 2 * kMinimumMarginPixels > shortSide) {
      continue;
    }
    scale = candidate;
    break;
  }
  if (scale == 0) {
    return 0;
  }

  const int extent = qr.size() * scale;
  const int originX = (maxWidth - extent) / 2;
  const int originY = (maxHeight - extent) / 2;
  // A scanner expects dark modules on a light background: light the whole area
  // and punch the dark modules out of it.
  framebuffer.fillRect(0, 0, maxWidth, maxHeight, true);
  for (int y = 0; y < qr.size(); ++y) {
    for (int x = 0; x < qr.size(); ++x) {
      if (qr.module(x, y)) {
        framebuffer.fillRect(originX + x * scale, originY + y * scale, scale, scale,
                             false);
      }
    }
  }
  return scale;
}

}  // namespace fw

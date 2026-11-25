#include "graphics/qr_render.h"

#include <algorithm>

namespace fw {
namespace {

// ISO/IEC 18004 asks for a four module quiet zone around the symbol. A 128x64
// panel cannot hold a version 2 code with that border at two pixels per module,
// so the lit background of the panel provides it: the renderer keeps at least
// two modules free on every side instead of dropping to one pixel per module,
// which no phone camera can resolve.
constexpr int kQuietZoneModules = 4;
constexpr int kMinimumMarginModules = 2;

}  // namespace

int drawQrCode(Framebuffer& framebuffer, const QrCode& qr, int maxWidth,
               int maxHeight) {
  if (!qr.valid() || maxWidth <= 0 || maxHeight <= 0) {
    return 0;
  }

  const int shortSide = std::min(maxWidth, maxHeight);
  int scale = shortSide / (qr.size() + 2 * kQuietZoneModules);
  if (scale < 2) {
    scale = std::max(scale, shortSide / (qr.size() + 2 * kMinimumMarginModules));
  }
  if (scale < 1) {
    return 0;
  }

  const int extent = qr.size() * scale;
  if (extent > maxWidth || extent > maxHeight) {
    return 0;
  }

  // A scanner expects dark modules on a light background: light the whole area
  // and punch the dark modules out of it.
  framebuffer.fillRect(0, 0, maxWidth, maxHeight, true);

  const int originX = (maxWidth - extent) / 2;
  const int originY = (maxHeight - extent) / 2;
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

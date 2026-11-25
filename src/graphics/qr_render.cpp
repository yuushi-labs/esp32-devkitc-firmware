#include "graphics/qr_render.h"

namespace fw {
namespace {

constexpr int kQuietZoneModules = 4;

}  // namespace

int drawQrCode(Framebuffer& framebuffer, const QrCode& qr, int maxWidth,
               int maxHeight) {
  if (!qr.valid() || maxWidth <= 0 || maxHeight <= 0) {
    return 0;
  }
  const int modules = qr.size() + 2 * kQuietZoneModules;
  int scale = maxWidth / modules;
  const int vertical = maxHeight / modules;
  if (vertical < scale) {
    scale = vertical;
  }
  if (scale < 1) {
    scale = 1;
  }

  const int extent = modules * scale;
  const int originX = (maxWidth - extent) / 2 + kQuietZoneModules * scale;
  const int originY = (maxHeight - extent) / 2 + kQuietZoneModules * scale;

  for (int y = 0; y < qr.size(); ++y) {
    for (int x = 0; x < qr.size(); ++x) {
      if (qr.module(x, y)) {
        framebuffer.fillRect(originX + x * scale, originY + y * scale, scale, scale,
                             true);
      }
    }
  }
  return scale;
}

}  // namespace fw

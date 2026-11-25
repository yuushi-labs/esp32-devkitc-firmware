// Minimal QR Code encoder for the OLED dashboard link.
//
// Implements ISO/IEC 18004 byte mode for versions 1..10 with ECC level L or M,
// including Reed-Solomon error correction, all eight masks and the penalty
// score used to pick the best one. No Arduino dependencies, so the matrices are
// verified on the host against reference vectors.
#pragma once

#include <cstddef>
#include <cstdint>

namespace fw {

enum class QrEcc { kLow = 0, kMedium = 1 };

class QrCode {
 public:
  static constexpr int kMaxVersion = 10;
  // 17 + 4 * version modules per side at the largest supported version.
  static constexpr int kMaxSize = 17 + 4 * kMaxVersion;

  QrCode();

  // Encodes `text` in byte mode, choosing the smallest version that fits.
  // Returns false when the text is too long for version 10 at this ECC level.
  bool encode(const char* text, QrEcc ecc = QrEcc::kLow);
  // Same with an explicit mask 0..7 (or -1 for the automatic choice).
  bool encodeWithMask(const char* text, QrEcc ecc, int mask);

  int size() const { return size_; }
  int version() const { return version_; }
  int mask() const { return mask_; }
  QrEcc ecc() const { return ecc_; }
  bool valid() const { return size_ > 0; }

  // True when the module at (x, y) is dark. Out of range reads are light.
  bool module(int x, int y) const;

  void clear();

 private:
  bool encodeAtVersion(const uint8_t* text, size_t length, QrEcc ecc, int version,
                       int mask);

  void setModule(int x, int y, bool dark);
  bool functionModule(int x, int y) const;
  void setFunction(int x, int y, bool dark);
  void drawFunctionPatterns();
  void drawFinderPattern(int centerX, int centerY);
  void drawAlignmentPattern(int centerX, int centerY);
  void drawFormatBits(int mask);
  void drawVersion();
  void drawCodewords(const uint8_t* data, size_t length);
  void applyMask(int mask);
  long penaltyScore() const;

  uint8_t modules_[kMaxSize * kMaxSize] = {0};
  uint8_t functions_[kMaxSize * kMaxSize] = {0};
  int size_ = 0;
  int version_ = 0;
  int mask_ = -1;
  QrEcc ecc_ = QrEcc::kLow;
};

}  // namespace fw

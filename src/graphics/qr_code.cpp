#include "graphics/qr_code.h"

#include <cstdlib>
#include <cstring>
#include <vector>

namespace fw {
namespace {

// ECC codewords per block and block counts, indexed [ecc][version]. Values for
// versions 1..10 are from ISO/IEC 18004 table 9.
constexpr int kEccCodewordsPerBlock[2][QrCode::kMaxVersion + 1] = {
    {-1, 7, 10, 15, 20, 26, 18, 20, 24, 30, 18},
    {-1, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26},
};
constexpr int kEccBlockCount[2][QrCode::kMaxVersion + 1] = {
    {-1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 4},
    {-1, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5},
};

// Format information uses a different two-bit value per ECC level.
constexpr int kFormatEccBits[2] = {1 /* L */, 0 /* M */};

int numRawDataModules(int version) {
  int result = (16 * version + 128) * version + 64;
  if (version >= 2) {
    const int align = version / 7 + 2;
    result -= (25 * align - 10) * align - 55;
    if (version >= 7) {
      result -= 36;
    }
  }
  return result;
}

int numDataCodewords(int version, QrEcc ecc) {
  const int index = static_cast<int>(ecc);
  return numRawDataModules(version) / 8 -
         kEccCodewordsPerBlock[index][version] * kEccBlockCount[index][version];
}

int charCountBits(int version) { return version <= 9 ? 8 : 16; }

uint8_t gfMultiply(uint8_t x, uint8_t y) {
  int z = 0;
  for (int i = 7; i >= 0; --i) {
    z = (z << 1) ^ ((z >> 7) * 0x11D);
    z ^= ((y >> i) & 1) * x;
  }
  return static_cast<uint8_t>(z);
}

std::vector<uint8_t> reedSolomonDivisor(int degree) {
  std::vector<uint8_t> divisor(static_cast<size_t>(degree), 0);
  divisor[static_cast<size_t>(degree - 1)] = 1;
  uint8_t root = 1;
  for (int i = 0; i < degree; ++i) {
    for (int j = 0; j < degree; ++j) {
      divisor[static_cast<size_t>(j)] = gfMultiply(divisor[static_cast<size_t>(j)], root);
      if (j + 1 < degree) {
        divisor[static_cast<size_t>(j)] ^= divisor[static_cast<size_t>(j + 1)];
      }
    }
    root = gfMultiply(root, 0x02);
  }
  return divisor;
}

std::vector<uint8_t> reedSolomonRemainder(const std::vector<uint8_t>& data,
                                          int degree) {
  const std::vector<uint8_t> divisor = reedSolomonDivisor(degree);
  std::vector<uint8_t> result(static_cast<size_t>(degree), 0);
  for (uint8_t byte : data) {
    const uint8_t factor = byte ^ result[0];
    result.erase(result.begin());
    result.push_back(0);
    for (size_t i = 0; i < result.size(); ++i) {
      result[i] ^= gfMultiply(divisor[i], factor);
    }
  }
  return result;
}

void appendBits(std::vector<uint8_t>& bits, uint32_t value, int length) {
  for (int i = length - 1; i >= 0; --i) {
    bits.push_back(static_cast<uint8_t>((value >> i) & 1));
  }
}

int alignmentPositions(int version, int* out) {
  if (version == 1) {
    return 0;
  }
  const int count = version / 7 + 2;
  const int size = version * 4 + 17;
  const int step =
      (version * 4 + count * 2 + 1) / (count * 2 - 2) * 2;
  out[0] = 6;
  int position = size - 7;
  for (int i = count - 1; i >= 1; --i) {
    out[i] = position;
    position -= step;
  }
  return count;
}

}  // namespace

QrCode::QrCode() { clear(); }

void QrCode::clear() {
  size_ = 0;
  version_ = 0;
  mask_ = -1;
  ecc_ = QrEcc::kLow;
  std::memset(modules_, 0, sizeof(modules_));
  std::memset(functions_, 0, sizeof(functions_));
}

bool QrCode::module(int x, int y) const {
  if (x < 0 || y < 0 || x >= size_ || y >= size_) {
    return false;
  }
  return modules_[static_cast<size_t>(y) * static_cast<size_t>(size_) +
                  static_cast<size_t>(x)] != 0;
}

void QrCode::setModule(int x, int y, bool dark) {
  if (x < 0 || y < 0 || x >= size_ || y >= size_) {
    return;
  }
  modules_[static_cast<size_t>(y) * static_cast<size_t>(size_) +
           static_cast<size_t>(x)] = dark ? 1 : 0;
}

bool QrCode::functionModule(int x, int y) const {
  return functions_[static_cast<size_t>(y) * static_cast<size_t>(size_) +
                    static_cast<size_t>(x)] != 0;
}

void QrCode::setFunction(int x, int y, bool dark) {
  if (x < 0 || y < 0 || x >= size_ || y >= size_) {
    return;
  }
  const size_t index = static_cast<size_t>(y) * static_cast<size_t>(size_) +
                       static_cast<size_t>(x);
  modules_[index] = dark ? 1 : 0;
  functions_[index] = 1;
}

void QrCode::drawFinderPattern(int centerX, int centerY) {
  for (int dy = -4; dy <= 4; ++dy) {
    for (int dx = -4; dx <= 4; ++dx) {
      const int distance =
          std::abs(dx) > std::abs(dy) ? std::abs(dx) : std::abs(dy);
      setFunction(centerX + dx, centerY + dy, distance != 2 && distance != 4);
    }
  }
}

void QrCode::drawAlignmentPattern(int centerX, int centerY) {
  for (int dy = -2; dy <= 2; ++dy) {
    for (int dx = -2; dx <= 2; ++dx) {
      const int distance =
          std::abs(dx) > std::abs(dy) ? std::abs(dx) : std::abs(dy);
      setFunction(centerX + dx, centerY + dy, distance != 1);
    }
  }
}

void QrCode::drawFunctionPatterns() {
  for (int i = 0; i < size_; ++i) {
    setFunction(6, i, i % 2 == 0);
    setFunction(i, 6, i % 2 == 0);
  }

  drawFinderPattern(3, 3);
  drawFinderPattern(size_ - 4, 3);
  drawFinderPattern(3, size_ - 4);

  int positions[7] = {0};
  const int count = alignmentPositions(version_, positions);
  for (int i = 0; i < count; ++i) {
    for (int j = 0; j < count; ++j) {
      const bool nearFinder = (i == 0 && j == 0) || (i == 0 && j == count - 1) ||
                              (i == count - 1 && j == 0);
      if (!nearFinder) {
        drawAlignmentPattern(positions[i], positions[j]);
      }
    }
  }

  // Reserve the format and version areas; the real bits are drawn once the
  // mask has been chosen.
  drawFormatBits(0);
  drawVersion();
}

void QrCode::drawFormatBits(int mask) {
  const int data = (kFormatEccBits[static_cast<int>(ecc_)] << 3) | mask;
  int remainder = data;
  for (int i = 0; i < 10; ++i) {
    remainder = (remainder << 1) ^ ((remainder >> 9) * 0x537);
  }
  const int bits = ((data << 10) | remainder) ^ 0x5412;

  for (int i = 0; i <= 5; ++i) {
    setFunction(8, i, ((bits >> i) & 1) != 0);
  }
  setFunction(8, 7, ((bits >> 6) & 1) != 0);
  setFunction(8, 8, ((bits >> 7) & 1) != 0);
  setFunction(7, 8, ((bits >> 8) & 1) != 0);
  for (int i = 9; i < 15; ++i) {
    setFunction(14 - i, 8, ((bits >> i) & 1) != 0);
  }

  for (int i = 0; i < 8; ++i) {
    setFunction(size_ - 1 - i, 8, ((bits >> i) & 1) != 0);
  }
  for (int i = 8; i < 15; ++i) {
    setFunction(8, size_ - 15 + i, ((bits >> i) & 1) != 0);
  }
  setFunction(8, size_ - 8, true);
}

void QrCode::drawVersion() {
  if (version_ < 7) {
    return;
  }
  int remainder = version_;
  for (int i = 0; i < 12; ++i) {
    remainder = (remainder << 1) ^ ((remainder >> 11) * 0x1F25);
  }
  const int bits = (version_ << 12) | remainder;
  for (int i = 0; i < 18; ++i) {
    const bool dark = ((bits >> i) & 1) != 0;
    const int a = size_ - 11 + i % 3;
    const int b = i / 3;
    setFunction(a, b, dark);
    setFunction(b, a, dark);
  }
}

void QrCode::drawCodewords(const uint8_t* data, size_t length) {
  size_t bitIndex = 0;
  for (int right = size_ - 1; right >= 1; right -= 2) {
    if (right == 6) {
      right = 5;
    }
    for (int vertical = 0; vertical < size_; ++vertical) {
      for (int j = 0; j < 2; ++j) {
        const int x = right - j;
        const bool upward = ((right + 1) & 2) == 0;
        const int y = upward ? size_ - 1 - vertical : vertical;
        if (!functionModule(x, y) && bitIndex < length * 8) {
          const bool dark = ((data[bitIndex >> 3] >> (7 - (bitIndex & 7))) & 1) != 0;
          setModule(x, y, dark);
          ++bitIndex;
        }
      }
    }
  }
}

void QrCode::applyMask(int mask) {
  for (int y = 0; y < size_; ++y) {
    for (int x = 0; x < size_; ++x) {
      if (functionModule(x, y)) {
        continue;
      }
      bool invert = false;
      switch (mask) {
        case 0:
          invert = (x + y) % 2 == 0;
          break;
        case 1:
          invert = y % 2 == 0;
          break;
        case 2:
          invert = x % 3 == 0;
          break;
        case 3:
          invert = (x + y) % 3 == 0;
          break;
        case 4:
          invert = (y / 2 + x / 3) % 2 == 0;
          break;
        case 5:
          invert = (x * y) % 2 + (x * y) % 3 == 0;
          break;
        case 6:
          invert = ((x * y) % 2 + (x * y) % 3) % 2 == 0;
          break;
        case 7:
        default:
          invert = ((x + y) % 2 + (x * y) % 3) % 2 == 0;
          break;
      }
      if (invert) {
        setModule(x, y, !module(x, y));
      }
    }
  }
}

long QrCode::penaltyScore() const {
  long result = 0;

  for (int y = 0; y < size_; ++y) {
    bool color = module(0, y);
    int run = 1;
    for (int x = 1; x < size_; ++x) {
      if (module(x, y) == color) {
        ++run;
        if (run == 5) {
          result += 3;
        } else if (run > 5) {
          ++result;
        }
      } else {
        color = module(x, y);
        run = 1;
      }
    }
  }
  for (int x = 0; x < size_; ++x) {
    bool color = module(x, 0);
    int run = 1;
    for (int y = 1; y < size_; ++y) {
      if (module(x, y) == color) {
        ++run;
        if (run == 5) {
          result += 3;
        } else if (run > 5) {
          ++result;
        }
      } else {
        color = module(x, y);
        run = 1;
      }
    }
  }

  for (int y = 0; y < size_ - 1; ++y) {
    for (int x = 0; x < size_ - 1; ++x) {
      const bool color = module(x, y);
      if (color == module(x + 1, y) && color == module(x, y + 1) &&
          color == module(x + 1, y + 1)) {
        result += 3;
      }
    }
  }

  // Finder-like 1:1:3:1:1 patterns with four light modules on one side.
  const bool patternA[11] = {true,  false, true, true,  true, false, true,
                             false, false, false, false};
  const bool patternB[11] = {false, false, false, false, true, false, true,
                             true,  true,  false, true};
  for (int y = 0; y < size_; ++y) {
    for (int x = 0; x + 11 <= size_; ++x) {
      bool matchA = true;
      bool matchB = true;
      for (int i = 0; i < 11; ++i) {
        const bool value = module(x + i, y);
        matchA = matchA && value == patternA[i];
        matchB = matchB && value == patternB[i];
      }
      if (matchA) {
        result += 40;
      }
      if (matchB) {
        result += 40;
      }
    }
  }
  for (int x = 0; x < size_; ++x) {
    for (int y = 0; y + 11 <= size_; ++y) {
      bool matchA = true;
      bool matchB = true;
      for (int i = 0; i < 11; ++i) {
        const bool value = module(x, y + i);
        matchA = matchA && value == patternA[i];
        matchB = matchB && value == patternB[i];
      }
      if (matchA) {
        result += 40;
      }
      if (matchB) {
        result += 40;
      }
    }
  }

  long dark = 0;
  for (int y = 0; y < size_; ++y) {
    for (int x = 0; x < size_; ++x) {
      if (module(x, y)) {
        ++dark;
      }
    }
  }
  const long total = static_cast<long>(size_) * size_;
  long balance = (dark * 20 - total * 10) / total;
  if (balance < 0) {
    balance = -balance;
  }
  result += balance * 10;

  return result;
}

bool QrCode::encode(const char* text, QrEcc ecc) {
  return encodeWithMask(text, ecc, -1);
}

bool QrCode::encodeWithMask(const char* text, QrEcc ecc, int mask) {
  clear();
  if (text == nullptr) {
    return false;
  }
  const size_t length = std::strlen(text);
  const int eccIndex = static_cast<int>(ecc);

  int version = 0;
  for (int candidate = 1; candidate <= kMaxVersion; ++candidate) {
    const size_t capacityBits =
        static_cast<size_t>(numDataCodewords(candidate, ecc)) * 8;
    const size_t needed =
        4 + static_cast<size_t>(charCountBits(candidate)) + length * 8;
    if (needed <= capacityBits) {
      version = candidate;
      break;
    }
  }
  if (version == 0) {
    return false;
  }
  if (mask < -1 || mask > 7) {
    return false;
  }
  (void)eccIndex;
  return encodeAtVersion(reinterpret_cast<const uint8_t*>(text), length, ecc,
                         version, mask);
}

bool QrCode::encodeAtVersion(const uint8_t* text, size_t length, QrEcc ecc,
                             int version, int mask) {
  version_ = version;
  ecc_ = ecc;
  size_ = version * 4 + 17;
  std::memset(modules_, 0, sizeof(modules_));
  std::memset(functions_, 0, sizeof(functions_));

  const int eccIndex = static_cast<int>(ecc);
  const int dataCodewords = numDataCodewords(version, ecc);

  std::vector<uint8_t> bits;
  bits.reserve(static_cast<size_t>(dataCodewords) * 8);
  appendBits(bits, 0x4, 4);  // byte mode
  appendBits(bits, static_cast<uint32_t>(length), charCountBits(version));
  for (size_t i = 0; i < length; ++i) {
    appendBits(bits, text[i], 8);
  }
  const size_t capacityBits = static_cast<size_t>(dataCodewords) * 8;
  if (bits.size() > capacityBits) {
    clear();
    return false;
  }
  // Terminator (up to four zero bits), then pad to a whole codeword.
  for (int i = 0; i < 4 && bits.size() < capacityBits; ++i) {
    bits.push_back(0);
  }
  while (bits.size() % 8 != 0) {
    bits.push_back(0);
  }
  std::vector<uint8_t> data(static_cast<size_t>(dataCodewords), 0);
  for (size_t i = 0; i < bits.size(); ++i) {
    data[i >> 3] |= static_cast<uint8_t>(bits[i] << (7 - (i & 7)));
  }
  for (size_t i = bits.size() / 8; i < data.size(); ++i) {
    data[i] = ((i - bits.size() / 8) % 2 == 0) ? 0xEC : 0x11;
  }

  const int blockCount = kEccBlockCount[eccIndex][version];
  const int eccLength = kEccCodewordsPerBlock[eccIndex][version];
  const int rawCodewords = numRawDataModules(version) / 8;
  const int numShortBlocks = blockCount - rawCodewords % blockCount;
  const int shortBlockLength = rawCodewords / blockCount;

  std::vector<std::vector<uint8_t>> blocks;
  blocks.reserve(static_cast<size_t>(blockCount));
  size_t dataOffset = 0;
  for (int i = 0; i < blockCount; ++i) {
    const int blockDataLength =
        shortBlockLength - eccLength + (i < numShortBlocks ? 0 : 1);
    std::vector<uint8_t> block(data.begin() + static_cast<long>(dataOffset),
                               data.begin() + static_cast<long>(dataOffset + blockDataLength));
    dataOffset += static_cast<size_t>(blockDataLength);
    const std::vector<uint8_t> remainder =
        reedSolomonRemainder(block, eccLength);
    block.insert(block.end(), remainder.begin(), remainder.end());
    blocks.push_back(std::move(block));
  }

  std::vector<uint8_t> interleaved;
  interleaved.reserve(static_cast<size_t>(rawCodewords));
  const int maxDataLength = shortBlockLength - eccLength + 1;
  for (int i = 0; i < maxDataLength; ++i) {
    for (int block = 0; block < blockCount; ++block) {
      const int blockDataLength =
          shortBlockLength - eccLength + (block < numShortBlocks ? 0 : 1);
      if (i < blockDataLength) {
        interleaved.push_back(blocks[static_cast<size_t>(block)][static_cast<size_t>(i)]);
      }
    }
  }
  for (int i = 0; i < eccLength; ++i) {
    for (int block = 0; block < blockCount; ++block) {
      const int blockDataLength =
          shortBlockLength - eccLength + (block < numShortBlocks ? 0 : 1);
      interleaved.push_back(
          blocks[static_cast<size_t>(block)][static_cast<size_t>(blockDataLength + i)]);
    }
  }

  drawFunctionPatterns();
  drawCodewords(interleaved.data(), interleaved.size());

  int chosenMask = mask;
  if (chosenMask < 0) {
    long bestScore = -1;
    for (int candidate = 0; candidate < 8; ++candidate) {
      applyMask(candidate);
      drawFormatBits(candidate);
      const long score = penaltyScore();
      if (bestScore < 0 || score < bestScore) {
        bestScore = score;
        chosenMask = candidate;
      }
      applyMask(candidate);  // undo
    }
  }
  if (chosenMask < 0) {
    chosenMask = 0;
  }
  applyMask(chosenMask);
  drawFormatBits(chosenMask);
  mask_ = chosenMask;
  return true;
}

}  // namespace fw

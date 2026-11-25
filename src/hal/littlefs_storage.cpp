#include "hal/littlefs_storage.h"

#include <cstring>

namespace fw {

bool LittleFsLogStorage::begin() {
  // Format on first use: a brand new board has no LittleFS yet.
  if (!LittleFS.begin(true)) {
    return false;
  }
  return true;
}

bool LittleFsLogStorage::appendLine(const char* line) {
  if (line == nullptr) {
    return false;
  }
  File file = LittleFS.open(path_, FILE_APPEND);
  if (!file) {
    return false;
  }
  const size_t written = file.print(line);
  file.close();
  return written == std::strlen(line);
}

uint32_t LittleFsLogStorage::countDataLines() {
  File file = LittleFS.open(path_, FILE_READ);
  if (!file) {
    return 0;
  }

  uint32_t newlines = 0;
  char chunk[128];
  while (file.available() > 0) {
    const size_t read = file.readBytes(chunk, sizeof(chunk));
    if (read == 0) {
      break;
    }
    for (size_t index = 0; index < read; ++index) {
      if (chunk[index] == '\n') {
        ++newlines;
      }
    }
  }
  file.close();

  // The first line is the CSV header.
  return newlines > 0 ? newlines - 1 : 0;
}

uint32_t LittleFsLogStorage::fileSize() {
  File file = LittleFS.open(path_, FILE_READ);
  if (!file) {
    return 0;
  }
  const size_t size = file.size();
  file.close();
  return static_cast<uint32_t>(size);
}

bool LittleFsLogStorage::removeFile() {
  if (!LittleFS.exists(path_)) {
    // Nothing to delete still counts as "the log is gone".
    return true;
  }
  return LittleFS.remove(path_);
}

size_t LittleFsLogStorage::readHeaderLine(char* out, size_t outSize) {
  if (out == nullptr || outSize == 0) {
    return 0;
  }
  out[0] = '\0';
  File file = LittleFS.open(path_, FILE_READ);
  if (!file) {
    return 0;
  }
  const size_t read = file.readBytesUntil('\n', out, outSize - 1);
  file.close();
  out[read] = '\0';
  return read;
}

bool LittleFsLogStorage::renameFile(const char* newPath) {
  if (newPath == nullptr || !LittleFS.exists(path_)) {
    return false;
  }
  // Replace an older copy so a second upgrade cannot fail on the name.
  if (LittleFS.exists(newPath)) {
    LittleFS.remove(newPath);
  }
  return LittleFS.rename(path_, newPath);
}

}  // namespace fw

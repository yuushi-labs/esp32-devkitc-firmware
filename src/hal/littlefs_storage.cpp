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
  // Keep an older backup instead of deleting it: after a downgrade to v0.2.0 and
  // a second upgrade the previous /log-v020.csv must survive, so a taken name
  // becomes /log-v020-2.csv, -3 and so on.
  char candidate[64];
  std::snprintf(candidate, sizeof(candidate), "%s", newPath);
  const size_t length = std::strlen(newPath);
  const bool csvSuffix =
      length > 4 && std::strcmp(newPath + length - 4, ".csv") == 0;
  for (int attempt = 2; attempt <= 9 && LittleFS.exists(candidate); ++attempt) {
    if (csvSuffix) {
      std::snprintf(candidate, sizeof(candidate), "%.*s-%d.csv",
                    static_cast<int>(length - 4), newPath, attempt);
    } else {
      std::snprintf(candidate, sizeof(candidate), "%s-%d", newPath, attempt);
    }
  }
  if (LittleFS.exists(candidate)) {
    // Out of names: leaving the old file in place beats overwriting data.
    return false;
  }
  return LittleFS.rename(path_, candidate);
}

}  // namespace fw

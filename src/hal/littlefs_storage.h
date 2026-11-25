// LittleFS backed LogStorage.
//
// The station firmware partitions flash with `no_ota.csv`, so LittleFS lives on
// the 1.9 MB `spiffs` partition at 0x210000 and the Android flasher can pull the
// same image back out.
#pragma once

#include <Arduino.h>
#include <LittleFS.h>

#include "logging/log_record.h"

namespace fw {

class LittleFsLogStorage : public LogStorage {
 public:
  explicit LittleFsLogStorage(const char* path) : path_(path) {}

  bool begin() override;
  bool appendLine(const char* line) override;
  uint32_t countDataLines() override;
  uint32_t fileSize() override;
  bool removeFile() override;
  size_t readHeaderLine(char* out, size_t outSize) override;
  bool renameFile(const char* newPath) override;

 private:
  const char* path_;
};

}  // namespace fw

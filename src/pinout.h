// GPIO wiring information compiled in from pins.json at build time.
//
// pins.json is the single source of truth: scripts/pio_pinout_header.py turns
// it into pinout_generated.h, which this module exposes to the serial command
// handler. The Android flasher reads the same data from flash-manifest.json.
#pragma once

#include <cstddef>

namespace fw {

// One GPIO connection as described by pins.json.
struct PinWiring {
  int gpio;
  const char* label;
  const char* direction;
  const char* activeLevel;  // "" when pins.json omitted it
  const char* note;         // "" when pins.json omitted it
};

// The wiring table plus board level notes.
struct PinoutTable {
  const char* board;  // "" when unknown
  int schemaVersion;
  const PinWiring* pins;
  size_t pinCount;
  const char* powerNote;  // "" when unset
  const char* const* notes;
  size_t noteCount;
};

// Table generated from pins.json at build time. When the file is missing the
// table is empty and the PINOUT command says so instead of failing to build.
PinoutTable embeddedPinout();

// The format functions below always NUL terminate and return the number of
// characters the full text needs (snprintf semantics), so callers can detect
// truncation.

// "GPIO2 output active=HIGH <label>\n" plus an indented note line when present.
size_t formatPinLine(const PinWiring& pin, char* out, size_t outSize);

// The multi-line wiring list printed by the PINOUT serial command.
size_t formatPinout(const PinoutTable& table, char* out, size_t outSize);

}  // namespace fw

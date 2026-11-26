// The OLED remote: the state and commands behind GET/POST /api/oled.
//
// Pure C++ so the parsing of the query fields and the JSON the dashboard polls
// are unit tested on the host; the station app applies the command to the panel.
#pragma once

#include <cstddef>
#include <cstdint>

#include "pet/pet_model.h"
#include "station/station_model.h"

namespace fw {

// Snapshot the dashboard reads back from GET /api/oled.
struct OledState {
  bool power = true;
  bool panelPresent = false;
  uint8_t address = 0;
  const char* screen = "mode";
  const char* mode = "pet";
  const char* menu = "pet";
  const char* view = "summary";
  const PetScene* pet = nullptr;
};

enum class OledPowerAction { kNoChange, kOn, kOff, kToggle };

// One POST /api/oled request. Every field is optional and the parts combine, so
// "mode=wifi" and "step=1" in one request move to the next mode and stop there.
struct OledCommand {
  OledPowerAction power = OledPowerAction::kNoChange;
  bool hasMode = false;
  StationMode mode = StationMode::kPet;
  // "menu" / "home": open the home menu instead of a mode.
  bool hasMenu = false;
  // Environment sub view: summary, graph or detail.
  bool hasView = false;
  EnvView view = EnvView::kSummary;
  // +1 = next mode (or next menu entry), -1 = previous.
  int8_t step = 0;
  // PSH / CON in the menu: open the selected entry.
  bool select = false;
  // BAK: leave the current screen for the home menu.
  bool back = false;
  // Replay the hourly clock chime on the pet screen.
  bool testChime = false;

  bool any() const {
    return power != OledPowerAction::kNoChange || hasMode || hasMenu ||
           hasView || step != 0 || select || back || testChime;
  }
};

// Parses the optional `power`, `mode` and `action` fields. Returns false and
// sets `error` (a static string) for a value it does not know.
bool parseOledCommand(const char* power, const char* mode, const char* action,
                      const char* view, OledCommand* out, const char** error);

// GET /api/oled
size_t formatOledStateJson(const OledState& state, char* out, size_t outSize);

}  // namespace fw

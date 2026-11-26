#include "station/oled_remote.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace fw {
namespace {

bool equalsIgnoreCase(const char* lhs, const char* rhs) {
  if (lhs == nullptr || rhs == nullptr) {
    return false;
  }
  while (*lhs != '\0' && *rhs != '\0') {
    const char a = (*lhs >= 'A' && *lhs <= 'Z') ? static_cast<char>(*lhs - 'A' + 'a') : *lhs;
    const char b = (*rhs >= 'A' && *rhs <= 'Z') ? static_cast<char>(*rhs - 'A' + 'a') : *rhs;
    if (a != b) {
      return false;
    }
    ++lhs;
    ++rhs;
  }
  return *lhs == '\0' && *rhs == '\0';
}

struct Appender {
  Appender(char* outBuffer, size_t bufferSize) : out(outBuffer), size(bufferSize) {}

  char* out;
  size_t size;
  size_t used = 0;
  size_t needed = 0;

  void appendf(const char* format, ...) {
    char scratch = '\0';
    char* destination = (out != nullptr && used < size) ? out + used : &scratch;
    const size_t remaining = (out != nullptr && used < size) ? size - used : 0;
    va_list args;
    va_start(args, format);
    const int written = std::vsnprintf(destination, remaining, format, args);
    va_end(args);
    if (written <= 0) {
      return;
    }
    needed += static_cast<size_t>(written);
    if (remaining > 0) {
      const size_t available = remaining - 1;
      used += static_cast<size_t>(written) < available
                  ? static_cast<size_t>(written)
                  : available;
    }
  }

  void appendJsonString(const char* value) {
    appendf("\"");
    if (value != nullptr) {
      for (const char* p = value; *p != '\0'; ++p) {
        const unsigned char c = static_cast<unsigned char>(*p);
        if (c == '"' || c == '\\') {
          appendf("\\%c", static_cast<char>(c));
        } else if (c < 0x20) {
          appendf("\\u%04x", static_cast<unsigned>(c));
        } else {
          appendf("%c", static_cast<char>(c));
        }
      }
    }
    appendf("\"");
  }
};

bool fieldIsEmpty(const char* value) { return value == nullptr || value[0] == '\0'; }

}  // namespace

bool parseOledCommand(const char* power, const char* mode, const char* action,
                      const char* view, OledCommand* out, const char** error) {
  if (error != nullptr) {
    *error = nullptr;
  }
  if (out == nullptr) {
    return false;
  }
  *out = OledCommand();

  if (!fieldIsEmpty(power)) {
    if (equalsIgnoreCase(power, "on") || equalsIgnoreCase(power, "1") ||
        equalsIgnoreCase(power, "true")) {
      out->power = OledPowerAction::kOn;
    } else if (equalsIgnoreCase(power, "off") || equalsIgnoreCase(power, "0") ||
               equalsIgnoreCase(power, "false")) {
      out->power = OledPowerAction::kOff;
    } else if (equalsIgnoreCase(power, "toggle")) {
      out->power = OledPowerAction::kToggle;
    } else {
      if (error != nullptr) {
        *error = "power must be on, off or toggle";
      }
      return false;
    }
  }

  if (!fieldIsEmpty(mode)) {
    if (equalsIgnoreCase(mode, "menu") || equalsIgnoreCase(mode, "home")) {
      out->hasMenu = true;
    } else if (stationModeFromName(mode, &out->mode)) {
      out->hasMode = true;
    } else {
      if (error != nullptr) {
        *error = "mode must be pet, env, logs, wifi, ble, timer, system or menu";
      }
      return false;
    }
  }

  if (!fieldIsEmpty(view)) {
    if (equalsIgnoreCase(view, "summary") || equalsIgnoreCase(view, "monitor")) {
      out->view = EnvView::kSummary;
      out->hasView = true;
    } else if (equalsIgnoreCase(view, "graph") || equalsIgnoreCase(view, "chart")) {
      out->view = EnvView::kGraph;
      out->hasView = true;
    } else if (equalsIgnoreCase(view, "detail")) {
      out->view = EnvView::kDetail;
      out->hasView = true;
    } else {
      if (error != nullptr) {
        *error = "view must be summary, graph or detail";
      }
      return false;
    }
  }

  if (!fieldIsEmpty(action)) {
    if (equalsIgnoreCase(action, "next")) {
      out->step = 1;
    } else if (equalsIgnoreCase(action, "prev") || equalsIgnoreCase(action, "previous")) {
      out->step = -1;
    } else if (equalsIgnoreCase(action, "select") || equalsIgnoreCase(action, "enter") ||
               equalsIgnoreCase(action, "ok")) {
      out->select = true;
    } else if (equalsIgnoreCase(action, "back")) {
      out->back = true;
    } else if (equalsIgnoreCase(action, "clock") || equalsIgnoreCase(action, "chime")) {
      out->testChime = true;
    } else {
      if (error != nullptr) {
        *error = "action must be next, prev, select, back or clock";
      }
      return false;
    }
  }

  return true;
}

size_t formatOledStateJson(const OledState& state, char* out, size_t outSize) {
  Appender text{out, outSize};
  text.appendf("{\"power\":%s,\"present\":%s,\"address\":",
               state.power ? "true" : "false",
               state.panelPresent ? "true" : "false");
  if (state.address != 0) {
    text.appendf("\"0x%02X\"", static_cast<unsigned>(state.address));
  } else {
    text.appendf("null");
  }
  text.appendf(",\"screen\":");
  text.appendJsonString(state.screen);
  text.appendf(",\"mode\":");
  text.appendJsonString(state.mode);
  text.appendf(",\"menu\":");
  text.appendJsonString(state.menu);
  text.appendf(",\"view\":");
  text.appendJsonString(state.view);
  text.appendf(",\"pet\":");
  if (state.pet == nullptr) {
    text.appendf("null");
  } else {
    const PetScene& pet = *state.pet;
    text.appendf("{\"activity\":");
    text.appendJsonString(petActivityName(pet.activity));
    text.appendf(",\"mood\":");
    text.appendJsonString(petMoodName(pet.mood));
    text.appendf(",\"clock\":");
    text.appendJsonString(pet.clock);
    text.appendf(",\"speech\":");
    text.appendJsonString(pet.speech);
    text.appendf(",\"bubble\":");
    text.appendJsonString(pet.bubbleVisible ? pet.bubbleText : "");
    text.appendf(",\"x\":%d}", static_cast<int>(pet.spriteX));
  }
  text.appendf("}");
  return text.needed;
}

}  // namespace fw

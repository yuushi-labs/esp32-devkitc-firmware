#include "pinout.h"

#include <cstdarg>
#include <cstdio>

#include "pinout_generated.h"

namespace fw {
namespace {

bool hasText(const char* text) {
  return text != nullptr && text[0] != '\0';
}

// Appends snprintf output while tracking the untruncated length, so callers can
// tell whether the destination was large enough.
class TextWriter {
 public:
  TextWriter(char* out, size_t outSize) : out_(out), size_(outSize) {}

  void append(const char* format, ...) {
    char scratch = '\0';
    const size_t remaining = size_ > used_ ? size_ - used_ : 0;
    va_list args;
    va_start(args, format);
    const int written = std::vsnprintf(
        remaining > 0 ? out_ + used_ : &scratch, remaining, format, args);
    va_end(args);
    if (written <= 0) {
      return;
    }
    needed_ += static_cast<size_t>(written);
    if (remaining > 0) {
      const size_t available = remaining - 1;  // vsnprintf keeps the NUL
      used_ += static_cast<size_t>(written) < available
                   ? static_cast<size_t>(written)
                   : available;
    }
  }

  size_t needed() const { return needed_; }

 private:
  char* out_;
  size_t size_;
  size_t used_ = 0;
  size_t needed_ = 0;
};

void writePin(TextWriter& writer, const PinWiring& pin) {
  writer.append("GPIO%d %s", pin.gpio, hasText(pin.direction) ? pin.direction : "-");
  if (hasText(pin.activeLevel)) {
    writer.append(" active=%s", pin.activeLevel);
  }
  writer.append(" %s\n", hasText(pin.label) ? pin.label : "");
  if (hasText(pin.note)) {
    writer.append("  note: %s\n", pin.note);
  }
}

}  // namespace

PinoutTable embeddedPinout() {
  PinoutTable table{};
  table.board = pins::kBoard;
  table.schemaVersion = pins::kSchemaVersion;
  table.pins = pins::kPins;
  table.pinCount = pins::kPinCount;
  table.powerNote = pins::kPowerNote;
  table.notes = pins::kNotes;
  table.noteCount = pins::kNoteCount;
  return table;
}

size_t formatPinLine(const PinWiring& pin, char* out, size_t outSize) {
  TextWriter writer(out, outSize);
  writePin(writer, pin);
  return writer.needed();
}

size_t formatPinout(const PinoutTable& table, char* out, size_t outSize) {
  TextWriter writer(out, outSize);

  if (table.pins == nullptr || table.pinCount == 0) {
    writer.append("pinout: this firmware was built without pins.json\n");
    return writer.needed();
  }

  writer.append("pinout board=%s pins=%u\n",
                hasText(table.board) ? table.board : "unknown",
                static_cast<unsigned>(table.pinCount));
  for (size_t index = 0; index < table.pinCount; ++index) {
    writePin(writer, table.pins[index]);
  }
  if (hasText(table.powerNote)) {
    writer.append("power: %s\n", table.powerNote);
  }
  for (size_t index = 0; table.notes != nullptr && index < table.noteCount;
       ++index) {
    if (hasText(table.notes[index])) {
      writer.append("note: %s\n", table.notes[index]);
    }
  }
  return writer.needed();
}

}  // namespace fw

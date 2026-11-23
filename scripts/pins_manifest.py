"""Load and validate ``pins.json`` and render the firmware pin table.

``pins.json`` is the single source of truth for "which GPIO is connected to
what" for the sample firmware. Two consumers read it:

* ``scripts/build_release_bins.py`` embeds the pin list and the wiring notes
  into ``flash-manifest.json`` so the Android flasher can show a wiring guide.
* ``scripts/pio_pinout_header.py`` renders ``pinout_generated.h`` at build time
  so the firmware's ``PINOUT`` serial command prints exactly the same list.

Only the standard library is used: this module runs on a bare CI runner and
inside a PlatformIO pre-script.
"""

from __future__ import annotations

import json
from collections.abc import Iterable, Mapping, Sequence
from pathlib import Path
from typing import Any

SCHEMA_VERSION = 1

MIN_GPIO = 0
MAX_GPIO = 39
# GPIO6-GPIO11 are wired to the on-chip SPI flash on every ESP32 variant.
RESERVED_FLASH_GPIOS = frozenset(range(6, 12))
ALLOWED_DIRECTIONS = (
    "output",
    "input",
    "input_pullup",
    "input_pulldown",
    "input_output",
)
ALLOWED_ACTIVE_LEVELS = ("HIGH", "LOW", "NONE")

# Keeps a single serial line (and a manifest entry) readable.
MAX_TEXT_LENGTH = 512

PIN_FIELDS = frozenset({"gpio", "label", "direction", "activeLevel", "note"})
DOCUMENT_FIELDS = frozenset(
    {"schemaVersion", "board", "pins", "powerNote", "notes"}
)


class PinsManifestError(ValueError):
    """Raised when ``pins.json`` (or an embedded pin block) is invalid."""


def _is_int(value: Any) -> bool:
    # bool is a subclass of int; ``true`` must not pass as a GPIO number.
    return isinstance(value, int) and not isinstance(value, bool)


def _text(value: Any, where: str, *, optional: bool = False) -> str | None:
    if value is None and optional:
        return None
    if not isinstance(value, str):
        raise PinsManifestError(f"{where}: expected a string")
    stripped = value.strip()
    if not stripped:
        if optional:
            return None
        raise PinsManifestError(f"{where}: must not be empty")
    if len(stripped) > MAX_TEXT_LENGTH:
        raise PinsManifestError(
            f"{where}: longer than {MAX_TEXT_LENGTH} characters"
        )
    return stripped


def _reject_unknown_keys(obj: Mapping[str, Any], allowed: frozenset[str], where: str) -> None:
    unknown = sorted(set(obj) - set(allowed))
    if unknown:
        raise PinsManifestError(
            f"{where}: unknown key(s) {', '.join(unknown)}; "
            f"allowed keys are {', '.join(sorted(allowed))}"
        )


def validate_pin(entry: Any, *, index: int, source: str) -> dict[str, Any]:
    """Validate one ``pins[]`` entry and return a normalised copy."""
    where = f"{source}: pins[{index}]"
    if not isinstance(entry, Mapping):
        raise PinsManifestError(f"{where}: expected an object")
    _reject_unknown_keys(entry, PIN_FIELDS, where)

    if "gpio" not in entry:
        raise PinsManifestError(f"{where}: missing 'gpio'")
    gpio = entry["gpio"]
    if not _is_int(gpio):
        raise PinsManifestError(f"{where}: 'gpio' must be an integer")
    if not MIN_GPIO <= gpio <= MAX_GPIO:
        raise PinsManifestError(
            f"{where}: 'gpio' {gpio} is outside {MIN_GPIO}-{MAX_GPIO}"
        )
    if gpio in RESERVED_FLASH_GPIOS:
        raise PinsManifestError(
            f"{where}: 'gpio' {gpio} is reserved for the on-chip flash "
            "(GPIO6-GPIO11 cannot be used)"
        )

    label = _text(entry.get("label"), f"{where}.label")

    if "direction" not in entry:
        raise PinsManifestError(f"{where}: missing 'direction'")
    direction = _text(entry["direction"], f"{where}.direction")
    assert direction is not None
    direction = direction.lower()
    if direction not in ALLOWED_DIRECTIONS:
        raise PinsManifestError(
            f"{where}.direction: {direction!r} is not one of "
            f"{', '.join(ALLOWED_DIRECTIONS)}"
        )

    active_level = _text(
        entry.get("activeLevel"), f"{where}.activeLevel", optional=True
    )
    if active_level is not None:
        active_level = active_level.upper()
        if active_level not in ALLOWED_ACTIVE_LEVELS:
            raise PinsManifestError(
                f"{where}.activeLevel: {active_level!r} is not one of "
                f"{', '.join(ALLOWED_ACTIVE_LEVELS)}"
            )

    note = _text(entry.get("note"), f"{where}.note", optional=True)

    normalised: dict[str, Any] = {
        "gpio": gpio,
        "label": label,
        "direction": direction,
    }
    if active_level is not None:
        normalised["activeLevel"] = active_level
    if note is not None:
        normalised["note"] = note
    return normalised


def validate_pin_list(pins: Any, *, source: str) -> list[dict[str, Any]]:
    """Validate a ``pins`` array (non-empty, unique GPIO numbers)."""
    if not isinstance(pins, Sequence) or isinstance(pins, (str, bytes)):
        raise PinsManifestError(f"{source}: 'pins' must be an array")
    if not pins:
        raise PinsManifestError(f"{source}: 'pins' must list at least one pin")

    validated: list[dict[str, Any]] = []
    seen: dict[int, int] = {}
    for index, entry in enumerate(pins):
        pin = validate_pin(entry, index=index, source=source)
        gpio = pin["gpio"]
        if gpio in seen:
            raise PinsManifestError(
                f"{source}: pins[{index}] repeats gpio {gpio} "
                f"(already used by pins[{seen[gpio]}])"
            )
        seen[gpio] = index
        validated.append(pin)
    return validated


def _validate_notes(notes: Any, *, source: str) -> list[str]:
    if notes is None:
        return []
    if not isinstance(notes, Sequence) or isinstance(notes, (str, bytes)):
        raise PinsManifestError(f"{source}: 'notes' must be an array of strings")
    validated: list[str] = []
    for index, note in enumerate(notes):
        text = _text(note, f"{source}: notes[{index}]")
        assert text is not None
        validated.append(text)
    return validated


def validate_document(
    document: Any,
    *,
    source: str = "pins.json",
    expected_board: str | None = None,
) -> dict[str, Any]:
    """Validate the whole ``pins.json`` document and return a normalised copy."""
    if not isinstance(document, Mapping):
        raise PinsManifestError(f"{source}: expected a JSON object")
    _reject_unknown_keys(document, DOCUMENT_FIELDS, source)

    if "schemaVersion" not in document:
        raise PinsManifestError(f"{source}: missing 'schemaVersion'")
    schema_version = document["schemaVersion"]
    if not _is_int(schema_version):
        raise PinsManifestError(f"{source}: 'schemaVersion' must be an integer")
    if schema_version != SCHEMA_VERSION:
        raise PinsManifestError(
            f"{source}: unsupported schemaVersion {schema_version} "
            f"(this build understands {SCHEMA_VERSION})"
        )

    board = _text(document.get("board"), f"{source}: board", optional=True)
    if board is not None and expected_board and board != expected_board:
        raise PinsManifestError(
            f"{source}: board {board!r} does not match the build board "
            f"{expected_board!r}"
        )

    if "pins" not in document:
        raise PinsManifestError(f"{source}: missing 'pins'")
    pins = validate_pin_list(document["pins"], source=source)

    power_note = _text(
        document.get("powerNote"), f"{source}: powerNote", optional=True
    )
    notes = _validate_notes(document.get("notes"), source=source)

    normalised: dict[str, Any] = {
        "schemaVersion": SCHEMA_VERSION,
        "pins": pins,
        "notes": notes,
    }
    if board is not None:
        normalised["board"] = board
    if power_note is not None:
        normalised["powerNote"] = power_note
    return normalised


def load_pins_file(path: Path, *, expected_board: str | None = None) -> dict[str, Any]:
    """Read and validate ``pins.json`` from ``path``."""
    try:
        raw = path.read_text(encoding="utf-8")
    except OSError as exc:
        raise PinsManifestError(f"cannot read {path}: {exc}") from exc
    try:
        document = json.loads(raw, strict=False)
    except json.JSONDecodeError as exc:
        raise PinsManifestError(f"{path}: invalid JSON ({exc})") from exc
    return validate_document(
        document, source=str(path), expected_board=expected_board
    )


def wiring_notes(document: Mapping[str, Any]) -> list[str]:
    """The power note first, then the board notes, as flat display lines."""
    notes: list[str] = []
    power_note = document.get("powerNote")
    if isinstance(power_note, str) and power_note:
        notes.append(power_note)
    notes.extend(document.get("notes") or [])
    return notes


def manifest_pin_block(document: Mapping[str, Any]) -> dict[str, Any]:
    """The ``flash-manifest.json`` fields consumed by the Android flasher."""
    block: dict[str, Any] = {
        "pins": document["pins"],
        "wiringNotes": wiring_notes(document),
    }
    if document.get("powerNote"):
        block["powerNote"] = document["powerNote"]
    return block


def _cpp_string(text: str) -> str:
    """Encode ``text`` as a C++ string literal, keeping UTF-8 readable."""
    special = {
        "\\": "\\\\",
        '"': '\\"',
        "\n": "\\n",
        "\r": "\\r",
        "\t": "\\t",
    }
    parts: list[str] = ['"']
    for char in text:
        code = ord(char)
        if char in special:
            parts.append(special[char])
        elif code < 0x20 or code == 0x7F:
            parts.append(f"\\{code:03o}")
        else:
            parts.append(char)
    parts.append('"')
    return "".join(parts)


def _cpp_array(values: Iterable[str]) -> str:
    return "".join(f"    {_cpp_string(value)},\n" for value in values)


def render_pinout_header(document: Mapping[str, Any] | None) -> str:
    """Render ``pinout_generated.h`` for the firmware and the native tests."""
    lines: list[str] = [
        "// Generated by scripts/pio_pinout_header.py from pins.json.",
        "// Do not edit by hand: change pins.json and rebuild instead.",
        "#pragma once",
        "",
        "#include <cstddef>",
        "",
        '#include "pinout.h"',
        "",
        "namespace fw {",
        "namespace pins {",
        "",
    ]

    if document is None:
        board = ""
        schema_version = 0
        pins: list[dict[str, Any]] = []
        power_note = ""
        notes: list[str] = []
    else:
        board = str(document.get("board") or "")
        schema_version = int(document.get("schemaVersion") or 0)
        pins = list(document.get("pins") or [])
        power_note = str(document.get("powerNote") or "")
        notes = list(document.get("notes") or [])

    if pins:
        lines.append("constexpr PinWiring kPins[] = {")
        for pin in pins:
            lines.append(
                "    {"
                f"{pin['gpio']}, {_cpp_string(pin['label'])}, "
                f"{_cpp_string(pin['direction'])}, "
                f"{_cpp_string(pin.get('activeLevel', ''))}, "
                f"{_cpp_string(pin.get('note', ''))}"
                "},"
            )
        lines.append("};")
        lines.append("constexpr size_t kPinCount = sizeof(kPins) / sizeof(kPins[0]);")
    else:
        # A zero length array is not valid C++; one placeholder keeps the
        # symbol usable while kPinCount stays 0.
        lines.append('constexpr PinWiring kPins[] = {{-1, "", "", "", ""}};')
        lines.append("constexpr size_t kPinCount = 0;")
    lines.append("")

    lines.append(f"constexpr const char* kPowerNote = {_cpp_string(power_note)};")
    lines.append("")
    if notes:
        lines.append("constexpr const char* kNotes[] = {")
        lines.append(_cpp_array(notes).rstrip("\n"))
        lines.append("};")
        lines.append("constexpr size_t kNoteCount = sizeof(kNotes) / sizeof(kNotes[0]);")
    else:
        lines.append('constexpr const char* kNotes[] = {""};')
        lines.append("constexpr size_t kNoteCount = 0;")
    lines.append("")
    lines.append(f"constexpr const char* kBoard = {_cpp_string(board)};")
    lines.append(f"constexpr int kSchemaVersion = {schema_version};")
    lines.append("")
    lines.append("}  // namespace pins")
    lines.append("}  // namespace fw")
    lines.append("")
    return "\n".join(lines)

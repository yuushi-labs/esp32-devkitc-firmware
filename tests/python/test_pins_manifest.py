"""Tests for scripts/pins_manifest.py (pins.json validation and rendering)."""

from __future__ import annotations

import copy
import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

import pins_manifest  # noqa: E402  (path set up above)
from pins_manifest import (  # noqa: E402
    PinsManifestError,
    load_pins_file,
    manifest_pin_block,
    render_pinout_header,
    validate_document,
    validate_pin_list,
    wiring_notes,
)

REPO_PINS_PATH = ROOT / "pins.json"


def sample_document() -> dict:
    return {
        "schemaVersion": 1,
        "board": "esp32dev",
        "pins": [
            {
                "gpio": 2,
                "label": "LED",
                "direction": "output",
                "activeLevel": "HIGH",
                "note": "220 ohm to GND",
            },
            {
                "gpio": 0,
                "label": "BOOT",
                "direction": "input_pullup",
                "activeLevel": "LOW",
            },
        ],
        "powerNote": "3.3 V logic only",
        "notes": ["GPIO6-11 are flash pins"],
    }


class RepositoryPinsFileTest(unittest.TestCase):
    def test_repository_pins_json_is_valid_and_matches_the_firmware(self):
        document = load_pins_file(REPO_PINS_PATH, expected_board="esp32dev")

        self.assertEqual(document["board"], "esp32dev")
        by_gpio = {pin["gpio"]: pin for pin in document["pins"]}

        # src/app_config.h uses GPIO2 for the onboard LED.
        self.assertEqual(by_gpio[2]["direction"], "output")
        self.assertEqual(by_gpio[2]["activeLevel"], "HIGH")
        self.assertIn("220", by_gpio[2]["note"])

        self.assertEqual(by_gpio[0]["direction"], "input_pullup")
        self.assertEqual(by_gpio[0]["activeLevel"], "LOW")

        self.assertIn("3.3V", document["powerNote"])
        joined = " ".join(document["notes"])
        self.assertIn("GPIO6", joined)
        self.assertIn("フラッシュ", joined)

    def test_repository_power_note_and_flash_warning_reach_the_manifest(self):
        document = load_pins_file(REPO_PINS_PATH)
        block = manifest_pin_block(document)

        notes = " ".join(block["wiringNotes"])
        self.assertIn("5V入力禁止", notes)
        self.assertIn("GPIO6", notes)
        self.assertEqual(block["wiringNotes"][0], document["powerNote"])


class ValidateDocumentTest(unittest.TestCase):
    def test_accepts_and_normalises_a_valid_document(self):
        document = copy.deepcopy(sample_document())
        document["pins"][0]["direction"] = "OUTPUT"
        document["pins"][0]["activeLevel"] = "high"
        document["pins"][0]["label"] = "  LED  "

        validated = validate_document(document)

        self.assertEqual(validated["pins"][0]["direction"], "output")
        self.assertEqual(validated["pins"][0]["activeLevel"], "HIGH")
        self.assertEqual(validated["pins"][0]["label"], "LED")
        self.assertNotIn("note", validated["pins"][1])
        self.assertEqual(validated["notes"], ["GPIO6-11 are flash pins"])

    def test_missing_schema_version_is_rejected(self):
        document = sample_document()
        del document["schemaVersion"]
        with self.assertRaises(PinsManifestError):
            validate_document(document)

    def test_unsupported_schema_version_is_rejected(self):
        document = sample_document()
        document["schemaVersion"] = 2
        with self.assertRaisesRegex(PinsManifestError, "schemaVersion"):
            validate_document(document)

    def test_boolean_schema_version_is_rejected(self):
        document = sample_document()
        document["schemaVersion"] = True
        with self.assertRaises(PinsManifestError):
            validate_document(document)

    def test_unknown_document_key_is_rejected(self):
        document = sample_document()
        document["wiringNotes"] = ["typo"]
        with self.assertRaisesRegex(PinsManifestError, "unknown key"):
            validate_document(document)

    def test_board_mismatch_is_rejected(self):
        with self.assertRaisesRegex(PinsManifestError, "does not match"):
            validate_document(sample_document(), expected_board="esp32s3")

    def test_missing_pins_is_rejected(self):
        document = sample_document()
        del document["pins"]
        with self.assertRaisesRegex(PinsManifestError, "'pins'"):
            validate_document(document)

    def test_empty_pin_list_is_rejected(self):
        document = sample_document()
        document["pins"] = []
        with self.assertRaises(PinsManifestError):
            validate_document(document)

    def test_pins_must_be_an_array(self):
        document = sample_document()
        document["pins"] = {"gpio": 2}
        with self.assertRaises(PinsManifestError):
            validate_document(document)

    def test_notes_must_be_strings(self):
        document = sample_document()
        document["notes"] = ["ok", 3]
        with self.assertRaises(PinsManifestError):
            validate_document(document)

    def test_pins_must_be_an_array(self):
        for value in (None, {}, "GPIO2", 2):
            with self.subTest(value=value):
                with self.assertRaisesRegex(PinsManifestError, "array"):
                    validate_pin_list(value, source="test")

    def test_blank_optional_text_is_dropped(self):
        document = sample_document()
        document["powerNote"] = "   "
        document["pins"][1]["note"] = "  "

        validated = validate_document(document)

        self.assertNotIn("powerNote", validated)
        self.assertNotIn("note", validated["pins"][1])

    def test_blank_note_entry_is_rejected(self):
        document = sample_document()
        document["notes"] = ["  "]
        with self.assertRaisesRegex(PinsManifestError, "notes\[0\]"):
            validate_document(document)


class ValidatePinTest(unittest.TestCase):
    def test_gpio_must_be_an_integer(self):
        for value in ("2", 2.0, True, None):
            with self.subTest(value=value):
                with self.assertRaisesRegex(PinsManifestError, "integer"):
                    validate_pin_list([{"gpio": value, "label": "x",
                                        "direction": "output"}], source="test")

    def test_gpio_must_be_in_range(self):
        for value in (-1, 40, 100):
            with self.subTest(value=value):
                with self.assertRaisesRegex(PinsManifestError, "outside"):
                    validate_pin_list([{"gpio": value, "label": "x",
                                        "direction": "output"}], source="test")

    def test_flash_pins_are_rejected(self):
        for value in range(6, 12):
            with self.subTest(gpio=value):
                with self.assertRaisesRegex(PinsManifestError, "flash"):
                    validate_pin_list([{"gpio": value, "label": "x",
                                        "direction": "output"}], source="test")

    def test_label_is_required_and_must_not_be_empty(self):
        with self.assertRaisesRegex(PinsManifestError, "missing 'gpio'"):
            validate_pin_list([{"label": "x", "direction": "output"}],
                              source="test")
        for label in ("", "   ", None, 5):
            with self.subTest(label=label):
                with self.assertRaisesRegex(PinsManifestError, "label"):
                    validate_pin_list([{"gpio": 4, "label": label,
                                        "direction": "output"}], source="test")

    def test_direction_is_required_and_restricted(self):
        with self.assertRaisesRegex(PinsManifestError, "missing 'direction'"):
            validate_pin_list([{"gpio": 4, "label": "x"}], source="test")
        with self.assertRaisesRegex(PinsManifestError, "direction"):
            validate_pin_list([{"gpio": 4, "label": "x", "direction": "magic"}],
                              source="test")

    def test_active_level_is_optional_but_restricted(self):
        pin = validate_pin_list([{"gpio": 4, "label": "x",
                                  "direction": "output"}], source="test")[0]
        self.assertNotIn("activeLevel", pin)

        with self.assertRaisesRegex(PinsManifestError, "activeLevel"):
            validate_pin_list([{"gpio": 4, "label": "x", "direction": "output",
                                "activeLevel": "HIGHISH"}], source="test")

    def test_unknown_pin_key_is_rejected(self):
        with self.assertRaisesRegex(PinsManifestError, "unknown key"):
            validate_pin_list([{"gpio": 4, "label": "x",
                                "direction": "output", "colour": "blue"}],
                              source="test")

    def test_duplicate_gpio_is_rejected(self):
        pins = [
            {"gpio": 4, "label": "a", "direction": "output"},
            {"gpio": 4, "label": "b", "direction": "output"},
        ]
        with self.assertRaisesRegex(PinsManifestError, "repeats gpio 4"):
            validate_pin_list(pins, source="test")

    def test_over_long_text_is_rejected(self):
        long_label = "x" * (pins_manifest.MAX_TEXT_LENGTH + 1)
        with self.assertRaisesRegex(PinsManifestError, "characters"):
            validate_pin_list([{"gpio": 4, "label": long_label,
                                "direction": "output"}], source="test")


class LoadFileTest(unittest.TestCase):
    def test_invalid_json_reports_the_path(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "pins.json"
            path.write_text("{not json", encoding="utf-8")
            with self.assertRaisesRegex(PinsManifestError, "invalid JSON"):
                load_pins_file(path)

    def test_missing_file_reports_the_path(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaisesRegex(PinsManifestError, "cannot read"):
                load_pins_file(Path(tmp) / "absent.json")

    def test_utf8_labels_survive_a_round_trip(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "pins.json"
            document = sample_document()
            document["pins"][0]["label"] = "オンボードLED"
            path.write_text(json.dumps(document, ensure_ascii=False),
                            encoding="utf-8")
            self.assertEqual(load_pins_file(path)["pins"][0]["label"],
                             "オンボードLED")


class WiringNotesTest(unittest.TestCase):
    def test_power_note_comes_first(self):
        self.assertEqual(
            wiring_notes({"powerNote": "power", "notes": ["a", "b"]}),
            ["power", "a", "b"],
        )

    def test_notes_are_empty_without_a_power_note(self):
        self.assertEqual(wiring_notes({"notes": ["a"]}), ["a"])
        self.assertEqual(wiring_notes({}), [])

    def test_manifest_block_keeps_the_power_note(self):
        block = manifest_pin_block(validate_document(sample_document()))
        self.assertEqual(block["powerNote"], "3.3 V logic only")
        self.assertEqual(len(block["pins"]), 2)
        self.assertEqual(len(block["wiringNotes"]), 2)


class RenderHeaderTest(unittest.TestCase):
    def test_header_lists_every_pin_and_note(self):
        header = render_pinout_header(validate_document(sample_document()))

        self.assertIn('"esp32dev"', header)
        self.assertIn("kSchemaVersion = 1", header)
        self.assertIn('{2, "LED", "output", "HIGH", "220 ohm to GND"}', header)
        self.assertIn('{0, "BOOT", "input_pullup", "LOW", ""}', header)
        self.assertIn("kPinCount = sizeof(kPins) / sizeof(kPins[0])", header)
        self.assertIn('"3.3 V logic only"', header)
        self.assertIn('"GPIO6-11 are flash pins"', header)
        self.assertIn('#include "pinout.h"', header)

    def test_header_without_a_pins_file_still_compiles_as_empty(self):
        header = render_pinout_header(None)

        self.assertIn("kPinCount = 0", header)
        self.assertIn("kNoteCount = 0", header)
        self.assertIn('kPowerNote = ""', header)
        self.assertIn('kBoard = ""', header)
        self.assertNotIn("kPinCount = sizeof", header)

    def test_strings_are_escaped_for_cpp(self):
        document = validate_document({
            "schemaVersion": 1,
            "pins": [{
                "gpio": 2,
                "label": 'say "hi"\\there',
                "direction": "output",
                "note": "line1\nline2\ttab",
            }],
            "powerNote": "caf\u00e9",
        })
        header = render_pinout_header(document)

        self.assertIn(r'\"', header)
        self.assertIn(r'say \"hi\"\\there', header)
        self.assertIn(r"line1\nline2\ttab", header)
        self.assertIn("caf\u00e9", header)


if __name__ == "__main__":
    unittest.main()

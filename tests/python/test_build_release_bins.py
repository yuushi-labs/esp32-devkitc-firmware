"""Tests for scripts/build_release_bins.py (manifest pin embedding)."""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

import build_release_bins as build  # noqa: E402  (path set up above)

GOOD_PINS = {
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

SEGMENT_FILES = {
    "bootloader.bin": b"BOOT" * 8,
    "partitions.bin": b"PART" * 8,
    "boot_app0.bin": b"APP0" * 8,
    "firmware.bin": b"FIRM" * 8,
}


def make_build_dir(root: Path) -> Path:
    build_dir = root / "build"
    build_dir.mkdir()
    for name, data in SEGMENT_FILES.items():
        (build_dir / name).write_bytes(data)
    return build_dir


def write_pins(path: Path, document: dict | None = None) -> Path:
    path.write_text(json.dumps(document or GOOD_PINS, ensure_ascii=False),
                    encoding="utf-8")
    return path


class BuildReleasePinsTest(unittest.TestCase):
    def build(self, output_dir: Path, pin_document: dict | None) -> dict:
        root = output_dir.parent
        return build.build_release(
            build_dir=make_build_dir(root),
            output_dir=output_dir,
            version="0.1.1",
            chip="esp32",
            board="esp32dev",
            flash_mode="dio",
            flash_freq="40m",
            flash_size="4MB",
            extra_source_dirs=(),
            pin_document=pin_document,
        )

    def test_pins_and_wiring_notes_are_embedded(self):
        with tempfile.TemporaryDirectory() as tmp:
            output_dir = Path(tmp) / "dist"
            document = build.load_pin_document(
                write_pins(Path(tmp) / "pins.json"),
                board="esp32dev",
                explicit=True,
            )
            manifest = self.build(output_dir, document)

            on_disk = json.loads(
                (output_dir / "flash-manifest.json").read_text(encoding="utf-8")
            )
            self.assertEqual(on_disk, manifest)
            self.assertEqual(len(on_disk["pins"]), 2)
            self.assertEqual(on_disk["pins"][0]["gpio"], 2)
            self.assertEqual(on_disk["pins"][0]["activeLevel"], "HIGH")
            self.assertEqual(on_disk["wiringNotes"][0], "3.3 V logic only")
            self.assertIn("GPIO6-11", on_disk["wiringNotes"][1])
            self.assertEqual(on_disk["powerNote"], "3.3 V logic only")
            self.assertEqual(on_disk["build"]["gitCommit"], build.detect_git_commit())

            # The pin block must not break the existing asset verification.
            build.verify_release(output_dir)

    def test_manifest_shape_is_unchanged_without_pins(self):
        with tempfile.TemporaryDirectory() as tmp:
            output_dir = Path(tmp) / "dist"
            manifest = self.build(output_dir, None)

            self.assertNotIn("pins", manifest)
            self.assertNotIn("wiringNotes", manifest)
            self.assertNotIn("powerNote", manifest)
            build.verify_release(output_dir)

    def test_pins_are_placed_before_the_build_block(self):
        with tempfile.TemporaryDirectory() as tmp:
            output_dir = Path(tmp) / "dist"
            document = build.load_pin_document(
                write_pins(Path(tmp) / "pins.json"),
                board="esp32dev",
                explicit=True,
            )
            manifest = self.build(output_dir, document)
            keys = list(manifest)
            self.assertLess(keys.index("pins"), keys.index("build"))

    def test_verify_release_rejects_a_duplicate_gpio_in_the_manifest(self):
        with tempfile.TemporaryDirectory() as tmp:
            output_dir = Path(tmp) / "dist"
            document = build.load_pin_document(
                write_pins(Path(tmp) / "pins.json"),
                board="esp32dev",
                explicit=True,
            )
            self.build(output_dir, document)

            manifest_path = output_dir / "flash-manifest.json"
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest["pins"][1]["gpio"] = manifest["pins"][0]["gpio"]
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

            with self.assertRaises(SystemExit):
                build.verify_release(output_dir)

    def test_verify_release_rejects_a_null_pins_block(self):
        with tempfile.TemporaryDirectory() as tmp:
            output_dir = Path(tmp) / "dist"
            document = build.load_pin_document(
                write_pins(Path(tmp) / "pins.json"),
                board="esp32dev",
                explicit=True,
            )
            self.build(output_dir, document)

            manifest_path = output_dir / "flash-manifest.json"
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest["pins"] = None
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

            with self.assertRaises(SystemExit):
                build.verify_release(output_dir)

    def test_verify_release_rejects_a_non_string_wiring_note(self):
        with tempfile.TemporaryDirectory() as tmp:
            output_dir = Path(tmp) / "dist"
            document = build.load_pin_document(
                write_pins(Path(tmp) / "pins.json"),
                board="esp32dev",
                explicit=True,
            )
            self.build(output_dir, document)

            manifest_path = output_dir / "flash-manifest.json"
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest["wiringNotes"] = ["ok", 7]
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

            with self.assertRaises(SystemExit):
                build.verify_release(output_dir)


class LoadPinDocumentTest(unittest.TestCase):
    def test_default_path_without_a_file_means_no_pin_information(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.assertIsNone(
                build.load_pin_document(Path(tmp) / "pins.json",
                                        board="esp32dev", explicit=False)
            )

    def test_explicit_path_must_exist(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(SystemExit) as raised:
                build.load_pin_document(Path(tmp) / "pins.json",
                                        board="esp32dev", explicit=True)
            self.assertIn("--pins not found", str(raised.exception))

    def test_invalid_pins_file_stops_the_build_with_a_message(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = write_pins(Path(tmp) / "pins.json",
                              {"schemaVersion": 1, "pins": [{"gpio": 2}]})
            with self.assertRaises(SystemExit) as raised:
                build.load_pin_document(path, board="esp32dev", explicit=True)
            self.assertIn("label", str(raised.exception))

    def test_board_mismatch_stops_the_build(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = write_pins(Path(tmp) / "pins.json")
            with self.assertRaises(SystemExit) as raised:
                build.load_pin_document(path, board="esp32s3", explicit=True)
            self.assertIn("does not match", str(raised.exception))


class CommandLineTest(unittest.TestCase):
    def test_cli_embeds_the_default_repository_pins_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            build_dir = make_build_dir(root)
            output_dir = root / "dist"
            exit_code = build.main([
                "--build-dir", str(build_dir),
                "--output-dir", str(output_dir),
                "--version", "0.1.1",
            ])
            self.assertEqual(exit_code, 0)

            manifest = json.loads(
                (output_dir / "flash-manifest.json").read_text(encoding="utf-8")
            )
            # The repository's pins.json describes the station wiring.
            self.assertEqual(
                {pin["gpio"] for pin in manifest["pins"]},
                {0, 2, 4, 21, 22, 25, 26, 27, 32, 33},
            )

    def test_cli_accepts_an_explicit_pins_path(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            build_dir = make_build_dir(root)
            output_dir = root / "dist"
            pins_path = write_pins(root / "custom-pins.json",
                                   {**GOOD_PINS, "pins": GOOD_PINS["pins"][:1]})
            exit_code = build.main([
                "--build-dir", str(build_dir),
                "--output-dir", str(output_dir),
                "--version", "0.1.1",
                "--pins", str(pins_path),
            ])
            self.assertEqual(exit_code, 0)

            manifest = json.loads(
                (output_dir / "flash-manifest.json").read_text(encoding="utf-8")
            )
            self.assertEqual([pin["gpio"] for pin in manifest["pins"]], [2])


class VariantReleaseTest(unittest.TestCase):
    """v0.2.0 ships two firmwares in one release."""

    def build(self, output_dir: Path, variant: str | None) -> dict:
        return build.build_release(
            build_dir=make_build_dir(output_dir.parent),
            output_dir=output_dir,
            version="0.2.0",
            chip="esp32",
            board="esp32dev",
            flash_mode="dio",
            flash_freq="40m",
            flash_size="4MB",
            extra_source_dirs=(),
            pin_document=None,
            variant=variant,
        )

    def test_station_assets_keep_the_default_manifest_name(self):
        with tempfile.TemporaryDirectory() as tmp:
            output_dir = Path(tmp) / "dist"
            manifest = self.build(output_dir, "station")

            self.assertEqual(manifest["variant"], "station")
            self.assertIn("station", manifest["variantLabel"])
            self.assertEqual(manifest["merged"]["filename"],
                             "esp32-devkitc-station-merged.bin")
            self.assertEqual(manifest["parts"][0]["filename"],
                             "esp32-devkitc-station-app.bin")
            self.assertTrue((output_dir / "flash-manifest.json").is_file())
            self.assertFalse((output_dir / "tester-flash-manifest.json").exists())

            filesystem = manifest["filesystems"][0]
            self.assertEqual(filesystem["type"], "littlefs")
            self.assertEqual(int(filesystem["offset"], 16), 0x290000)
            self.assertEqual(filesystem["size"], 0x160000)
            self.assertEqual(filesystem["logFile"], "/log.csv")

            build.verify_release(output_dir, "flash-manifest.json")

    def test_tester_has_its_own_manifest_and_no_log_partition(self):
        with tempfile.TemporaryDirectory() as tmp:
            output_dir = Path(tmp) / "dist"
            manifest = self.build(output_dir, "tester")

            self.assertEqual(manifest["variant"], "tester")
            self.assertNotIn("filesystems", manifest)
            self.assertEqual(manifest["merged"]["filename"],
                             "esp32-devkitc-tester-merged.bin")
            self.assertTrue((output_dir / "tester-flash-manifest.json").is_file())
            self.assertFalse((output_dir / "flash-manifest.json").exists())

            build.verify_release(output_dir, "tester-flash-manifest.json")

    def test_legacy_names_without_a_variant_are_unchanged(self):
        with tempfile.TemporaryDirectory() as tmp:
            output_dir = Path(tmp) / "dist"
            manifest = self.build(output_dir, None)

            self.assertNotIn("variant", manifest)
            self.assertNotIn("filesystems", manifest)
            self.assertEqual(manifest["merged"]["filename"],
                             build.MERGED_FILENAME)
            build.verify_release(output_dir)

    def test_unknown_variant_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(SystemExit):
                self.build(Path(tmp) / "dist", "prototype")

    def test_cli_selects_the_variant_and_manifest(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            build_dir = make_build_dir(root)
            output_dir = root / "dist"
            exit_code = build.main([
                "--build-dir", str(build_dir),
                "--output-dir", str(output_dir),
                "--version", "0.2.0",
                "--variant", "tester",
            ])
            self.assertEqual(exit_code, 0)
            self.assertTrue((output_dir / "tester-flash-manifest.json").is_file())
            self.assertTrue((output_dir / "esp32-devkitc-tester-merged.bin").is_file())

    def test_verify_rejects_a_broken_filesystem_block(self):
        with tempfile.TemporaryDirectory() as tmp:
            output_dir = Path(tmp) / "dist"
            manifest = self.build(output_dir, "station")
            manifest["filesystems"][0]["offset"] = "not-a-number"
            (output_dir / "flash-manifest.json").write_text(
                json.dumps(manifest), encoding="utf-8"
            )
            with self.assertRaises(SystemExit):
                build.verify_release(output_dir, "flash-manifest.json")

    def test_verify_rejects_a_missing_manifest(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(SystemExit):
                build.verify_release(Path(tmp) / "dist", "tester-flash-manifest.json")


if __name__ == "__main__":
    unittest.main()

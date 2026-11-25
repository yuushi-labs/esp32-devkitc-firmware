#!/usr/bin/env python3
"""Build the release binaries for esp32-devkitc-firmware.

Takes the PlatformIO build output for ``esp32dev_station`` (or
``esp32dev_tester`` with ``--variant tester``) and produces the assets consumed
by the Android flasher:

* ``esp32-devkitc-<variant>-merged.bin`` - bootloader + partitions + boot_app0
  + firmware merged into one image based at 0x0000
* ``esp32-devkitc-<variant>-app.bin``    - the application image (0x10000)
* ``flash-manifest.json`` (plus ``tester-flash-manifest.json`` for the tester)
  - offsets, sizes, SHA-256 and version info

The merge is byte-for-byte equivalent to ``esptool.py merge_bin`` with the
standard ESP32 layout and the default 0xFF fill, implemented here without extra
dependencies so it also runs on a bare CI runner.

The station manifest also advertises the LittleFS data partition parsed from
``no_ota.csv`` so the flasher can pull ``/log.csv`` back out.

Usage:
    python scripts/build_release_bins.py \
        --build-dir .pio/build/esp32dev_station --output-dir dist --version 0.3.0 \
        --variant station
    python scripts/build_release_bins.py \
        --build-dir .pio/build/esp32dev_tester --output-dir dist \
        --version 0.3.0 --variant tester
    python scripts/build_release_bins.py --verify-only --output-dir dist

``pins.json`` (repository root, or ``--pins PATH``) is validated and embedded
in ``flash-manifest.json`` as ``pins``/``wiringNotes`` so the Android flasher
can show which GPIO to wire where. Without it the manifest keeps its previous
shape.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from pins_manifest import (  # noqa: E402 - after the sys.path tweak
    PinsManifestError,
    load_pins_file,
    manifest_pin_block,
    validate_pin_list,
)

# Legacy single firmware names, still the default so older callers and the
# existing tests keep producing exactly the same assets.
MERGED_FILENAME = "esp32-devkitc-firmware-merged.bin"
APP_FILENAME = "esp32-devkitc-firmware-app.bin"
MANIFEST_FILENAME = "flash-manifest.json"


@dataclass(frozen=True)
class VariantNaming:
    """Asset names and manifest name for one firmware variant."""

    key: str | None
    label: str
    prefix: str
    manifest: str


VARIANTS: dict[str | None, VariantNaming] = {
    None: VariantNaming(None, "firmware", "esp32-devkitc-firmware", MANIFEST_FILENAME),
    "station": VariantNaming(
        "station",
        "環境ステーション (station)",
        "esp32-devkitc-station",
        MANIFEST_FILENAME,
    ),
    "tester": VariantNaming(
        "tester",
        "パーツ動作確認テスト (tester)",
        "esp32-devkitc-tester",
        # Deliberately sorts after "flash-manifest.json": older flasher builds
        # take the first asset named exactly flash-manifest.json, so the station
        # firmware stays the default while a v0.2.0 flasher can list variants.
        "tester-flash-manifest.json",
    ),
}

# The station firmware logs CSV samples to LittleFS in the data partition of
# no_ota.csv, so the Android flasher can pull that image back out and read
# /log.csv. The offset and size are parsed from the partition table at build
# time; this tuple is only the fallback for a checkout without no_ota.csv.
STATION_FILESYSTEMS = (
    {
        "type": "littlefs",
        "label": "spiffs",
        "offset": "0x210000",
        "size": 0x1E0000,
        "mountPoint": "/littlefs",
        "format": "csv",
        "logFile": "/log.csv",
        "notes": (
            "no_ota.csv の spiffs パーティション。station ファームが "
            "/log.csv (timestamp,uptime_ms,temp_c,pressure_hpa,humidity_pct) を追記する"
        ),
    },
)

MERGED_OFFSET = 0x0000
FLASH_FILL_BYTE = 0xFF

# name, source file in the PlatformIO build dir, flash offset
SEGMENTS = (
    ("bootloader", "bootloader.bin", 0x1000),
    ("partitions", "partitions.bin", 0x8000),
    ("boot_app0", "boot_app0.bin", 0xE000),
    ("app", "firmware.bin", 0x10000),
)

APP_OFFSET = 0x10000

# 4 MB flash on the ESP32-DevKitC; a partition block that runs past it would
# point the flasher at a region that does not exist.
FLASH_SIZE_BYTES = 4 * 1024 * 1024

DEFAULT_VERSION = "0.0.0-dev"

# pins.json lives at the repository root; the firmware embeds the same file.
PROJECT_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_PINS_PATH = PROJECT_ROOT / "pins.json"
DEFAULT_PARTITION_CSV = PROJECT_ROOT / "no_ota.csv"


@dataclass(frozen=True)
class PartitionEntry:
    name: str
    type: str
    subtype: str
    offset: int
    size: int


def parse_partition_number(text: str) -> int:
    """Parse an ESP-IDF partition size/offset.

    The CSV format accepts decimal, ``0x`` hexadecimal and ``K``/``M`` suffixes
    (``spiffs,data,spiffs,2162688,1966080,`` and ``app0,app,ota_0,0x10000,2M,``
    both work), so the manifest must not read every value as hexadecimal.
    """
    value = text.strip()
    if not value:
        raise ValueError("empty number")
    multiplier = 1
    suffix = value[-1]
    if suffix in ("k", "K"):
        multiplier = 1024
        value = value[:-1]
    elif suffix in ("m", "M"):
        multiplier = 1024 * 1024
        value = value[:-1]
    if not value:
        raise ValueError("missing digits")
    if value.lower().startswith("0x"):
        number = int(value, 16)
    else:
        number = int(value, 10)
    return number * multiplier


def align_up(value: int, alignment: int) -> int:
    return (value + alignment - 1) // alignment * alignment


def load_partition_table(path: Path) -> list[PartitionEntry]:
    """Parse a PlatformIO/ESP-IDF partition CSV (comments and blanks skipped)."""
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError as exc:
        raise SystemExit(f"error: cannot read partition table {path}: {exc}") from exc

    entries: list[PartitionEntry] = []
    for number, raw in enumerate(lines, start=1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        fields = [field.strip() for field in line.split(",")]
        if len(fields) < 5:
            raise SystemExit(
                f"error: {path}:{number}: expected at least 5 fields, got {len(fields)}"
            )
        name, part_type, subtype, offset, size = fields[:5]
        try:
            size_bytes = parse_partition_number(size)
            if offset.strip():
                offset_bytes = parse_partition_number(offset)
            else:
                # ESP-IDF allows an empty offset and fills it with the end of the
                # previous partition, aligned to 4 KB.
                previous_end = entries[-1].offset + entries[-1].size if entries else 0x9000
                offset_bytes = align_up(previous_end, 0x1000)
            entries.append(
                PartitionEntry(name, part_type, subtype, offset_bytes, size_bytes)
            )
        except ValueError as exc:
            raise SystemExit(f"error: {path}:{number}: {exc}") from exc
    return entries


def station_filesystems(partition_csv: Path | None = None) -> list[dict]:
    """LittleFS/SPIFFS data partition advertised in the station manifest.

    Without a readable partition table the fixed v0.2.0 layout is the only
    information available, so it is used as a compatibility fallback. As soon as
    the table can be read it is the single source of truth: a table without a
    data partition advertises no filesystem at all, because a manifest that
    points the flasher at a region the table does not define would offer to read
    a ``/log.csv`` that cannot exist.
    """
    path = partition_csv if partition_csv is not None else DEFAULT_PARTITION_CSV
    if not path.is_file():
        return [dict(entry) for entry in STATION_FILESYSTEMS]

    entries = [
        entry
        for entry in load_partition_table(path)
        if entry.type == "data" and entry.subtype in ("spiffs", "littlefs")
    ]
    if not entries:
        return []

    return [
        {
            "type": "littlefs",
            "label": entry.name,
            "offset": hex(entry.offset),
            "size": entry.size,
            "mountPoint": "/littlefs",
            "format": "csv",
            "logFile": "/log.csv",
            "notes": (
                f"{path.name} の {entry.name} パーティション。station ファームが "
                "/log.csv (timestamp,uptime_ms,temp_c,pressure_hpa,humidity_pct) を追記する"
            ),
        }
        for entry in entries
    ]


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def hex_offset(value: int) -> str:
    return f"0x{value:04x}"


def detect_version() -> str:
    """Prefer FW_VERSION, then the git tag/commit, then a neutral default."""
    env_version = os.environ.get("FW_VERSION", "").strip()
    if env_version:
        return env_version.lstrip("v")

    try:
        described = subprocess.run(
            ["git", "describe", "--tags", "--always", "--dirty"],
            capture_output=True,
            check=True,
            text=True,
        ).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return DEFAULT_VERSION

    return described.lstrip("v") or DEFAULT_VERSION


def detect_git_commit() -> str | None:
    try:
        return subprocess.run(
            ["git", "rev-parse", "HEAD"],
            capture_output=True,
            check=True,
            text=True,
        ).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return None


def platformio_core_dir() -> Path:
    env_dir = os.environ.get("PLATFORMIO_CORE_DIR", "").strip()
    if env_dir:
        return Path(env_dir)
    return Path.home() / ".platformio"


def default_boot_app0_dirs() -> tuple[Path, ...]:
    """PlatformIO keeps boot_app0.bin inside the Arduino framework package."""
    packages = platformio_core_dir() / "packages"
    candidates = [packages / "framework-arduinoespressif32" / "tools" / "partitions"]
    candidates.extend(
        sorted(packages.glob("framework-arduinoespressif32*/tools/partitions"))
    )
    return tuple(candidates)


def resolve_source(
    build_dir: Path, filename: str, extra_dirs: tuple[Path, ...] = ()
) -> Path:
    candidates: list[Path] = []
    for directory in (build_dir, *extra_dirs):
        candidate = directory / filename
        if candidate not in candidates:
            candidates.append(candidate)
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    searched = "\n  ".join(str(path) for path in candidates)
    raise SystemExit(
        f"error: missing {filename}; looked in:\n  {searched}\n"
        "Build the firmware first with 'pio run -e esp32dev_station' "
        "(or the environment matching --build-dir)."
    )


def load_pin_document(path: Path, *, board: str, explicit: bool) -> dict | None:
    """Read pins.json when it exists.

    ``explicit`` marks a path given with ``--pins``: a missing file is then an
    error, while the default location simply means "no pin information" so
    older checkouts keep building.
    """
    if not path.is_file():
        if explicit:
            raise SystemExit(f"error: --pins not found: {path}")
        return None
    try:
        return load_pins_file(path, expected_board=board)
    except PinsManifestError as exc:
        raise SystemExit(f"error: {exc}") from exc


def merge_segments(segments: list[tuple[int, bytes]]) -> bytes:
    end = max(offset + len(data) for offset, data in segments)
    merged = bytearray([FLASH_FILL_BYTE]) * end
    for offset, data in segments:
        merged[offset : offset + len(data)] = data
    return bytes(merged)


def build_release(
    build_dir: Path,
    output_dir: Path,
    version: str,
    chip: str,
    board: str,
    flash_mode: str,
    flash_freq: str,
    flash_size: str,
    extra_source_dirs: tuple[Path, ...],
    boot_app0_path: Path | None = None,
    pin_document: dict | None = None,
    variant: str | None = None,
    partition_csv: Path | None = None,
) -> dict:
    output_dir.mkdir(parents=True, exist_ok=True)

    naming = VARIANTS.get(variant)
    if naming is None:
        raise SystemExit(f"error: unknown variant {variant!r}")
    merged_filename = f"{naming.prefix}-merged.bin"
    app_filename = f"{naming.prefix}-app.bin"

    if boot_app0_path is not None and not boot_app0_path.is_file():
        raise SystemExit(f"error: --boot-app0 not found: {boot_app0_path}")

    loaded: list[tuple[str, str, int, Path, bytes]] = []
    for name, filename, offset in SEGMENTS:
        if name == "boot_app0" and boot_app0_path is not None:
            path = boot_app0_path
        else:
            path = resolve_source(build_dir, filename, extra_source_dirs)
        loaded.append((name, filename, offset, path, path.read_bytes()))

    merged = merge_segments([(offset, data) for _, _, offset, _, data in loaded])
    merged_path = output_dir / merged_filename
    merged_path.write_bytes(merged)

    app_data = next(data for name, _, _, _, data in loaded if name == "app")
    app_path = output_dir / app_filename
    app_path.write_bytes(app_data)

    segments = [
        {
            "name": name,
            "source": filename,
            "offset": hex_offset(offset),
            "size": len(data),
            "sha256": sha256_bytes(data),
        }
        for name, filename, offset, _, data in loaded
    ]

    manifest = {
        "schemaVersion": 1,
        "chip": chip,
        "board": board,
        "version": version,
        "flashMode": flash_mode,
        "flashFreq": flash_freq,
        "flashSize": flash_size,
        "merged": {
            "filename": merged_filename,
            "offset": hex_offset(MERGED_OFFSET),
            "size": len(merged),
            "sha256": sha256_bytes(merged),
        },
        "parts": [
            {
                "filename": app_filename,
                "offset": hex_offset(APP_OFFSET),
                "size": len(app_data),
                "sha256": sha256_bytes(app_data),
            }
        ],
        "segments": segments,
    }

    if naming.key is not None:
        manifest["variant"] = naming.key
        manifest["variantLabel"] = naming.label
    if variant == "station":
        filesystems = station_filesystems(partition_csv)
        if filesystems:
            manifest["filesystems"] = filesystems

    # Pin wiring (pins.json) is optional so manifests produced before it existed
    # keep exactly the same shape.
    if pin_document is not None:
        manifest.update(manifest_pin_block(pin_document))

    manifest["build"] = {
        "tool": "scripts/build_release_bins.py",
        "generatedAt": datetime.now(timezone.utc)
        .replace(microsecond=0)
        .isoformat()
        .replace("+00:00", "Z"),
        "gitCommit": detect_git_commit(),
    }

    manifest_path = output_dir / naming.manifest
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return manifest


def verify_release(output_dir: Path, manifest_name: str = MANIFEST_FILENAME) -> dict:
    """Re-read the produced assets and check every hash and offset."""
    manifest_path = output_dir / manifest_name
    if not manifest_path.is_file():
        raise SystemExit(f"error: missing {manifest_path}")

    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    problems: list[str] = []

    def check_file(entry: dict, key: str) -> Path | None:
        path = output_dir / entry["filename"]
        if not path.is_file():
            problems.append(f"{key}: missing file {path}")
            return None
        if path.stat().st_size != entry["size"]:
            problems.append(
                f"{key}: size mismatch for {path} "
                f"(manifest {entry['size']}, actual {path.stat().st_size})"
            )
        actual = sha256_file(path)
        if actual != entry["sha256"]:
            problems.append(
                f"{key}: sha256 mismatch for {path} "
                f"(manifest {entry['sha256']}, actual {actual})"
            )
        return path

    merged_path = check_file(manifest["merged"], "merged")
    if int(manifest["merged"]["offset"], 16) != MERGED_OFFSET:
        problems.append(
            f"merged: offset {manifest['merged']['offset']} is not "
            f"{hex_offset(MERGED_OFFSET)}"
        )

    for index, part in enumerate(manifest["parts"]):
        check_file(part, f"parts[{index}]")
        if int(part["offset"], 16) != APP_OFFSET:
            problems.append(
                f"parts[{index}]: offset {part['offset']} is not "
                f"{hex_offset(APP_OFFSET)}"
            )

    expected_offsets = {name: offset for name, _, offset in SEGMENTS}
    for segment in manifest["segments"]:
        expected = expected_offsets.get(segment["name"])
        if expected is None:
            problems.append(f"segment {segment['name']}: unknown segment")
        elif int(segment["offset"], 16) != expected:
            problems.append(
                f"segment {segment['name']}: offset {segment['offset']} is not "
                f"{hex_offset(expected)}"
            )

    if merged_path is not None:
        merged = merged_path.read_bytes()
        for segment in manifest["segments"]:
            offset = int(segment["offset"], 16)
            end = offset + segment["size"]
            if end > len(merged):
                problems.append(
                    f"segment {segment['name']}: {segment['offset']} + "
                    f"{segment['size']} exceeds merged size {len(merged)}"
                )
                continue
            actual = sha256_bytes(merged[offset:end])
            if actual != segment["sha256"]:
                problems.append(
                    f"segment {segment['name']}: merged image does not contain "
                    f"the expected bytes at {segment['offset']}"
                )

    # A station manifest advertises the LittleFS partition so the flasher can
    # offer to read /log.csv back; a malformed block would break that offer, and
    # a region outside the flash would make it offer the impossible.
    if "filesystems" in manifest:
        filesystems = manifest["filesystems"]
        if not isinstance(filesystems, list) or not filesystems:
            problems.append("filesystems: expected a non-empty array")
        else:
            for index, entry in enumerate(filesystems):
                if not isinstance(entry, dict):
                    problems.append(f"filesystems[{index}]: expected an object")
                    continue
                for key in ("type", "offset", "size"):
                    if key not in entry:
                        problems.append(f"filesystems[{index}]: missing {key}")
                try:
                    offset = int(str(entry.get("offset")), 16)
                except (TypeError, ValueError):
                    problems.append(f"filesystems[{index}]: bad offset")
                    continue
                size = entry.get("size")
                if not isinstance(size, int) or size <= 0:
                    problems.append(f"filesystems[{index}]: bad size")
                elif offset + size > FLASH_SIZE_BYTES:
                    problems.append(
                        f"filesystems[{index}]: {entry.get('offset')} + {size} "
                        f"exceeds the {FLASH_SIZE_BYTES} byte flash"
                    )

    # The pin block is optional, but when it is present it must stay valid and
    # the wiring notes must be displayable strings. Check the key, not the
    # value, so an explicit "pins": null is rejected instead of skipped.
    if "pins" in manifest:
        pins = manifest["pins"]
        try:
            validate_pin_list(pins, source="flash-manifest.json")
        except PinsManifestError as exc:
            problems.append(str(exc))
        wiring_notes = manifest.get("wiringNotes")
        if not isinstance(wiring_notes, list) or not all(
            isinstance(note, str) and note.strip() for note in wiring_notes
        ):
            problems.append(
                "wiringNotes: expected an array of non-empty strings"
            )

    if problems:
        for problem in problems:
            print(f"error: {problem}", file=sys.stderr)
        raise SystemExit(1)

    print(f"verified {len(manifest['parts'])} part(s) and "
          f"{len(manifest['segments'])} merged segment(s) in {output_dir}")
    return manifest


def print_summary(manifest: dict, output_dir: Path) -> None:
    merged = manifest["merged"]
    print(f"version : {manifest['version']}")
    print(f"variant : {manifest.get('variantLabel', 'legacy firmware')}")
    print(f"board   : {manifest['board']} ({manifest['chip']})")
    print(
        f"merged  : {merged['filename']} offset={merged['offset']} "
        f"size={merged['size']} sha256={merged['sha256']}"
    )
    for part in manifest["parts"]:
        print(
            f"part    : {part['filename']} offset={part['offset']} "
            f"size={part['size']} sha256={part['sha256']}"
        )
    for segment in manifest["segments"]:
        print(
            f"segment : {segment['name']:<10} {segment['offset']} "
            f"size={segment['size']}"
        )
    pins = manifest.get("pins")
    if pins:
        gpios = ", ".join(f"GPIO{pin['gpio']}" for pin in pins)
        print(f"pins    : {len(pins)} ({gpios})")
    else:
        print("pins    : none (no pins.json)")
    for filesystem in manifest.get("filesystems", []):
        print(
            f"fs      : {filesystem['type']} {filesystem.get('label', '')} "
            f"offset={filesystem['offset']} size={filesystem['size']}"
        )
    print(f"output  : {output_dir}")


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build-dir", type=Path,
                        default=Path(".pio/build/esp32dev_station"),
                        help="PlatformIO build directory "
                             "(default: .pio/build/esp32dev_station)")
    parser.add_argument("--output-dir", type=Path, default=Path("dist"),
                        help="directory for the release assets (default: dist)")
    parser.add_argument("--version", default=None,
                        help="firmware version (default: $FW_VERSION, git describe, or "
                             f"{DEFAULT_VERSION})")
    parser.add_argument("--chip", default="esp32")
    parser.add_argument("--board", default="esp32dev")
    parser.add_argument("--flash-mode", default="dio")
    parser.add_argument("--flash-freq", default="40m")
    parser.add_argument("--flash-size", default="4MB")
    parser.add_argument("--boot-app0", type=Path, default=None,
                        help="explicit path to boot_app0.bin when it is not in the "
                             "build directory")
    parser.add_argument("--pins", type=Path, default=None,
                        help="pins.json with the GPIO wiring embedded in the "
                             "manifest (default: the repository's pins.json when "
                             "it exists)")
    parser.add_argument("--variant", choices=("station", "tester"), default=None,
                        help="firmware variant: names the assets "
                             "esp32-devkitc-<variant>-*.bin and adds variant/"
                             "filesystems blocks to the manifest")
    parser.add_argument("--verify-only", action="store_true",
                        help="only re-verify an existing output directory")
    parser.add_argument("--print-manifest", action="store_true",
                        help="print the manifest JSON to stdout")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)

    manifest_name = VARIANTS[args.variant].manifest

    if args.verify_only:
        manifest = verify_release(args.output_dir, manifest_name)
    else:
        pins_path = args.pins if args.pins is not None else DEFAULT_PINS_PATH
        pin_document = load_pin_document(
            pins_path, board=args.board, explicit=args.pins is not None
        )
        manifest = build_release(
            build_dir=args.build_dir,
            output_dir=args.output_dir,
            version=(args.version or detect_version()).lstrip("v"),
            chip=args.chip,
            board=args.board,
            flash_mode=args.flash_mode,
            flash_freq=args.flash_freq,
            flash_size=args.flash_size,
            extra_source_dirs=default_boot_app0_dirs(),
            boot_app0_path=args.boot_app0,
            pin_document=pin_document,
            variant=args.variant,
        )
        manifest = verify_release(args.output_dir, manifest_name)
        print_summary(manifest, args.output_dir)

    if args.print_manifest:
        print(json.dumps(manifest, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())

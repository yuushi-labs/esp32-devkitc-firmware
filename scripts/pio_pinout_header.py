"""PlatformIO pre-script: render ``pins.json`` into ``pinout_generated.h``.

The generated header lives in the build directory and is added to the include
path, so the firmware's ``PINOUT`` command and the native tests read the exact
same pin table that ``scripts/build_release_bins.py`` embeds in
``flash-manifest.json``. Without ``pins.json`` the build still succeeds and the
command reports that no pin data is available.

Environment:
    PINS_JSON   override the pins file path (default: <project>/pins.json)
"""

import os
import sys
from pathlib import Path

Import("env")  # noqa: F821 - injected by PlatformIO

project_dir = Path(env.subst("$PROJECT_DIR"))  # noqa: F821
build_dir = Path(env.subst("$BUILD_DIR"))  # noqa: F821

# NOTE: SCons does not guarantee __file__ inside a pre-script, so the helper
# module is found through the project directory instead.
sys.path.insert(0, str(project_dir / "scripts"))

from pins_manifest import (  # noqa: E402 - after the sys.path tweak
    PinsManifestError,
    load_pins_file,
    render_pinout_header,
)

override = os.environ.get("PINS_JSON", "").strip()
pins_path = Path(override) if override else project_dir / "pins.json"

document = None
if pins_path.is_file():
    try:
        document = load_pins_file(pins_path)
    except PinsManifestError as exc:
        sys.stderr.write(f"error: {exc}\n")
        raise SystemExit(1)
else:
    sys.stderr.write(
        f"warning: {pins_path} not found; PINOUT will report no pin data\n"
    )

header = render_pinout_header(document)
build_dir.mkdir(parents=True, exist_ok=True)
target = build_dir / "pinout_generated.h"
try:
    unchanged = target.is_file() and target.read_text(encoding="utf-8") == header
except OSError:
    unchanged = False
if not unchanged:
    target.write_text(header, encoding="utf-8")

env.Append(CPPPATH=[str(build_dir)])  # noqa: F821

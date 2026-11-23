"""PlatformIO pre-script: embed FW_VERSION from the environment.

Usage:
    FW_VERSION=0.2.0 pio run -e esp32dev

When FW_VERSION is unset the fallback in src/app_config.h is used.
"""

import os

Import("env")  # noqa: F821 - injected by PlatformIO

version = os.environ.get("FW_VERSION", "").strip().lstrip("v")
if version:
    # Escaped quotes survive SCons' command-line parsing so the compiler
    # sees a C string literal: -DFW_VERSION=\"0.1.0\"
    env.Append(CPPDEFINES=[("FW_VERSION", '\\"%s\\"' % version)])

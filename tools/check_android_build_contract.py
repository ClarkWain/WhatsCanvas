#!/usr/bin/env python3
"""Keep Android text/render features from silently diverging from the core defaults."""

from __future__ import annotations

import argparse
import pathlib
import re
import sys


def require_cache_setting(text: str, name: str, value: str) -> None:
    pattern = re.compile(
        rf"set\(\s*{re.escape(name)}\s+{re.escape(value)}\s+"
        rf"CACHE\s+BOOL\s+\"\"\s+FORCE\s*\)",
        re.IGNORECASE | re.MULTILINE,
    )
    if pattern.search(text) is None:
        raise RuntimeError(f"Android CMake must force {name}={value}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True, type=pathlib.Path)
    arguments = parser.parse_args()
    root = arguments.root.resolve()
    cmake_path = root / "platforms/android/app/src/main/cpp/CMakeLists.txt"
    gradle_path = root / "platforms/android/app/build.gradle"
    cmake = cmake_path.read_text(encoding="utf-8")
    gradle = gradle_path.read_text(encoding="utf-8")

    require_cache_setting(cmake, "WHATSCANVAS_BUILD_OPENGLES", "ON")
    require_cache_setting(cmake, "WHATSCANVAS_ENABLE_FREETYPE_RASTERIZER", "ON")
    require_cache_setting(cmake, "WHATSCANVAS_ENABLE_OPENTYPE_SHAPING", "ON")

    abi_match = re.search(r"abiFilters\s+([^\r\n]+)", gradle)
    if abi_match is None:
        raise RuntimeError("Android Gradle config must declare ABI filters")
    abi_line = abi_match.group(1)
    for abi in ("armeabi-v7a", "arm64-v8a", "x86_64"):
        if f"'{abi}'" not in abi_line and f'"{abi}"' not in abi_line:
            raise RuntimeError(f"Android Gradle config is missing ABI {abi}")

    print("ANDROID_BUILD_CONTRACT_RESULT=PASS")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError) as error:
        print(f"ANDROID_BUILD_CONTRACT_RESULT=FAIL: {error}", file=sys.stderr)
        raise SystemExit(1)

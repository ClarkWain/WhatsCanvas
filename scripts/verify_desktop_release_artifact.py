#!/usr/bin/env python3
"""Verify that a desktop install tree matches the current source release."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def source_version() -> str:
    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    match = re.search(
        r"project\s*\(\s*WhatsCanvas\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)",
        cmake,
    )
    require(match is not None, "Unable to read source version from CMakeLists.txt")
    return match.group(1)


def verify(package_dir: Path) -> None:
    expected = source_version()
    config_dir = package_dir / "lib" / "cmake" / "WhatsCanvas"
    version_file = config_dir / "WhatsCanvasConfigVersion.cmake"
    required = (
        package_dir / "include" / "wsc" / "wsc.h",
        config_dir / "WhatsCanvasConfig.cmake",
        version_file,
        package_dir / "share" / "WhatsCanvas" / "LICENSE",
        package_dir / "share" / "WhatsCanvas" / "THIRD_PARTY_NOTICES.md",
        package_dir / "share" / "WhatsCanvas" / "licenses" / "freetype" / "FTL.TXT",
        package_dir / "share" / "WhatsCanvas" / "licenses" / "harfbuzz" / "COPYING",
    )
    missing = [str(path.relative_to(package_dir)) for path in required if not path.is_file()]
    require(not missing, "Desktop package is missing: " + ", ".join(missing))

    version_text = version_file.read_text(encoding="utf-8")
    match = re.search(r'set\(PACKAGE_VERSION "([0-9]+\.[0-9]+\.[0-9]+)"\)', version_text)
    require(match is not None, "Desktop package version file has no PACKAGE_VERSION")
    require(
        match.group(1) == expected,
        f"Desktop package version is {match.group(1)}, expected {expected}",
    )

    library_candidates = [
        path
        for path in (package_dir / "lib").glob("*WhatsCanvas*")
        if path.is_file() and path.suffix.lower() in {".a", ".lib", ".so", ".dylib"}
    ]
    require(library_candidates, "Desktop package contains no WhatsCanvas library")
    print(f"Desktop release artifact verified: {package_dir} ({expected})")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--package-dir", type=Path, required=True)
    args = parser.parse_args()
    verify(args.package_dir.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())


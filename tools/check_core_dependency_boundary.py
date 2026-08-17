#!/usr/bin/env python3
"""Keep external reference-engine details outside the product boundary."""

from __future__ import annotations

import argparse
import pathlib
import re
import sys


FORBIDDEN = re.compile(
    r"\bskia\b|WHATSCANVAS_SKIA|SkFontMgr|SkFont|SkTypeface|SkCanvas|SkPaint",
    re.IGNORECASE,
)
TEXT_SUFFIXES = {
    ".c",
    ".cc",
    ".cpp",
    ".h",
    ".hpp",
    ".inl",
    ".m",
    ".mm",
    ".cmake",
}
GENERATED_DIRECTORIES = {"build", ".gradle", ".cxx"}


def product_files(root: pathlib.Path):
    yield root / "CMakeLists.txt"
    for directory_name in ("include", "src", "platforms"):
        directory = root / directory_name
        for path in directory.rglob("*"):
            relative_parts = path.relative_to(directory).parts
            if any(part in GENERATED_DIRECTORIES for part in relative_parts):
                continue
            if path.is_file() and path.suffix.lower() in TEXT_SUFFIXES:
                yield path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True, type=pathlib.Path)
    arguments = parser.parse_args()
    root = arguments.root.resolve()
    violations: list[str] = []
    for path in product_files(root):
        try:
            lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        except OSError as error:
            violations.append(f"{path}: cannot read: {error}")
            continue
        for line_number, line in enumerate(lines, start=1):
            if FORBIDDEN.search(line):
                violations.append(
                    f"{path.relative_to(root)}:{line_number}: {line.strip()}"
                )
    if violations:
        print("Core dependency boundary violations:", file=sys.stderr)
        for violation in violations:
            print(f"  {violation}", file=sys.stderr)
        return 1
    print("CORE_DEPENDENCY_BOUNDARY_RESULT=PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())

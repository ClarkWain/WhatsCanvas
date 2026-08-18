#!/usr/bin/env python3
"""Verify the pinned Skia/FreeType scanner snapshot for a real font file."""

from __future__ import annotations

import argparse
import json
import pathlib
import subprocess
import sys

from run_android_font_oracle import first_difference, write_snapshot


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", required=True, type=pathlib.Path)
    parser.add_argument("--font", required=True, type=pathlib.Path)
    parser.add_argument("--golden", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    arguments = parser.parse_args()
    if not arguments.probe.is_file() or not arguments.font.is_file():
        raise RuntimeError("scanner probe or real-font fixture does not exist")
    result = subprocess.run(
        [str(arguments.probe), str(arguments.font)],
        check=False,
        capture_output=True,
        encoding="utf-8",
        errors="replace",
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"Skia scanner exited with {result.returncode}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    try:
        actual = json.loads(result.stdout)
        expected = json.loads(arguments.golden.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise RuntimeError(f"invalid scanner snapshot: {error}") from error
    if (
        actual.get("schema") != "whatscanvas.skia-font-scanner.v1"
        or actual.get("engine") != "skia-freetype"
    ):
        raise RuntimeError("scanner probe reported an unexpected schema or engine")
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    write_snapshot(arguments.output, actual)
    difference = first_difference(expected, actual)
    if difference:
        raise RuntimeError(f"Skia scanner golden mismatch at {difference}")
    print("SKIA_FONT_SCANNER_RESULT=PASS font=RobotoFlex-Variable.ttf")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except RuntimeError as error:
        print(f"SKIA_FONT_SCANNER_RESULT=FAIL reason={error}", file=sys.stderr)
        sys.exit(1)

#!/usr/bin/env python3
"""Verify deterministic real-file raster output through pinned Skia."""

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
    parser.add_argument("--config", required=True, type=pathlib.Path)
    parser.add_argument("--font-dir", required=True, type=pathlib.Path)
    parser.add_argument("--golden", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    arguments = parser.parse_args()
    result = subprocess.run(
        [str(arguments.probe), str(arguments.config), str(arguments.font_dir)],
        check=False,
        capture_output=True,
        encoding="utf-8",
        errors="replace",
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"Skia font raster probe exited with {result.returncode}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    try:
        actual = json.loads(result.stdout)
        expected = json.loads(arguments.golden.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise RuntimeError(f"invalid font-raster snapshot: {error}") from error
    if (
        actual.get("schema") != "whatscanvas.skia-font-raster.v1"
        or actual.get("engine") != "skia-android-freetype"
    ):
        raise RuntimeError("font-raster probe reported an unexpected schema or engine")
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    write_snapshot(arguments.output, actual)
    difference = first_difference(expected, actual)
    if difference:
        raise RuntimeError(f"Skia font-raster golden mismatch at {difference}")
    if actual["latin"]["inkPixels"] <= 0 or actual["simplifiedCjk"]["inkPixels"] <= 0:
        raise RuntimeError("outline glyph raster produced no ink")
    if actual["colrEmoji"]["colorPixels"] <= 0:
        raise RuntimeError("COLR emoji raster produced no color pixels")
    if actual["bitmapEmoji"]["colorPixels"] <= 0:
        raise RuntimeError("CBDT emoji raster produced no color pixels")
    print("SKIA_FONT_RASTER_RESULT=PASS")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (KeyError, RuntimeError) as error:
        print(f"SKIA_FONT_RASTER_RESULT=FAIL reason={error}", file=sys.stderr)
        sys.exit(1)

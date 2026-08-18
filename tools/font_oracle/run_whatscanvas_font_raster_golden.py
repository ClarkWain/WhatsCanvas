#!/usr/bin/env python3
"""Verify WhatsCanvas real-file raster output against its own golden."""

from __future__ import annotations

import argparse
import json
import pathlib
import subprocess
import sys

from run_android_font_oracle import first_difference, write_snapshot


def invoke(arguments: argparse.Namespace) -> dict:
    result = subprocess.run(
        [
            str(arguments.probe),
            str(arguments.latin_font),
            str(arguments.cjk_font),
            str(arguments.colr_emoji_font),
            str(arguments.bitmap_emoji_font),
        ],
        check=False,
        capture_output=True,
        encoding="utf-8",
        errors="replace",
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"WhatsCanvas font raster probe exited with {result.returncode}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    try:
        return json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise RuntimeError(f"invalid WhatsCanvas raster snapshot: {error}") from error


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", required=True, type=pathlib.Path)
    parser.add_argument("--latin-font", required=True, type=pathlib.Path)
    parser.add_argument("--cjk-font", required=True, type=pathlib.Path)
    parser.add_argument("--colr-emoji-font", required=True, type=pathlib.Path)
    parser.add_argument("--bitmap-emoji-font", required=True, type=pathlib.Path)
    parser.add_argument("--golden", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    arguments = parser.parse_args()
    actual = invoke(arguments)
    try:
        expected = json.loads(arguments.golden.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise RuntimeError(f"invalid WhatsCanvas raster golden: {error}") from error
    if (
        actual.get("schema") != "whatscanvas.font-raster.v1"
        or actual.get("engine") != "whatscanvas-freetype"
    ):
        raise RuntimeError("WhatsCanvas raster probe reported an unexpected schema or engine")
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    write_snapshot(arguments.output, actual)
    difference = first_difference(expected, actual)
    if difference:
        raise RuntimeError(f"WhatsCanvas font-raster golden mismatch at {difference}")
    if actual["colrEmoji"]["colorPixels"] <= 0:
        raise RuntimeError("COLR emoji produced no color pixels")
    if actual["bitmapEmoji"]["colorPixels"] <= 0:
        raise RuntimeError("CBDT emoji produced no color pixels")
    print("WHATSCANVAS_FONT_RASTER_RESULT=PASS")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (KeyError, RuntimeError) as error:
        print(f"WHATSCANVAS_FONT_RASTER_RESULT=FAIL reason={error}", file=sys.stderr)
        sys.exit(1)

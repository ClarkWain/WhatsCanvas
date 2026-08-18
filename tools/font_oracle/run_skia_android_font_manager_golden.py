#!/usr/bin/env python3
"""Verify real-file matching through Skia's Android font manager."""

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
            f"Skia Android font manager exited with {result.returncode}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    try:
        actual = json.loads(result.stdout)
        expected = json.loads(arguments.golden.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise RuntimeError(f"invalid font-manager snapshot: {error}") from error
    if (
        actual.get("schema") != "whatscanvas.skia-android-font-manager.v1"
        or actual.get("engine") != "skia-android-freetype"
    ):
        raise RuntimeError("font-manager probe reported an unexpected schema or engine")
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    write_snapshot(arguments.output, actual)
    difference = first_difference(expected, actual)
    if difference:
        raise RuntimeError(f"Skia Android font-manager golden mismatch at {difference}")
    print("SKIA_ANDROID_FONT_MANAGER_RESULT=PASS")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except RuntimeError as error:
        print(f"SKIA_ANDROID_FONT_MANAGER_RESULT=FAIL reason={error}", file=sys.stderr)
        sys.exit(1)

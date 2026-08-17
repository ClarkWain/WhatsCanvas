#!/usr/bin/env python3
"""Compare layered raster semantics without requiring cross-engine pixel identity."""

from __future__ import annotations

import argparse
import json
import pathlib
import subprocess
import sys

from run_android_font_oracle import write_snapshot


def run_json(command: list[str], name: str) -> dict:
    result = subprocess.run(
        command,
        check=False,
        capture_output=True,
        encoding="utf-8",
        errors="replace",
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"{name} exited with {result.returncode}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    try:
        return json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise RuntimeError(f"{name} emitted invalid JSON: {error}") from error


def maximum_delta(left: list[float], right: list[float]) -> float:
    if len(left) != len(right):
        return float("inf")
    return max((abs(float(a) - float(b)) for a, b in zip(left, right)), default=0.0)


def compare_supported(name: str, whatscanvas: dict, reference: dict) -> dict:
    if not whatscanvas.get("found") or not reference.get("found"):
        raise RuntimeError(f"{name} must be supported by both rasterizers")
    glyph_equal = whatscanvas.get("glyph") == reference.get("glyph")
    advance_delta = abs(float(whatscanvas["advance"]) - float(reference["advance"]))
    glyph_bounds_delta = maximum_delta(
        whatscanvas["glyphBounds"], reference["glyphBounds"]
    )
    ink_bounds_delta = maximum_delta(whatscanvas["inkBounds"], reference["inkBounds"])
    reference_ink = max(1, int(reference["inkPixels"]))
    ink_ratio = float(whatscanvas["inkPixels"]) / reference_ink
    if not glyph_equal:
        raise RuntimeError(f"{name} selected different glyph IDs")
    if advance_delta > 0.01:
        raise RuntimeError(f"{name} advance delta {advance_delta} exceeds 0.01px")
    if glyph_bounds_delta > 1.0 or ink_bounds_delta > 1.0:
        raise RuntimeError(
            f"{name} bounds diverged: glyph={glyph_bounds_delta}, ink={ink_bounds_delta}"
        )
    if not 0.97 <= ink_ratio <= 1.03:
        raise RuntimeError(f"{name} ink ratio {ink_ratio} is outside [0.97, 1.03]")
    return {
        "classification": "compatible",
        "glyphEqual": glyph_equal,
        "advanceDelta": advance_delta,
        "glyphBoundsMaxDelta": glyph_bounds_delta,
        "inkBoundsMaxDelta": ink_bounds_delta,
        "inkPixelRatio": ink_ratio,
        "pixelHashesEqual": whatscanvas["pixelHash"] == reference["pixelHash"],
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--whatscanvas", required=True, type=pathlib.Path)
    parser.add_argument("--skia", required=True, type=pathlib.Path)
    parser.add_argument("--config", required=True, type=pathlib.Path)
    parser.add_argument("--font-dir", required=True, type=pathlib.Path)
    parser.add_argument("--latin-font", required=True, type=pathlib.Path)
    parser.add_argument("--cjk-font", required=True, type=pathlib.Path)
    parser.add_argument("--colr-emoji-font", required=True, type=pathlib.Path)
    parser.add_argument("--bitmap-emoji-font", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    arguments = parser.parse_args()
    whatscanvas = run_json(
        [
            str(arguments.whatscanvas),
            str(arguments.latin_font),
            str(arguments.cjk_font),
            str(arguments.colr_emoji_font),
            str(arguments.bitmap_emoji_font),
        ],
        "WhatsCanvas raster probe",
    )
    reference = run_json(
        [str(arguments.skia), str(arguments.config), str(arguments.font_dir)],
        "reference raster probe",
    )
    if whatscanvas.get("schema") != "whatscanvas.font-raster.v1":
        raise RuntimeError("unexpected WhatsCanvas raster schema")
    if reference.get("schema") != "whatscanvas.skia-font-raster.v1":
        raise RuntimeError("unexpected reference raster schema")
    if any(whatscanvas[key] != reference[key] for key in ("width", "height", "fontSize")):
        raise RuntimeError("raster scene dimensions differ")

    comparisons = {
        name: compare_supported(name, whatscanvas[name], reference[name])
        for name in ("latin", "simplifiedCjk", "colrEmoji", "bitmapEmoji")
    }
    if whatscanvas["latin"]["colorPixels"] != 0 or reference["latin"]["colorPixels"] != 0:
        raise RuntimeError("Latin outline unexpectedly produced colored pixels")
    if whatscanvas["simplifiedCjk"]["colorPixels"] != 0 or reference["simplifiedCjk"]["colorPixels"] != 0:
        raise RuntimeError("CJK outline unexpectedly produced colored pixels")
    if whatscanvas["colrEmoji"]["colorPixels"] <= 0 or reference["colrEmoji"]["colorPixels"] <= 0:
        raise RuntimeError("COLR emoji must produce colored pixels in both engines")
    if whatscanvas["bitmapEmoji"]["colorPixels"] <= 0 or reference["bitmapEmoji"]["colorPixels"] <= 0:
        raise RuntimeError("CBDT emoji must produce colored pixels in both engines")
    report = {
        "schema": "whatscanvas.font-raster-differential.v1",
        "whatscanvas": whatscanvas,
        "reference": reference,
        "comparisons": comparisons,
    }
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    write_snapshot(arguments.output, report)
    print("FONT_RASTER_DIFFERENTIAL_RESULT=PASS")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (KeyError, RuntimeError) as error:
        print(f"FONT_RASTER_DIFFERENTIAL_RESULT=FAIL reason={error}", file=sys.stderr)
        sys.exit(1)

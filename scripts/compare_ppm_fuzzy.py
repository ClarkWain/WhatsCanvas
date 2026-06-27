#!/usr/bin/env python3
"""Compare two binary P6 PPM files with simple fuzzy thresholds."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path


class PpmError(RuntimeError):
    pass


def _next_token(data: bytes, offset: int) -> tuple[str, int]:
    length = len(data)
    while offset < length:
        byte = data[offset]
        if byte in b" \t\r\n":
            offset += 1
            continue
        if byte == ord("#"):
            while offset < length and data[offset] not in b"\r\n":
                offset += 1
            continue
        break

    if offset >= length:
        raise PpmError("unexpected end of header")

    start = offset
    while offset < length and data[offset] not in b" \t\r\n":
        offset += 1
    return data[start:offset].decode("ascii"), offset


def read_ppm(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    offset = 0
    magic, offset = _next_token(data, offset)
    if magic != "P6":
        raise PpmError(f"{path}: expected P6 magic, got {magic!r}")

    width_text, offset = _next_token(data, offset)
    height_text, offset = _next_token(data, offset)
    max_value_text, offset = _next_token(data, offset)
    try:
        width = int(width_text)
        height = int(height_text)
        max_value = int(max_value_text)
    except ValueError as exc:
        raise PpmError(f"{path}: invalid PPM header") from exc

    if width <= 0 or height <= 0:
        raise PpmError(f"{path}: dimensions must be positive")
    if max_value != 255:
        raise PpmError(f"{path}: only max value 255 is supported")

    if offset >= len(data) or data[offset] not in b" \t\r\n":
        raise PpmError(f"{path}: missing pixel-data separator")
    offset += 1

    expected_size = width * height * 3
    pixels = data[offset:]
    if len(pixels) != expected_size:
        raise PpmError(f"{path}: expected {expected_size} pixel bytes, got {len(pixels)}")
    return width, height, pixels


def compare_pixels(reference: bytes, candidate: bytes) -> tuple[int, float, float]:
    max_delta = 0
    total_delta = 0
    changed_pixels = 0
    pixel_count = len(reference) // 3

    for pixel_index in range(pixel_count):
        base = pixel_index * 3
        pixel_changed = False
        for channel in range(3):
            delta = abs(reference[base + channel] - candidate[base + channel])
            max_delta = max(max_delta, delta)
            total_delta += delta
            pixel_changed = pixel_changed or delta != 0
        if pixel_changed:
            changed_pixels += 1

    mean_delta = total_delta / float(len(reference)) if reference else 0.0
    changed_percent = (changed_pixels * 100.0 / float(pixel_count)) if pixel_count else 0.0
    return max_delta, mean_delta, changed_percent


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--max-channel-delta", type=int, default=3)
    parser.add_argument("--max-mean-delta", type=float, default=0.75)
    parser.add_argument("--max-changed-percent", type=float, default=5.0)
    args = parser.parse_args(argv)

    try:
        ref_width, ref_height, ref_pixels = read_ppm(args.reference)
        cand_width, cand_height, cand_pixels = read_ppm(args.candidate)
        if (ref_width, ref_height) != (cand_width, cand_height):
            raise PpmError(
                f"dimension mismatch: reference={ref_width}x{ref_height}, "
                f"candidate={cand_width}x{cand_height}"
            )
        max_delta, mean_delta, changed_percent = compare_pixels(ref_pixels, cand_pixels)
    except (OSError, PpmError) as exc:
        print("FUZZY_PPM_COMPARE_RESULT=FAIL")
        print(f"FUZZY_PPM_COMPARE_ERROR={exc}")
        return 1

    passed = (
        max_delta <= args.max_channel_delta
        and mean_delta <= args.max_mean_delta
        and changed_percent <= args.max_changed_percent
    )
    print(f"FUZZY_PPM_COMPARE_WIDTH={ref_width}")
    print(f"FUZZY_PPM_COMPARE_HEIGHT={ref_height}")
    print(f"FUZZY_PPM_COMPARE_MAX_CHANNEL_DELTA={max_delta}")
    print(f"FUZZY_PPM_COMPARE_MEAN_DELTA={mean_delta:.6f}")
    print(f"FUZZY_PPM_COMPARE_CHANGED_PERCENT={changed_percent:.6f}")
    print(f"FUZZY_PPM_COMPARE_RESULT={'PASS' if passed else 'FAIL'}")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))

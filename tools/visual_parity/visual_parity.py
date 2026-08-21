#!/usr/bin/env python3
"""Cross-platform visual parity contract validator and image comparator.

The tool intentionally uses only the Python standard library. It reads the
PNG files emitted by mobile hosts and the PPM/PAM files emitted by desktop or
test renderers, normalizes their declared content rectangles to the contract
viewport, and applies region-specific fuzzy pixel thresholds.
"""

from __future__ import annotations

import argparse
import json
import math
import pathlib
import struct
import sys
import zlib
from dataclasses import dataclass
from typing import Any, Iterable


@dataclass(frozen=True)
class Image:
    width: int
    height: int
    rgba: bytes


def _tokens(data: bytes) -> Iterable[bytes]:
    index = 0
    while index < len(data):
        while index < len(data) and data[index] in b" \t\r\n":
            index += 1
        if index < len(data) and data[index] == ord("#"):
            while index < len(data) and data[index] not in b"\r\n":
                index += 1
            continue
        if index >= len(data):
            return
        start = index
        while index < len(data) and data[index] not in b" \t\r\n#":
            index += 1
        yield data[start:index]


def _read_ppm(path: pathlib.Path, data: bytes) -> Image:
    token_iter = iter(_tokens(data))
    header = [next(token_iter, b"") for _ in range(4)]
    if header[0] != b"P6":
        raise ValueError(f"{path}: unsupported PPM encoding")
    if any(not token for token in header[1:]):
        raise ValueError(f"{path}: truncated PPM header")
    try:
        width = int(header[1])
        height = int(header[2])
        maximum = int(header[3])
    except ValueError as error:
        raise ValueError(f"{path}: invalid PPM header") from error
    if width <= 0 or height <= 0 or maximum != 255:
        raise ValueError(f"{path}: invalid PPM header")

    header_tokens = 0
    index = 0
    while header_tokens < 4:
        while index < len(data) and data[index] in b" \t\r\n":
            index += 1
        if index >= len(data):
            raise ValueError(f"{path}: truncated PPM header")
        if data[index] == ord("#"):
            while index < len(data) and data[index] not in b"\r\n":
                index += 1
            if index >= len(data):
                raise ValueError(f"{path}: truncated PPM comment")
            continue
        while index < len(data) and data[index] not in b" \t\r\n#":
            index += 1
        header_tokens += 1
    if index >= len(data) or data[index] not in b" \t\r\n":
        raise ValueError(f"{path}: PPM header terminator is missing")
    if data[index:index + 2] == b"\r\n":
        index += 2
    else:
        index += 1
    rgb = data[index:]
    if len(rgb) != width * height * 3:
        raise ValueError(f"{path}: PPM payload has the wrong size")
    rgba = bytearray(width * height * 4)
    for pixel in range(width * height):
        rgba[pixel * 4:pixel * 4 + 3] = rgb[pixel * 3:pixel * 3 + 3]
        rgba[pixel * 4 + 3] = 255
    return Image(width, height, bytes(rgba))


def _read_pam(path: pathlib.Path, data: bytes) -> Image:
    end = data.find(b"ENDHDR\n")
    if end < 0:
        raise ValueError(f"{path}: PAM ENDHDR is missing")
    header = data[:end].decode("ascii").splitlines()
    values: dict[str, str] = {}
    for line in header[1:]:
        if line and not line.startswith("#"):
            key, value = line.split(maxsplit=1)
            values[key] = value
    width = int(values.get("WIDTH", "0"))
    height = int(values.get("HEIGHT", "0"))
    depth = int(values.get("DEPTH", "0"))
    maximum = int(values.get("MAXVAL", "0"))
    payload = data[end + len(b"ENDHDR\n"):]
    if width <= 0 or height <= 0 or maximum != 255 or depth not in (3, 4):
        raise ValueError(f"{path}: unsupported PAM header")
    if len(payload) != width * height * depth:
        raise ValueError(f"{path}: PAM payload has the wrong size")
    if depth == 4:
        return Image(width, height, payload)
    rgba = bytearray(width * height * 4)
    for pixel in range(width * height):
        rgba[pixel * 4:pixel * 4 + 3] = payload[pixel * 3:pixel * 3 + 3]
        rgba[pixel * 4 + 3] = 255
    return Image(width, height, bytes(rgba))


def _paeth(left: int, up: int, upper_left: int) -> int:
    prediction = left + up - upper_left
    left_distance = abs(prediction - left)
    up_distance = abs(prediction - up)
    upper_left_distance = abs(prediction - upper_left)
    if left_distance <= up_distance and left_distance <= upper_left_distance:
        return left
    return up if up_distance <= upper_left_distance else upper_left


def _read_png(path: pathlib.Path, data: bytes) -> Image:
    if not data.startswith(b"\x89PNG\r\n\x1a\n"):
        raise ValueError(f"{path}: invalid PNG signature")
    index = 8
    compressed = bytearray()
    width = height = bit_depth = color_type = interlace = 0
    while index + 12 <= len(data):
        length = struct.unpack(">I", data[index:index + 4])[0]
        chunk_type = data[index + 4:index + 8]
        payload = data[index + 8:index + 8 + length]
        index += length + 12
        if chunk_type == b"IHDR":
            width, height, bit_depth, color_type, _, _, interlace = struct.unpack(
                ">IIBBBBB", payload
            )
        elif chunk_type == b"IDAT":
            compressed.extend(payload)
        elif chunk_type == b"IEND":
            break
    channels = {0: 1, 2: 3, 4: 2, 6: 4}.get(color_type)
    if width <= 0 or height <= 0 or bit_depth != 8 or channels is None or interlace:
        raise ValueError(f"{path}: only non-interlaced 8-bit gray/RGB/RGBA PNG is supported")
    raw = zlib.decompress(bytes(compressed))
    stride = width * channels
    if len(raw) != height * (stride + 1):
        raise ValueError(f"{path}: PNG payload has the wrong size")
    rows: list[bytearray] = []
    offset = 0
    previous = bytearray(stride)
    for _ in range(height):
        filter_type = raw[offset]
        source = raw[offset + 1:offset + 1 + stride]
        offset += stride + 1
        row = bytearray(stride)
        for byte_index, value in enumerate(source):
            left = row[byte_index - channels] if byte_index >= channels else 0
            up = previous[byte_index]
            upper_left = previous[byte_index - channels] if byte_index >= channels else 0
            if filter_type == 0:
                predictor = 0
            elif filter_type == 1:
                predictor = left
            elif filter_type == 2:
                predictor = up
            elif filter_type == 3:
                predictor = (left + up) // 2
            elif filter_type == 4:
                predictor = _paeth(left, up, upper_left)
            else:
                raise ValueError(f"{path}: unknown PNG filter {filter_type}")
            row[byte_index] = (value + predictor) & 0xFF
        rows.append(row)
        previous = row
    rgba = bytearray(width * height * 4)
    for y, row in enumerate(rows):
        for x in range(width):
            source = x * channels
            target = (y * width + x) * 4
            if color_type == 0:
                rgba[target:target + 3] = bytes((row[source],)) * 3
                rgba[target + 3] = 255
            elif color_type == 2:
                rgba[target:target + 3] = row[source:source + 3]
                rgba[target + 3] = 255
            elif color_type == 4:
                rgba[target:target + 3] = bytes((row[source],)) * 3
                rgba[target + 3] = row[source + 1]
            else:
                rgba[target:target + 4] = row[source:source + 4]
    return Image(width, height, bytes(rgba))


def read_image(path: pathlib.Path) -> Image:
    data = path.read_bytes()
    if data.startswith(b"\x89PNG"):
        return _read_png(path, data)
    if data.startswith(b"P6"):
        return _read_ppm(path, data)
    if data.startswith(b"P7"):
        return _read_pam(path, data)
    raise ValueError(f"{path}: supported formats are PNG, binary PPM and PAM")


def crop(image: Image, rect: list[int]) -> Image:
    if len(rect) != 4:
        raise ValueError("content_rect_pixels must contain x, y, width and height")
    x, y, width, height = rect
    if width <= 0 or height <= 0 or x < 0 or y < 0 \
            or x + width > image.width or y + height > image.height:
        raise ValueError("content_rect_pixels is outside the image")
    rgba = bytearray(width * height * 4)
    for row in range(height):
        source = ((y + row) * image.width + x) * 4
        target = row * width * 4
        rgba[target:target + width * 4] = image.rgba[source:source + width * 4]
    return Image(width, height, bytes(rgba))


def resize_bilinear(image: Image, width: int, height: int) -> Image:
    if image.width == width and image.height == height:
        return image
    rgba = bytearray(width * height * 4)
    for y in range(height):
        source_y = (y + 0.5) * image.height / height - 0.5
        y0 = max(0, min(image.height - 1, math.floor(source_y)))
        y1 = min(image.height - 1, y0 + 1)
        fy = max(0.0, source_y - y0)
        for x in range(width):
            source_x = (x + 0.5) * image.width / width - 0.5
            x0 = max(0, min(image.width - 1, math.floor(source_x)))
            x1 = min(image.width - 1, x0 + 1)
            fx = max(0.0, source_x - x0)
            target = (y * width + x) * 4
            for channel in range(4):
                top = image.rgba[(y0 * image.width + x0) * 4 + channel] * (1.0 - fx) \
                    + image.rgba[(y0 * image.width + x1) * 4 + channel] * fx
                bottom = image.rgba[(y1 * image.width + x0) * 4 + channel] * (1.0 - fx) \
                    + image.rgba[(y1 * image.width + x1) * 4 + channel] * fx
                rgba[target + channel] = round(top * (1.0 - fy) + bottom * fy)
    return Image(width, height, bytes(rgba))


def load_contract(path: pathlib.Path) -> dict[str, Any]:
    contract = json.loads(path.read_text(encoding="utf-8"))
    errors = validate_contract(contract)
    if errors:
        raise ValueError("invalid visual parity contract:\n  " + "\n  ".join(errors))
    return contract


def validate_contract(contract: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    if contract.get("schema_version") != 1:
        errors.append("schema_version must be 1")
    profiles = contract.get("profiles")
    if not isinstance(profiles, dict) or not profiles:
        errors.append("profiles must be a non-empty object")
        profiles = {}
    for profile_id, profile in profiles.items():
        if not isinstance(profile, dict):
            errors.append(f"profile {profile_id} must be an object")
            continue
        search_radius = profile.get("search_radius")
        if (not isinstance(search_radius, int) or isinstance(search_radius, bool)
                or search_radius not in range(0, 5)):
            errors.append(f"profile {profile_id} search_radius must be between 0 and 4")
        for key in ("bad_channel_threshold", "max_mean_delta", "max_bad_pixel_ratio"):
            if key not in profile:
                errors.append(f"profile {profile_id} is missing {key}")
    scenes = contract.get("scenes")
    if not isinstance(scenes, list) or not scenes:
        errors.append("scenes must be a non-empty array")
        return errors
    standards = contract.get("viewport_standards", {})
    if not isinstance(standards, dict):
        errors.append("viewport_standards must be an object")
        standards = {}
    primary_standards = 0
    for standard_id, standard in standards.items():
        if not standard_id or not isinstance(standard, dict):
            errors.append("viewport standard id is empty or invalid")
            continue
        role = standard.get("role")
        if role not in {
                "primary_pixel_gate", "layout_conformance",
                "legacy_regression_only"}:
            errors.append(f"viewport standard {standard_id} has an invalid role")
        primary_standards += role == "primary_pixel_gate"
        portrait = standard.get("portrait", [])
        landscape = standard.get("landscape", [])
        for orientation, dimensions in (("portrait", portrait),
                                        ("landscape", landscape)):
            if not isinstance(dimensions, list) or len(dimensions) != 2 or any(
                    not isinstance(value, int) or isinstance(value, bool) or value <= 0
                    for value in dimensions):
                errors.append(
                    f"viewport standard {standard_id}/{orientation} has invalid dimensions")
        if role != "legacy_regression_only" \
                and isinstance(portrait, list) and len(portrait) == 2 \
                and isinstance(landscape, list) and len(landscape) == 2 \
                and portrait != [landscape[1], landscape[0]]:
            errors.append(f"viewport standard {standard_id} must be rotation-symmetric")
    if standards and primary_standards != 1:
        errors.append("viewport_standards must define exactly one primary pixel gate")

    scene_ids: set[str] = set()
    for scene in scenes:
        scene_id = scene.get("id", "")
        if not scene_id or scene_id in scene_ids:
            errors.append(f"scene id is empty or duplicated: {scene_id!r}")
        scene_ids.add(scene_id)
        platforms = scene.get("required_platforms", [])
        if sorted(platforms) != ["android", "desktop", "ios", "web"]:
            errors.append(
                f"scene {scene_id} must require android, ios, desktop and web")
        samples = scene.get("samples", [])
        sample_ids = [sample.get("id") for sample in samples]
        if not samples or len(sample_ids) != len(set(sample_ids)):
            errors.append(f"scene {scene_id} samples must be non-empty and unique")
        times = [sample.get("time_seconds") for sample in samples]
        if any(not isinstance(value, (int, float)) or value < 0 for value in times):
            errors.append(f"scene {scene_id} sample times must be non-negative numbers")
        standard_id = scene.get("viewport_standard")
        if standards and standard_id not in standards:
            errors.append(f"scene {scene_id} uses an unknown viewport standard")
        viewport_ids: set[str] = set()
        for viewport in scene.get("viewports", []):
            viewport_id = viewport.get("id", "")
            if not viewport_id or viewport_id in viewport_ids:
                errors.append(f"scene {scene_id} viewport id is empty or duplicated")
            viewport_ids.add(viewport_id)
            if int(viewport.get("width", 0)) <= 0 or int(viewport.get("height", 0)) <= 0:
                errors.append(f"scene {scene_id}/{viewport_id} has invalid dimensions")
            region_ids: set[str] = set()
            for region in viewport.get("regions", []):
                region_id = region.get("id", "")
                rect = region.get("rect", [])
                if not region_id or region_id in region_ids:
                    errors.append(f"scene {scene_id}/{viewport_id} region id is empty or duplicated")
                region_ids.add(region_id)
                if region.get("profile") not in profiles:
                    errors.append(f"scene {scene_id}/{viewport_id}/{region_id} uses an unknown profile")
                if len(rect) != 4 or any(not isinstance(value, (int, float)) for value in rect):
                    errors.append(f"scene {scene_id}/{viewport_id}/{region_id} has an invalid rect")
                elif rect[0] < 0 or rect[1] < 0 or rect[2] <= 0 or rect[3] <= 0 \
                        or rect[0] + rect[2] > 1.000001 or rect[1] + rect[3] > 1.000001:
                    errors.append(f"scene {scene_id}/{viewport_id}/{region_id} rect is out of bounds")
        if viewport_ids != {"landscape", "portrait"}:
            errors.append(f"scene {scene_id} must define landscape and portrait viewports")
        if standard_id in standards:
            expected = standards[standard_id]
            for viewport in scene.get("viewports", []):
                orientation = viewport.get("id")
                if orientation in ("portrait", "landscape"):
                    dimensions = [viewport.get("width"), viewport.get("height")]
                    if dimensions != expected.get(orientation):
                        errors.append(
                            f"scene {scene_id}/{orientation} does not match "
                            f"viewport standard {standard_id}")
    return errors


def find_scene(contract: dict[str, Any], scene_id: str) -> dict[str, Any]:
    for scene in contract["scenes"]:
        if scene["id"] == scene_id:
            return scene
    raise ValueError(f"unknown scene {scene_id}")


def find_viewport(scene: dict[str, Any], viewport_id: str) -> dict[str, Any]:
    for viewport in scene["viewports"]:
        if viewport["id"] == viewport_id:
            return viewport
    raise ValueError(f"unknown viewport {scene['id']}/{viewport_id}")


def _normalized_image(path: pathlib.Path, metadata_path: pathlib.Path | None,
                      scene_id: str, viewport_id: str, sample_id: str,
                      width: int, height: int) -> Image:
    image = read_image(path)
    if metadata_path is not None:
        metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
        for key, expected in (("scene_id", scene_id), ("viewport_id", viewport_id),
                              ("sample_id", sample_id)):
            if metadata.get(key) != expected:
                raise ValueError(f"{metadata_path}: {key} does not match {expected}")
        image = crop(image, metadata["content_rect_pixels"])
    expected_aspect = width / height
    actual_aspect = image.width / image.height
    if abs(actual_aspect / expected_aspect - 1.0) > 0.01:
        raise ValueError(
            f"{path}: content aspect {actual_aspect:.6f} does not match "
            f"{scene_id}/{viewport_id} ({expected_aspect:.6f}); provide capture metadata"
        )
    return resize_bilinear(image, width, height)


def compare_region(reference: Image, actual: Image, rect: list[float],
                   profile: dict[str, Any]) -> dict[str, Any]:
    x0 = max(0, min(reference.width - 1, math.floor(rect[0] * reference.width)))
    y0 = max(0, min(reference.height - 1, math.floor(rect[1] * reference.height)))
    x1 = max(x0 + 1, min(reference.width, math.ceil((rect[0] + rect[2]) * reference.width)))
    y1 = max(y0 + 1, min(reference.height, math.ceil((rect[1] + rect[3]) * reference.height)))
    radius = int(profile["search_radius"])
    threshold = int(profile["bad_channel_threshold"])
    total_delta = 0
    maximum_delta = 0
    bad_pixels = 0
    pixels = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            actual_offset = (y * actual.width + x) * 4
            actual_rgb = actual.rgba[actual_offset:actual_offset + 3]
            best_sum = 1 << 30
            best_max = 255
            for reference_y in range(max(y0, y - radius), min(y1, y + radius + 1)):
                for reference_x in range(max(x0, x - radius), min(x1, x + radius + 1)):
                    reference_offset = (reference_y * reference.width + reference_x) * 4
                    deltas = [abs(actual_rgb[channel] - reference.rgba[reference_offset + channel])
                              for channel in range(3)]
                    delta_sum = sum(deltas)
                    if delta_sum < best_sum:
                        best_sum = delta_sum
                        best_max = max(deltas)
            total_delta += best_sum
            maximum_delta = max(maximum_delta, best_max)
            bad_pixels += 1 if best_max > threshold else 0
            pixels += 1
    mean_delta = total_delta / (pixels * 3)
    bad_ratio = bad_pixels / pixels
    passed = mean_delta <= float(profile["max_mean_delta"]) \
        and bad_ratio <= float(profile["max_bad_pixel_ratio"])
    return {
        "status": "PASS" if passed else "FAIL",
        "pixels": pixels,
        "max_channel_delta": maximum_delta,
        "mean_channel_delta": round(mean_delta, 6),
        "bad_pixels": bad_pixels,
        "bad_pixel_ratio": round(bad_ratio, 6),
    }


def write_diff_pam(path: pathlib.Path, reference: Image, actual: Image) -> None:
    if reference.width != actual.width or reference.height != actual.height:
        raise ValueError("cannot write a diff for images with different dimensions")
    rgba = bytearray(reference.width * reference.height * 4)
    for pixel in range(reference.width * reference.height):
        offset = pixel * 4
        delta = max(abs(reference.rgba[offset + channel] - actual.rgba[offset + channel])
                    for channel in range(3))
        rgba[offset:offset + 4] = bytes((delta, 0, 255 - delta, 255))
    path.parent.mkdir(parents=True, exist_ok=True)
    header = (f"P7\nWIDTH {reference.width}\nHEIGHT {reference.height}\nDEPTH 4\n"
              "MAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n").encode("ascii")
    path.write_bytes(header + rgba)


def build_comparison_report(contract: dict[str, Any], scene_id: str,
                            viewport_id: str, sample_id: str,
                            reference_path: pathlib.Path, actual_path: pathlib.Path,
                            reference_metadata: pathlib.Path | None = None,
                            actual_metadata: pathlib.Path | None = None,
                            diff_path: pathlib.Path | None = None) -> dict[str, Any]:
    scene = find_scene(contract, scene_id)
    if sample_id not in {sample["id"] for sample in scene["samples"]}:
        raise ValueError(f"unknown sample {scene_id}/{sample_id}")
    viewport = find_viewport(scene, viewport_id)
    width = int(viewport["width"])
    height = int(viewport["height"])
    reference = _normalized_image(reference_path, reference_metadata,
                                  scene_id, viewport_id, sample_id, width, height)
    actual = _normalized_image(actual_path, actual_metadata,
                               scene_id, viewport_id, sample_id, width, height)
    if diff_path is not None:
        write_diff_pam(diff_path, reference, actual)
    regions = []
    passed = True
    for region in viewport["regions"]:
        result = compare_region(reference, actual, region["rect"],
                                contract["profiles"][region["profile"]])
        result.update({"id": region["id"], "profile": region["profile"]})
        regions.append(result)
        passed = passed and result["status"] == "PASS"
    return {
        "schema_version": 1,
        "status": "PASS" if passed else "FAIL",
        "scene_id": scene_id,
        "viewport_id": viewport_id,
        "sample_id": sample_id,
        "reference": str(reference_path),
        "actual": str(actual_path),
        "normalized_size": [width, height],
        "regions": regions,
    }


def compare_command(args: argparse.Namespace) -> int:
    contract = load_contract(args.contract)
    report = build_comparison_report(
        contract, args.scene, args.viewport, args.sample,
        args.reference, args.actual, args.reference_metadata,
        args.actual_metadata, args.diff)
    passed = report["status"] == "PASS"
    rendered = json.dumps(report, indent=2, sort_keys=True)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(rendered + "\n", encoding="utf-8")
    print(rendered)
    return 0 if passed else 1


def _capture_path(root: pathlib.Path, platform: str, scene_id: str,
                  viewport_id: str, sample_id: str) -> pathlib.Path:
    stem = root / platform / scene_id / viewport_id / sample_id
    for suffix in (".png", ".pam", ".ppm"):
        candidate = stem.with_suffix(suffix)
        if candidate.is_file():
            return candidate
    raise ValueError(f"missing capture {platform}/{scene_id}/{viewport_id}/{sample_id}")


def matrix_command(args: argparse.Namespace) -> int:
    contract = load_contract(args.contract)
    reports: list[dict[str, Any]] = []
    passed = True
    for scene in contract["scenes"]:
        scene_id = scene["id"]
        platforms = scene["required_platforms"]
        if args.reference_platform not in platforms:
            raise ValueError(
                f"reference platform {args.reference_platform} is not required by {scene_id}"
            )
        for viewport in scene["viewports"]:
            viewport_id = viewport["id"]
            for sample in scene["samples"]:
                sample_id = sample["id"]
                reference = _capture_path(
                    args.captures, args.reference_platform, scene_id, viewport_id, sample_id)
                reference_metadata = reference.with_suffix(".json")
                if not reference_metadata.is_file():
                    reference_metadata = None
                for platform in platforms:
                    if platform == args.reference_platform:
                        continue
                    actual = _capture_path(
                        args.captures, platform, scene_id, viewport_id, sample_id)
                    actual_metadata = actual.with_suffix(".json")
                    if not actual_metadata.is_file():
                        actual_metadata = None
                    report_stem = args.output / scene_id / viewport_id / sample_id \
                        / f"{platform}-vs-{args.reference_platform}"
                    report = build_comparison_report(
                        contract, scene_id, viewport_id, sample_id,
                        reference, actual, reference_metadata, actual_metadata,
                        report_stem.with_suffix(".pam"))
                    report.update({
                        "reference_platform": args.reference_platform,
                        "actual_platform": platform,
                    })
                    report_stem.parent.mkdir(parents=True, exist_ok=True)
                    report_stem.with_suffix(".json").write_text(
                        json.dumps(report, indent=2, sort_keys=True) + "\n",
                        encoding="utf-8")
                    reports.append(report)
                    passed = passed and report["status"] == "PASS"
    summary = {
        "schema_version": 1,
        "status": "PASS" if passed else "FAIL",
        "comparisons": len(reports),
        "failed": sum(report["status"] != "PASS" for report in reports),
    }
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0 if passed else 1


def validate_command(args: argparse.Namespace) -> int:
    contract = json.loads(args.contract.read_text(encoding="utf-8"))
    errors = validate_contract(contract)
    if errors:
        for error in errors:
            print(f"VISUAL_PARITY_CONTRACT status=FAIL reason={error}", file=sys.stderr)
        return 1
    scene_count = len(contract["scenes"])
    sample_count = sum(len(scene["samples"]) for scene in contract["scenes"])
    print(f"VISUAL_PARITY_CONTRACT status=PASS scenes={scene_count} samples={sample_count}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    validate_parser = subparsers.add_parser("validate", help="validate a scene contract")
    validate_parser.add_argument("--contract", type=pathlib.Path, required=True)
    validate_parser.set_defaults(handler=validate_command)
    compare_parser = subparsers.add_parser("compare", help="compare two captures")
    compare_parser.add_argument("--contract", type=pathlib.Path, required=True)
    compare_parser.add_argument("--scene", required=True)
    compare_parser.add_argument("--viewport", required=True)
    compare_parser.add_argument("--sample", required=True)
    compare_parser.add_argument("--reference", type=pathlib.Path, required=True)
    compare_parser.add_argument("--actual", type=pathlib.Path, required=True)
    compare_parser.add_argument("--reference-metadata", type=pathlib.Path)
    compare_parser.add_argument("--actual-metadata", type=pathlib.Path)
    compare_parser.add_argument("--report", type=pathlib.Path)
    compare_parser.add_argument("--diff", type=pathlib.Path,
                                help="write a normalized heat-map as PAM")
    compare_parser.set_defaults(handler=compare_command)
    matrix_parser = subparsers.add_parser(
        "matrix", help="compare every required platform capture in the contract")
    matrix_parser.add_argument("--contract", type=pathlib.Path, required=True)
    matrix_parser.add_argument("--captures", type=pathlib.Path, required=True)
    matrix_parser.add_argument("--output", type=pathlib.Path, required=True)
    matrix_parser.add_argument("--reference-platform", default="desktop")
    matrix_parser.set_defaults(handler=matrix_command)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        return int(args.handler(args))
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(f"VISUAL_PARITY status=FAIL reason={error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())

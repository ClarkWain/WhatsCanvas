#!/usr/bin/env python3
"""Run external 2D adapters and gate timing on comparable rendered output."""

from __future__ import annotations

import argparse
import json
import math
import os
import shlex
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any


class BenchmarkError(RuntimeError):
    pass


@dataclass(frozen=True)
class Adapter:
    label: str
    command: tuple[str, ...]


@dataclass(frozen=True)
class PpmImage:
    width: int
    height: int
    pixels: bytes


@dataclass(frozen=True)
class QualityMetrics:
    mean_absolute_error: float
    root_mean_square_error: float
    max_channel_delta: int
    changed_pixel_fraction: float


@dataclass
class AdapterResult:
    adapter: Adapter
    scene: str
    metadata: dict[str, Any]
    result: dict[str, Any]
    capture: Path


def parse_adapter(value: str) -> Adapter:
    label, separator, command = value.partition("=")
    if not separator or not label.strip() or not command.strip():
        raise argparse.ArgumentTypeError(
            "adapter must use LABEL=COMMAND syntax"
        )
    parts = tuple(shlex.split(command, posix=os.name != "nt"))
    if os.name == "nt":
        parts = tuple(
            part[1:-1]
            if len(part) >= 2
            and part[0] == part[-1]
            and part[0] in ("'", '"')
            else part
            for part in parts
        )
    if not parts:
        raise argparse.ArgumentTypeError("adapter command is empty")
    return Adapter(label.strip(), parts)


def _ppm_token(data: bytes, offset: int) -> tuple[bytes, int]:
    size = len(data)
    while offset < size:
        if data[offset] == ord("#"):
            newline = data.find(b"\n", offset)
            offset = size if newline < 0 else newline + 1
        elif chr(data[offset]).isspace():
            offset += 1
        else:
            break
    start = offset
    while offset < size and not chr(data[offset]).isspace():
        offset += 1
    if start == offset:
        raise BenchmarkError("unexpected end of PPM header")
    return data[start:offset], offset


def read_ppm(path: Path) -> PpmImage:
    data = path.read_bytes()
    magic, offset = _ppm_token(data, 0)
    width_token, offset = _ppm_token(data, offset)
    height_token, offset = _ppm_token(data, offset)
    max_token, offset = _ppm_token(data, offset)
    if magic != b"P6":
        raise BenchmarkError(f"{path}: only binary P6 PPM is supported")
    width = int(width_token)
    height = int(height_token)
    maximum = int(max_token)
    if width <= 0 or height <= 0 or maximum != 255:
        raise BenchmarkError(f"{path}: unsupported PPM dimensions/range")
    if offset >= len(data) or not chr(data[offset]).isspace():
        raise BenchmarkError(f"{path}: PPM header is missing its data separator")
    if data[offset : offset + 2] == b"\r\n":
        offset += 2
    else:
        offset += 1
    expected = width * height * 3
    pixels = data[offset:]
    if len(pixels) != expected:
        raise BenchmarkError(
            f"{path}: expected {expected} RGB bytes, found {len(pixels)}"
        )
    return PpmImage(width, height, pixels)


def compare_images(
    reference: PpmImage, candidate: PpmImage, channel_threshold: int
) -> QualityMetrics:
    if (
        reference.width != candidate.width
        or reference.height != candidate.height
    ):
        raise BenchmarkError("capture dimensions do not match")
    absolute_total = 0
    squared_total = 0
    max_delta = 0
    changed_pixels = 0
    pixel_count = reference.width * reference.height
    for pixel in range(pixel_count):
        changed = False
        base = pixel * 3
        for channel in range(3):
            delta = abs(
                reference.pixels[base + channel]
                - candidate.pixels[base + channel]
            )
            absolute_total += delta
            squared_total += delta * delta
            max_delta = max(max_delta, delta)
            changed = changed or delta > channel_threshold
        changed_pixels += int(changed)
    channel_count = pixel_count * 3
    return QualityMetrics(
        mean_absolute_error=absolute_total / channel_count,
        root_mean_square_error=math.sqrt(squared_total / channel_count),
        max_channel_delta=max_delta,
        changed_pixel_fraction=changed_pixels / pixel_count,
    )


def quality_passes(
    metrics: QualityMetrics, thresholds: dict[str, Any]
) -> bool:
    return (
        metrics.mean_absolute_error
        <= float(thresholds["max_mean_absolute_error"])
        and metrics.root_mean_square_error
        <= float(thresholds["max_root_mean_square_error"])
        and metrics.changed_pixel_fraction
        <= float(thresholds["max_changed_pixel_fraction"])
    )


def load_jsonl(path: Path) -> tuple[dict[str, Any], dict[str, Any]]:
    metadata: dict[str, Any] | None = None
    result: dict[str, Any] | None = None
    for line_number, raw in enumerate(
        path.read_text(encoding="utf-8").splitlines(), 1
    ):
        line = raw.strip()
        if not line:
            continue
        for prefix in ("PERF_METADATA ", "PERF_RESULT "):
            if line.startswith(prefix):
                line = line[len(prefix) :]
                break
        try:
            record = json.loads(line)
        except json.JSONDecodeError as error:
            raise BenchmarkError(
                f"{path}:{line_number}: invalid JSON: {error.msg}"
            ) from error
        if record.get("type") == "metadata":
            metadata = record
        elif record.get("type") == "result":
            if result is not None:
                raise BenchmarkError(f"{path}: expected one scene result")
            result = record
    if metadata is None or result is None:
        raise BenchmarkError(f"{path}: metadata/result record missing")
    for field in (
        "backend",
        "library",
        "library_version",
        "synchronization",
        "cross_library_contract",
        "build_type",
        "profile",
        "width",
        "height",
        "frames",
        "warmup",
    ):
        if field not in metadata:
            raise BenchmarkError(f"{path}: metadata field {field!r} missing")
    if metadata["synchronization"] != "gpu_complete":
        raise BenchmarkError(
            f"{path}: synchronization must be 'gpu_complete'"
        )
    if str(metadata["build_type"]).lower() == "debug":
        raise BenchmarkError(f"{path}: Debug builds are not comparable")
    for field in ("scene", "total_median_ms", "total_p95_ms", "pixel_hash"):
        if field not in result:
            raise BenchmarkError(f"{path}: result field {field!r} missing")
    return metadata, result


def run_adapter(
    adapter: Adapter,
    scene: str,
    profile: str,
    width: int,
    height: int,
    contract_path: Path,
    contract_version: str,
    output_dir: Path,
    timeout: int,
) -> AdapterResult:
    adapter_dir = output_dir / adapter.label
    capture_dir = adapter_dir / "captures"
    adapter_dir.mkdir(parents=True, exist_ok=True)
    capture_dir.mkdir(parents=True, exist_ok=True)
    result_path = adapter_dir / f"{scene}.jsonl"
    command = [
        *adapter.command,
        "--profile",
        profile,
        "--scene",
        scene,
        "--width",
        str(width),
        "--height",
        str(height),
        "--output",
        str(result_path),
        "--capture-dir",
        str(capture_dir),
    ]
    environment = os.environ.copy()
    environment["WHATSCANVAS_CROSS_LIBRARY_CONTRACT"] = str(
        contract_path.resolve()
    )
    completed = subprocess.run(
        command,
        cwd=Path.cwd(),
        env=environment,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=timeout,
        check=False,
    )
    if completed.returncode != 0:
        raise BenchmarkError(
            f"{adapter.label}/{scene} failed ({completed.returncode}):\n"
            f"{completed.stdout}"
        )
    metadata, result = load_jsonl(result_path)
    if metadata["cross_library_contract"] != contract_version:
        raise BenchmarkError(
            f"{result_path}: expected contract {contract_version!r}, got "
            f"{metadata['cross_library_contract']!r}"
        )
    if result["scene"] != scene:
        raise BenchmarkError(
            f"{result_path}: expected scene {scene!r}, got {result['scene']!r}"
        )
    if (
        metadata["profile"] != profile
        or metadata["width"] != width
        or metadata["height"] != height
    ):
        raise BenchmarkError(f"{result_path}: run settings do not match")
    capture = capture_dir / f"{metadata['backend']}_{scene}.ppm"
    if not capture.is_file():
        raise BenchmarkError(f"capture not found: {capture}")
    return AdapterResult(adapter, scene, metadata, result, capture)


def markdown_report(
    reference_label: str,
    rows: list[tuple[AdapterResult, QualityMetrics, bool, float]],
) -> str:
    lines = [
        "# Cross-Library 2D Benchmark",
        "",
        f"Reference renderer: `{reference_label}`. Timing is reported only "
        "alongside the quality gate.",
        "",
        "| Adapter | Scene | Median | Relative | MAE | RMSE | Changed pixels | Quality |",
        "| --- | --- | ---: | ---: | ---: | ---: | ---: | --- |",
    ]
    for result, metrics, passed, relative in rows:
        lines.append(
            f"| {result.adapter.label} | `{result.scene}` | "
            f"{float(result.result['total_median_ms']):.3f} ms | "
            f"{relative:.2f}x | {metrics.mean_absolute_error:.3f} | "
            f"{metrics.root_mean_square_error:.3f} | "
            f"{metrics.changed_pixel_fraction * 100.0:.3f}% | "
            f"{'PASS' if passed else 'FAIL'} |"
        )
    lines.extend(
        [
            "",
            "Relative is candidate median divided by reference median; lower "
            "is faster. A quality failure invalidates the timing comparison.",
            "",
        ]
    )
    return "\n".join(lines)


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--contract",
        default=str(root / "benchmarks/cross_library/contract.json"),
    )
    parser.add_argument("--reference", required=True, type=parse_adapter)
    parser.add_argument(
        "--adapter", action="append", default=[], type=parse_adapter
    )
    parser.add_argument("--scene", action="append", default=[])
    parser.add_argument(
        "--profile", choices=("quick", "standard", "thorough"), default="standard"
    )
    parser.add_argument("--width", type=int)
    parser.add_argument("--height", type=int)
    parser.add_argument(
        "--output-dir", default="build/cross-library-results"
    )
    parser.add_argument("--report")
    parser.add_argument("--timeout", type=int, default=300)
    args = parser.parse_args()

    contract_path = Path(args.contract)
    contract = json.loads(contract_path.read_text(encoding="utf-8"))
    scenes = args.scene or list(contract["scenes"].keys())
    unknown = [scene for scene in scenes if scene not in contract["scenes"]]
    if unknown:
        raise BenchmarkError(f"unknown contract scenes: {', '.join(unknown)}")
    width = args.width or int(contract["default_width"])
    height = args.height or int(contract["default_height"])
    if width <= 0 or height <= 0:
        raise BenchmarkError("benchmark dimensions must be positive")
    if args.timeout <= 0:
        raise BenchmarkError("timeout must be positive")
    output_dir = Path(args.output_dir)
    adapters = [args.reference, *args.adapter]
    if len({adapter.label for adapter in adapters}) != len(adapters):
        raise BenchmarkError("adapter labels must be unique")

    rows: list[tuple[AdapterResult, QualityMetrics, bool, float]] = []
    all_passed = True
    for scene in scenes:
        reference = run_adapter(
            args.reference, scene, args.profile, width, height,
            contract_path, str(contract["version"]), output_dir, args.timeout
        )
        reference_image = read_ppm(reference.capture)
        reference_median = float(reference.result["total_median_ms"])
        zero = QualityMetrics(0.0, 0.0, 0, 0.0)
        rows.append((reference, zero, True, 1.0))
        quality = contract["scenes"][scene]["quality"]
        for adapter in args.adapter:
            candidate = run_adapter(
                adapter, scene, args.profile, width, height,
                contract_path, str(contract["version"]), output_dir, args.timeout
            )
            for sample_field in ("frames", "warmup"):
                if candidate.metadata[sample_field] != reference.metadata[sample_field]:
                    raise BenchmarkError(
                        f"{adapter.label}/{scene}: {sample_field} differs from "
                        "the reference"
                    )
            metrics = compare_images(
                reference_image,
                read_ppm(candidate.capture),
                int(quality["channel_threshold"]),
            )
            passed = quality_passes(metrics, quality)
            all_passed = all_passed and passed
            relative = (
                float(candidate.result["total_median_ms"])
                / reference_median
            )
            rows.append((candidate, metrics, passed, relative))

    report = markdown_report(args.reference.label, rows)
    report_path = (
        Path(args.report)
        if args.report
        else output_dir / "cross-library-report.md"
    )
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(report, encoding="utf-8")
    print(report)
    print(f"Report: {report_path}")
    return 0 if all_passed else 2


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (BenchmarkError, OSError, subprocess.SubprocessError) as error:
        print(f"CROSS_LIBRARY_ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)

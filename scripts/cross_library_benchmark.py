#!/usr/bin/env python3
"""Run quality-gated, ABBA-balanced cross-library 2D benchmarks."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import random
import shlex
import statistics
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable


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
    run_id: str


@dataclass
class ReportRow:
    label: str
    scene: str
    process_samples_ms: list[float]
    median_ci_ms: tuple[float, float]
    relative_samples: list[float]
    relative_ci: tuple[float, float]
    quality: QualityMetrics
    reference_signal: QualityMetrics | None
    quality_passed: bool


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
    offset += 2 if data[offset : offset + 2] == b"\r\n" else 1
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


def worst_quality(metrics: Iterable[QualityMetrics]) -> QualityMetrics:
    values = list(metrics)
    if not values:
        return QualityMetrics(0.0, 0.0, 0, 0.0)
    return QualityMetrics(
        max(value.mean_absolute_error for value in values),
        max(value.root_mean_square_error for value in values),
        max(value.max_channel_delta for value in values),
        max(value.changed_pixel_fraction for value in values),
    )


def solid_image(width: int, height: int, rgb: Iterable[int]) -> PpmImage:
    channels = tuple(int(value) for value in rgb)
    if (
        width <= 0
        or height <= 0
        or len(channels) != 3
        or any(value < 0 or value > 255 for value in channels)
    ):
        raise BenchmarkError("invalid reference background color")
    return PpmImage(width, height, bytes(channels) * (width * height))


def quality_passes(
    metrics: QualityMetrics,
    thresholds: dict[str, Any],
    reference_signal: QualityMetrics | None = None,
    candidate_signal: QualityMetrics | None = None,
) -> bool:
    absolute_fields = (
        ("max_mean_absolute_error", metrics.mean_absolute_error),
        ("max_root_mean_square_error", metrics.root_mean_square_error),
        ("max_changed_pixel_fraction", metrics.changed_pixel_fraction),
    )
    if any(
        value > float(thresholds[limit])
        for limit, value in absolute_fields
        if limit in thresholds
    ):
        return False

    relative_fields = (
        (
            "max_mean_absolute_error_fraction_of_reference_signal",
            metrics.mean_absolute_error,
            "mean_absolute_error",
        ),
        (
            "max_root_mean_square_error_fraction_of_reference_signal",
            metrics.root_mean_square_error,
            "root_mean_square_error",
        ),
        (
            "max_changed_pixel_fraction_of_reference_signal",
            metrics.changed_pixel_fraction,
            "changed_pixel_fraction",
        ),
    )
    relative_limits = [
        item for item in relative_fields if item[0] in thresholds
    ]
    if relative_limits and reference_signal is None:
        raise BenchmarkError(
            "relative quality limits require a reference signal"
        )
    for limit, value, signal_field in relative_limits:
        signal = float(getattr(reference_signal, signal_field))
        if signal <= 0.0 or value > float(thresholds[limit]) * signal:
            return False

    candidate_signal_fields = (
        (
            "candidate_mean_absolute_error_fraction_of_reference_signal",
            "mean_absolute_error",
        ),
        (
            "candidate_root_mean_square_error_fraction_of_reference_signal",
            "root_mean_square_error",
        ),
        (
            "candidate_changed_pixel_fraction_of_reference_signal",
            "changed_pixel_fraction",
        ),
    )
    for name, signal_field in candidate_signal_fields:
        minimum = f"min_{name}"
        maximum = f"max_{name}"
        if minimum not in thresholds and maximum not in thresholds:
            continue
        if reference_signal is None or candidate_signal is None:
            raise BenchmarkError(
                "candidate signal limits require reference and candidate "
                "signals"
            )
        reference_value = float(getattr(reference_signal, signal_field))
        candidate_value = float(getattr(candidate_signal, signal_field))
        if reference_value <= 0.0:
            return False
        fraction = candidate_value / reference_value
        if minimum in thresholds and fraction < float(thresholds[minimum]):
            return False
        if maximum in thresholds and fraction > float(thresholds[maximum]):
            return False
    return True


def percentile(sorted_values: list[float], ratio: float) -> float:
    if not sorted_values:
        return 0.0
    position = ratio * (len(sorted_values) - 1)
    lower = int(position)
    upper = min(lower + 1, len(sorted_values) - 1)
    fraction = position - lower
    return (
        sorted_values[lower] * (1.0 - fraction)
        + sorted_values[upper] * fraction
    )


def bootstrap_median_ci(
    samples: list[float], iterations: int = 10000, seed: int = 1
) -> tuple[float, float]:
    if not samples:
        raise BenchmarkError("cannot bootstrap an empty sample")
    if len(samples) == 1:
        return samples[0], samples[0]
    generator = random.Random(seed)
    estimates = []
    for _ in range(iterations):
        resample = [
            samples[generator.randrange(len(samples))]
            for _ in samples
        ]
        estimates.append(float(statistics.median(resample)))
    estimates.sort()
    return percentile(estimates, 0.025), percentile(estimates, 0.975)


def abba_schedule(
    reference: Adapter, candidate: Adapter, repetitions: int
) -> list[tuple[str, Adapter]]:
    if repetitions < 2 or repetitions % 2 != 0:
        raise BenchmarkError("repetitions must be an even number of at least 2")
    schedule: list[tuple[str, Adapter]] = []
    for block in range(repetitions // 2):
        prefix = f"block-{block + 1:02d}"
        schedule.extend(
            (
                (f"{prefix}-01-a", reference),
                (f"{prefix}-02-b", candidate),
                (f"{prefix}-03-b", candidate),
                (f"{prefix}-04-a", reference),
            )
        )
    return schedule


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
        "clear_semantics",
        "font_sha256",
        "text_shaping_mode",
        "text_raster_mode",
        "workload_mode",
        "workload_seed",
        "workload_operations",
        "workload_texture_count",
        "workload_rounded_ratio",
        "workload_state_change_rate",
        "workload_text_length",
    ):
        if field not in metadata:
            raise BenchmarkError(f"{path}: metadata field {field!r} missing")
    if metadata["synchronization"] != "gpu_complete":
        raise BenchmarkError(
            f"{path}: synchronization must be 'gpu_complete'"
        )
    if str(metadata["build_type"]).lower() == "debug":
        raise BenchmarkError(f"{path}: Debug builds are not comparable")
    for field in (
        "scene",
        "total_median_ms",
        "total_p95_ms",
        "pixel_hash",
        "record_samples_ms",
        "submit_samples_ms",
        "total_samples_ms",
    ):
        if field not in result:
            raise BenchmarkError(f"{path}: result field {field!r} missing")
    frame_count = int(metadata["frames"])
    for field in (
        "record_samples_ms",
        "submit_samples_ms",
        "total_samples_ms",
    ):
        samples = result[field]
        if not isinstance(samples, list) or len(samples) != frame_count:
            raise BenchmarkError(
                f"{path}: {field} must contain exactly {frame_count} samples"
            )
        if not all(
            isinstance(value, (int, float))
            and math.isfinite(float(value))
            and float(value) >= 0.0
            for value in samples
        ):
            raise BenchmarkError(f"{path}: {field} contains invalid samples")
    return metadata, result


def run_adapter(
    adapter: Adapter,
    scene: str,
    profile: str,
    width: int,
    height: int,
    frames: int,
    warmup: int,
    contract_path: Path,
    contract: dict[str, Any],
    output_dir: Path,
    run_id: str,
    timeout: int,
    workload_arguments: list[str],
    font_sha256: str,
) -> AdapterResult:
    adapter_dir = output_dir / "runs" / scene / adapter.label / run_id
    capture_dir = adapter_dir / "captures"
    adapter_dir.mkdir(parents=True, exist_ok=True)
    capture_dir.mkdir(parents=True, exist_ok=True)
    result_path = adapter_dir / "result.jsonl"
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
        "--frames",
        str(frames),
        "--warmup",
        str(warmup),
        *workload_arguments,
        "--output",
        str(result_path),
        "--capture-dir",
        str(capture_dir),
    ]
    environment = os.environ.copy()
    environment["WHATSCANVAS_CROSS_LIBRARY_CONTRACT"] = str(
        contract_path.resolve()
    )
    environment["WHATSCANVAS_CROSS_LIBRARY_CONTRACT_VERSION"] = str(
        contract["version"]
    )
    environment["WHATSCANVAS_CROSS_LIBRARY_FONT_SHA256"] = font_sha256
    environment["WHATSCANVAS_CROSS_LIBRARY_FONT_PATH"] = str(
        (
            contract_path.parent
            / contract["assets"]["font_regular"]
        ).resolve()
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
            f"{adapter.label}/{scene}/{run_id} failed "
            f"({completed.returncode}):\n{completed.stdout}"
        )
    metadata, result = load_jsonl(result_path)
    expected_metadata = {
        "cross_library_contract": str(contract["version"]),
        "profile": profile,
        "width": width,
        "height": height,
        "frames": frames,
        "warmup": warmup,
        "clear_semantics": contract["rendering"]["clear_semantics"],
        "font_sha256": font_sha256,
        "text_shaping_mode": contract["text"]["shaping_mode"],
        "text_raster_mode": contract["text"]["raster_mode"],
    }
    for field, expected in expected_metadata.items():
        if metadata[field] != expected:
            raise BenchmarkError(
                f"{result_path}: expected {field}={expected!r}, got "
                f"{metadata[field]!r}"
            )
    if result["scene"] != scene:
        raise BenchmarkError(
            f"{result_path}: expected scene {scene!r}, got {result['scene']!r}"
        )
    capture = capture_dir / f"{metadata['backend']}_{scene}.ppm"
    if not capture.is_file():
        raise BenchmarkError(f"capture not found: {capture}")
    return AdapterResult(
        adapter, scene, metadata, result, capture, run_id
    )


def _block_relative_samples(
    runs: list[AdapterResult], reference_label: str
) -> list[float]:
    ratios: list[float] = []
    for offset in range(0, len(runs), 4):
        block = runs[offset : offset + 4]
        if len(block) != 4:
            raise BenchmarkError("incomplete ABBA block")
        references = [
            float(run.result["total_median_ms"])
            for run in block
            if run.adapter.label == reference_label
        ]
        candidates = [
            float(run.result["total_median_ms"])
            for run in block
            if run.adapter.label != reference_label
        ]
        if len(references) != 2 or len(candidates) != 2:
            raise BenchmarkError("invalid ABBA block ordering")
        reference_geomean = math.sqrt(references[0] * references[1])
        candidate_geomean = math.sqrt(candidates[0] * candidates[1])
        ratios.append(candidate_geomean / reference_geomean)
    return ratios


def markdown_report(
    reference_label: str, rows: list[ReportRow], repetitions: int
) -> str:
    block_count = repetitions // 2
    block_label = "block" if block_count == 1 else "blocks"
    lines = [
        "# Cross-Library 2D Benchmark",
        "",
        f"Reference renderer: `{reference_label}`. Each candidate uses "
        f"{block_count} independent ABBA {block_label} "
        f"({repetitions} fresh processes per renderer).",
        "",
        "| Adapter | Scene | Process median (95% CI) | Relative (95% CI) | MAE | RMSE | Changed pixels | Quality |",
        "| --- | --- | ---: | ---: | ---: | ---: | ---: | --- |",
    ]
    for row in rows:
        median = float(statistics.median(row.process_samples_ms))
        relative = (
            float(statistics.median(row.relative_samples))
            if row.relative_samples else 1.0
        )
        mae = f"{row.quality.mean_absolute_error:.3f}"
        rmse = f"{row.quality.root_mean_square_error:.3f}"
        if row.reference_signal is not None:
            if row.reference_signal.mean_absolute_error > 0.0:
                mae += (
                    " / "
                    f"{row.quality.mean_absolute_error / row.reference_signal.mean_absolute_error:.3f}x"
                )
            if row.reference_signal.root_mean_square_error > 0.0:
                rmse += (
                    " / "
                    f"{row.quality.root_mean_square_error / row.reference_signal.root_mean_square_error:.3f}x"
                )
        lines.append(
            f"| {row.label} | `{row.scene}` | {median:.3f} ms "
            f"[{row.median_ci_ms[0]:.3f}, {row.median_ci_ms[1]:.3f}] | "
            f"{relative:.3f}x "
            f"[{row.relative_ci[0]:.3f}, {row.relative_ci[1]:.3f}] | "
            f"{mae} | "
            f"{rmse} | "
            f"{row.quality.changed_pixel_fraction * 100.0:.3f}% | "
            f"{'PASS' if row.quality_passed else 'FAIL'} |"
        )
    lines.extend(
        (
            "",
            "Relative is the candidate/reference geometric-mean ratio inside "
            "each ABBA block; lower is faster. Confidence intervals use a "
            "deterministic bootstrap over fresh-process samples. Every JSONL "
            "run retains all measured frame samples.",
            "",
            "For parameterized scenes with a declared reference background, "
            "MAE and RMSE also show error/reference-signal ratios. A blank "
            "renderer has a ratio of 1.0 and zero candidate signal, so it "
            "fails the combined gate.",
            "",
            "A quality failure invalidates the timing comparison.",
            "",
        )
    )
    return "\n".join(lines)


def workload_arguments(args: argparse.Namespace) -> list[str]:
    customized = (
        args.workload != "fixed"
        or args.operations is not None
        or args.seed != 1
        or args.texture_count is not None
        or args.rounded_ratio is not None
        or args.state_change_rate is not None
        or args.text_length is not None
    )
    if not customized:
        return []
    mode = args.workload
    if mode == "fixed":
        mode = "stable"
    values = [
        "--workload", mode,
        "--seed", str(args.seed),
    ]
    if args.operations is not None:
        values.extend(("--operations", str(args.operations)))
    if args.texture_count is not None:
        values.extend(("--texture-count", str(args.texture_count)))
    if args.rounded_ratio is not None:
        values.extend(("--rounded-ratio", str(args.rounded_ratio)))
    if args.state_change_rate is not None:
        values.extend(("--state-change-rate", str(args.state_change_rate)))
    if args.text_length is not None:
        values.extend(("--text-length", str(args.text_length)))
    return values


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
        "--profile", choices=("quick", "standard", "thorough"),
        default="standard",
    )
    parser.add_argument("--width", type=int)
    parser.add_argument("--height", type=int)
    parser.add_argument("--frames", type=int)
    parser.add_argument("--warmup", type=int)
    parser.add_argument("--repetitions", type=int, default=8)
    parser.add_argument("--bootstrap-samples", type=int, default=10000)
    parser.add_argument(
        "--workload",
        choices=("fixed", "stable", "dynamic-data", "dynamic-structure"),
        default="fixed",
    )
    parser.add_argument("--operations", type=int)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--texture-count", type=int)
    parser.add_argument("--rounded-ratio", type=float)
    parser.add_argument("--state-change-rate", type=float)
    parser.add_argument("--text-length", type=int)
    parser.add_argument(
        "--output-dir", default="build/cross-library-results"
    )
    parser.add_argument("--report")
    parser.add_argument("--summary")
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
    frames = args.frames or int(contract["timing"]["frames"])
    warmup = (
        args.warmup
        if args.warmup is not None
        else int(contract["timing"]["warmup"])
    )
    if min(width, height, frames) <= 0 or warmup < 0:
        raise BenchmarkError("dimensions/frames must be positive")
    if args.timeout <= 0 or args.bootstrap_samples <= 0:
        raise BenchmarkError("timeout/bootstrap-samples must be positive")
    if args.repetitions < 2 or args.repetitions % 2 != 0:
        raise BenchmarkError("repetitions must be an even number of at least 2")
    output_dir = Path(args.output_dir)
    adapters = [args.reference, *args.adapter]
    if len({adapter.label for adapter in adapters}) != len(adapters):
        raise BenchmarkError("adapter labels must be unique")
    if not args.adapter:
        raise BenchmarkError("at least one --adapter is required")

    font_path = (
        contract_path.parent / contract["assets"]["font_regular"]
    ).resolve()
    font_sha256 = hashlib.sha256(font_path.read_bytes()).hexdigest().upper()
    expected_font_sha256 = str(
        contract["assets"]["font_sha256"]
    ).upper()
    if font_sha256 != expected_font_sha256:
        raise BenchmarkError(
            f"font SHA-256 mismatch: expected {expected_font_sha256}, "
            f"got {font_sha256}"
        )

    workload = workload_arguments(args)
    rows: list[ReportRow] = []
    summary_comparisons: list[dict[str, Any]] = []
    all_passed = True
    for scene_index, scene in enumerate(scenes):
        reference_process_samples: list[float] = []
        reference_runs_for_scene: list[AdapterResult] = []
        for candidate_index, candidate in enumerate(args.adapter):
            runs: list[AdapterResult] = []
            schedule = abba_schedule(
                args.reference, candidate, args.repetitions
            )
            pair_dir = output_dir / f"pair-{candidate.label}"
            for run_id, adapter in schedule:
                run = run_adapter(
                    adapter, scene, args.profile, width, height,
                    frames, warmup, contract_path, contract, pair_dir,
                    run_id, args.timeout, workload, font_sha256,
                )
                runs.append(run)
                if adapter.label == args.reference.label:
                    reference_runs_for_scene.append(run)
                    reference_process_samples.append(
                        float(run.result["total_median_ms"])
                    )

            reference_image = read_ppm(
                next(
                    run.capture for run in runs
                    if run.adapter.label == args.reference.label
                )
            )
            reference_hashes = {
                run.result["pixel_hash"] for run in runs
                if run.adapter.label == args.reference.label
            }
            if len(reference_hashes) != 1:
                raise BenchmarkError(
                    f"{scene}: reference validation frame is nondeterministic"
                )
            reference_metadata = next(
                run.metadata for run in runs
                if run.adapter.label == args.reference.label
            )
            for run in runs:
                for field in (
                    "workload_mode",
                    "workload_seed",
                    "workload_operations",
                    "workload_texture_count",
                    "workload_rounded_ratio",
                    "workload_state_change_rate",
                    "workload_text_length",
                ):
                    if run.metadata[field] != reference_metadata[field]:
                        raise BenchmarkError(
                            f"{run.adapter.label}/{scene}: {field} differs "
                            "from the reference"
                        )
            scene_contract = contract["scenes"][scene]
            quality_contract = (
                scene_contract.get(
                    "parameterized_quality",
                    scene_contract["quality"],
                )
                if workload else scene_contract["quality"]
            )
            quality_values = [
                compare_images(
                    reference_image, read_ppm(run.capture),
                    int(quality_contract["channel_threshold"]),
                )
                for run in runs
                if run.adapter.label == candidate.label
            ]
            candidate_hashes = {
                run.result["pixel_hash"] for run in runs
                if run.adapter.label == candidate.label
            }
            if len(candidate_hashes) != 1:
                raise BenchmarkError(
                    f"{scene}: {candidate.label} validation frame is "
                    "nondeterministic"
                )
            quality = worst_quality(quality_values)
            reference_signal = None
            candidate_signal = None
            if "reference_background_rgb" in quality_contract:
                background = solid_image(
                    reference_image.width,
                    reference_image.height,
                    quality_contract["reference_background_rgb"],
                )
                reference_signal = compare_images(
                    reference_image,
                    background,
                    int(quality_contract["channel_threshold"]),
                )
                candidate_image = read_ppm(
                    next(
                        run.capture for run in runs
                        if run.adapter.label == candidate.label
                    )
                )
                candidate_signal = compare_images(
                    candidate_image,
                    background,
                    int(quality_contract["channel_threshold"]),
                )
            passed = quality_passes(
                quality,
                quality_contract,
                reference_signal,
                candidate_signal,
            )
            all_passed = all_passed and passed
            candidate_samples = [
                float(run.result["total_median_ms"])
                for run in runs
                if run.adapter.label == candidate.label
            ]
            relative_samples = _block_relative_samples(
                runs, args.reference.label
            )
            seed_base = (
                args.seed + scene_index * 1009 + candidate_index * 9176
            )
            row = ReportRow(
                candidate.label,
                scene,
                candidate_samples,
                bootstrap_median_ci(
                    candidate_samples, args.bootstrap_samples, seed_base
                ),
                relative_samples,
                bootstrap_median_ci(
                    relative_samples, args.bootstrap_samples, seed_base + 1
                ),
                quality,
                reference_signal,
                passed,
            )
            rows.append(row)
            summary_comparisons.append(
                {
                    "scene": scene,
                    "reference": args.reference.label,
                    "candidate": candidate.label,
                    "schedule": [run.run_id for run in runs],
                    "reference_process_medians_ms": [
                        float(run.result["total_median_ms"])
                        for run in runs
                        if run.adapter.label == args.reference.label
                    ],
                    "candidate_process_medians_ms": candidate_samples,
                    "abba_relative_samples": relative_samples,
                    "candidate_median_ci_ms": list(row.median_ci_ms),
                    "relative_ci": list(row.relative_ci),
                    "quality": {
                        **quality.__dict__,
                        **(
                            {
                                "reference_signal":
                                    reference_signal.__dict__,
                                "mean_absolute_error_fraction_of_reference_signal":
                                    quality.mean_absolute_error
                                    / reference_signal.mean_absolute_error,
                                "root_mean_square_error_fraction_of_reference_signal":
                                    quality.root_mean_square_error
                                    / reference_signal.root_mean_square_error,
                                "changed_pixel_fraction_of_reference_signal":
                                    quality.changed_pixel_fraction
                                    / reference_signal.changed_pixel_fraction,
                                "candidate_signal":
                                    candidate_signal.__dict__,
                                "candidate_mean_absolute_error_fraction_of_reference_signal":
                                    candidate_signal.mean_absolute_error
                                    / reference_signal.mean_absolute_error,
                                "candidate_root_mean_square_error_fraction_of_reference_signal":
                                    candidate_signal.root_mean_square_error
                                    / reference_signal.root_mean_square_error,
                                "candidate_changed_pixel_fraction_of_reference_signal":
                                    candidate_signal.changed_pixel_fraction
                                    / reference_signal.changed_pixel_fraction,
                            }
                            if (
                                reference_signal is not None
                                and candidate_signal is not None
                            )
                            else {}
                        ),
                        "passed": passed,
                    },
                    "run_files": [
                        (
                            run.capture.parent.parent / "result.jsonl"
                        ).relative_to(output_dir).as_posix()
                        for run in runs
                    ],
                }
            )

        reference_ci = bootstrap_median_ci(
            reference_process_samples,
            args.bootstrap_samples,
            args.seed + scene_index * 1009 + 31,
        )
        rows.insert(
            len(rows) - len(args.adapter),
            ReportRow(
                args.reference.label,
                scene,
                reference_process_samples,
                reference_ci,
                [],
                (1.0, 1.0),
                QualityMetrics(0.0, 0.0, 0, 0.0),
                None,
                True,
            ),
        )

    report = markdown_report(
        args.reference.label, rows, args.repetitions
    )
    report_path = (
        Path(args.report)
        if args.report
        else output_dir / "cross-library-report.md"
    )
    summary_path = (
        Path(args.summary)
        if args.summary
        else output_dir / "cross-library-summary.json"
    )
    report_path.parent.mkdir(parents=True, exist_ok=True)
    summary_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(report, encoding="utf-8")
    summary_path.write_text(
        json.dumps(
            {
                "schema": 2,
                "contract": contract["version"],
                "reference": args.reference.label,
                "profile": args.profile,
                "width": width,
                "height": height,
                "frames": frames,
                "warmup": warmup,
                "repetitions_per_renderer": args.repetitions,
                "bootstrap_samples": args.bootstrap_samples,
                "font_sha256": font_sha256,
                "workload_arguments": workload,
                "comparisons": summary_comparisons,
            },
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )
    print(report)
    print(f"Report: {report_path}")
    print(f"Summary: {summary_path}")
    return 0 if all_passed else 2


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (BenchmarkError, OSError, subprocess.SubprocessError) as error:
        print(f"CROSS_LIBRARY_ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)

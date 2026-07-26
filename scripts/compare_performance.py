#!/usr/bin/env python3
"""Validate and compare WhatsCanvas performance-suite JSONL results."""

from __future__ import annotations

import argparse
import json
import math
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable


REQUIRED_METADATA = {
    "type",
    "schema",
    "suite",
    "version",
    "backend",
    "device",
    "device_vendor",
    "driver",
    "os",
    "architecture",
    "cpu",
    "compiler",
    "compiler_version",
    "build_type",
    "profile",
    "width",
    "height",
    "frames",
    "warmup",
    "hardware_threads",
}

REQUIRED_RESULT = {
    "type",
    "schema",
    "backend",
    "scene",
    "category",
    "cache_mode",
    "width",
    "height",
    "frames",
    "warmup",
    "cold_total_ms",
    "record_median_ms",
    "submit_median_ms",
    "total_median_ms",
    "total_p95_ms",
    "fps",
    "operations_per_second",
    "readback_ms",
    "pixel_hash",
    "rss_after_bytes",
    "peak_rss_bytes",
}

SCENE_ORDER = {
    name: index
    for index, name in enumerate(
        (
            "solid_rects",
            "rounded_ui",
            "path_cached",
            "path_churn",
            "image_grid",
            "clip_layers",
            "shadow_grid",
            "text_cached",
            "text_churn",
            "frosted_glass",
            "inner_shadow",
        )
    )
}


@dataclass
class Run:
    source: Path
    metadata: dict[str, Any]
    results: list[dict[str, Any]]


class ResultError(RuntimeError):
    pass


def result_files(paths: Iterable[str]) -> list[Path]:
    files: list[Path] = []
    for raw in paths:
        path = Path(raw)
        if path.is_dir():
            files.extend(sorted(path.rglob("*.jsonl")))
        elif path.is_file():
            files.append(path)
        else:
            raise ResultError(f"result path does not exist: {path}")
    if not files:
        raise ResultError("no JSONL result files found")
    return files


def parse_json_line(line: str, source: Path, line_number: int) -> dict[str, Any]:
    stripped = line.strip()
    if not stripped:
        return {}
    for prefix in ("PERF_METADATA ", "PERF_RESULT "):
        if stripped.startswith(prefix):
            stripped = stripped[len(prefix) :]
            break
    try:
        value = json.loads(stripped)
    except json.JSONDecodeError as error:
        raise ResultError(
            f"{source}:{line_number}: invalid JSON: {error.msg}"
        ) from error
    if not isinstance(value, dict):
        raise ResultError(f"{source}:{line_number}: record must be an object")
    return value


def require_fields(
    record: dict[str, Any], required: set[str], source: Path, line_number: int
) -> None:
    missing = sorted(required - record.keys())
    if missing:
        raise ResultError(
            f"{source}:{line_number}: missing fields: {', '.join(missing)}"
        )


def load_run(path: Path) -> Run:
    metadata: dict[str, Any] | None = None
    results: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8") as handle:
        for line_number, line in enumerate(handle, 1):
            record = parse_json_line(line, path, line_number)
            if not record:
                continue
            kind = record.get("type")
            if kind == "metadata":
                require_fields(record, REQUIRED_METADATA, path, line_number)
                if metadata is not None:
                    raise ResultError(
                        f"{path}:{line_number}: duplicate metadata record"
                    )
                metadata = record
            elif kind == "result":
                require_fields(record, REQUIRED_RESULT, path, line_number)
                results.append(record)
            else:
                raise ResultError(
                    f"{path}:{line_number}: unsupported record type {kind!r}"
                )
    if metadata is None:
        raise ResultError(f"{path}: metadata record is missing")
    if not results:
        raise ResultError(f"{path}: no result records")
    for result in results:
        if result["schema"] != metadata["schema"]:
            raise ResultError(f"{path}: metadata/result schema mismatch")
        if result["backend"] != metadata["backend"]:
            raise ResultError(f"{path}: metadata/result backend mismatch")
        if result["width"] != metadata["width"] or result["height"] != metadata["height"]:
            raise ResultError(f"{path}: metadata/result dimensions mismatch")
        for field in ("total_median_ms", "total_p95_ms", "fps"):
            value = result[field]
            if not isinstance(value, (int, float)) or not math.isfinite(value):
                raise ResultError(f"{path}: {field} must be finite")
            if value < 0:
                raise ResultError(f"{path}: {field} must not be negative")
    return Run(path, metadata, results)


def load_runs(paths: Iterable[str]) -> list[Run]:
    return [load_run(path) for path in result_files(paths)]


def run_key(run: Run, result: dict[str, Any]) -> tuple[Any, ...]:
    return (
        result["backend"],
        result["scene"],
        result["width"],
        result["height"],
        run.metadata["profile"],
    )


def index_results(runs: list[Run]) -> dict[tuple[Any, ...], tuple[Run, dict[str, Any]]]:
    indexed: dict[tuple[Any, ...], tuple[Run, dict[str, Any]]] = {}
    for run in runs:
        for result in run.results:
            key = run_key(run, result)
            if key in indexed:
                raise ResultError(f"duplicate result key: {key}")
            indexed[key] = (run, result)
    return indexed


def percent_change(before: float, after: float) -> float:
    if before == 0:
        return 0.0 if after == 0 else math.inf
    return (after - before) * 100.0 / before


def format_delta(delta: float) -> str:
    if math.isinf(delta):
        return "+inf"
    return f"{delta:+.1f}%"


def compatibility_notes(baseline: Run, candidate: Run) -> list[str]:
    notes: list[str] = []
    fields = (
        "backend",
        "device",
        "device_vendor",
        "driver",
        "os",
        "architecture",
        "cpu",
        "compiler",
        "compiler_version",
        "build_type",
        "profile",
        "width",
        "height",
        "frames",
        "warmup",
        "hardware_threads",
    )
    for field in fields:
        if baseline.metadata.get(field) != candidate.metadata.get(field):
            notes.append(
                f"{field}: {baseline.metadata.get(field)!r} vs "
                f"{candidate.metadata.get(field)!r}"
            )
    return notes


def environment_label(run: Run) -> str:
    metadata = run.metadata
    revision = str(metadata.get("commit") or "unknown")[:12]
    return (
        f"{metadata.get('suite')} {metadata.get('version')} @ {revision}; "
        f"{metadata.get('cpu')}; "
        f"{metadata.get('device')} ({metadata.get('driver')}); "
        f"{metadata.get('build_type')}, {metadata.get('profile')}, "
        f"{metadata.get('width')}x{metadata.get('height')}, "
        f"{metadata.get('frames')}+{metadata.get('warmup')} frames"
    )


def make_summary(runs: list[Run]) -> str:
    indexed = index_results(runs)
    lines = [
        "# WhatsCanvas Performance Summary",
        "",
        "This is a reproducible single-machine reference, not a cross-library "
        "ranking. Compare timing only against runs with matching environment "
        "metadata.",
        "",
        "## Environments",
        "",
        "| Backend | Environment |",
        "|---|---|",
    ]
    by_backend: dict[str, Run] = {}
    for run in runs:
        by_backend.setdefault(str(run.metadata["backend"]), run)
    for backend, run in sorted(by_backend.items()):
        lines.append(f"| {backend} | {environment_label(run)} |")

    non_release = sorted(
        {
            str(run.metadata["build_type"])
            for run in runs
            if run.metadata["build_type"] != "Release"
        }
    )
    if non_release:
        lines.extend(
            [
                "",
                "> Warning: this report contains non-Release results: "
                + ", ".join(non_release),
            ]
        )

    lines.extend(
        [
            "",
            "## Results",
            "",
            "| Backend | Scene | Median | p95 | FPS | Cold | Record | Submit | "
            "Peak RSS | Pixel hash |",
            "|---|---|---:|---:|---:|---:|---:|---:|---:|---|",
        ]
    )
    ordered = sorted(
        indexed.values(),
        key=lambda item: (
            str(item[1]["backend"]),
            SCENE_ORDER.get(str(item[1]["scene"]), len(SCENE_ORDER)),
            str(item[1]["scene"]),
        ),
    )
    for _, result in ordered:
        lines.append(
            f"| {result['backend']} | {result['scene']} | "
            f"{result['total_median_ms']:.3f} ms | "
            f"{result['total_p95_ms']:.3f} ms | "
            f"{result['fps']:.1f} | {result['cold_total_ms']:.3f} ms | "
            f"{result['record_median_ms']:.3f} ms | "
            f"{result['submit_median_ms']:.3f} ms | "
            f"{result['peak_rss_bytes'] / (1024 * 1024):.1f} MiB | "
            f"`{result['pixel_hash']}` |"
        )
    lines.extend(
        [
            "",
            "Frame timing is synchronized; readback and hashing are excluded. "
            "Pixel hashes validate deterministic output but do not score visual quality.",
            "",
        ]
    )
    return "\n".join(lines)


def make_report(
    baseline_runs: list[Run],
    candidate_runs: list[Run],
    threshold: float,
) -> tuple[str, list[tuple[str, float]], bool, bool]:
    baseline = index_results(baseline_runs)
    candidate = index_results(candidate_runs)
    common = sorted(set(baseline) & set(candidate))
    if not common:
        raise ResultError("baseline and candidate have no matching scenes")

    lines = [
        "# WhatsCanvas Performance Comparison",
        "",
        "## Environments",
        "",
        "| Backend | Baseline | Candidate |",
        "|---|---|---|",
    ]
    for backend in sorted({key[0] for key in common}):
        key = next(key for key in common if key[0] == backend)
        lines.append(
            f"| {backend} | {environment_label(baseline[key][0])} | "
            f"{environment_label(candidate[key][0])} |"
        )
    lines.extend(
        [
            "",
            "## Results",
            "",
        f"Regression threshold: **{threshold:.1f}%** (positive time delta is slower).",
        "",
        ]
    )
    incompatible = False
    compatibility: list[str] = []
    compared_configurations: set[tuple[Any, ...]] = set()
    for key in common:
        configuration = (key[0], key[2], key[3], key[4])
        if configuration in compared_configurations:
            continue
        compared_configurations.add(configuration)
        base_run = baseline[key][0]
        next_run = candidate[key][0]
        notes = compatibility_notes(base_run, next_run)
        if notes:
            incompatible = True
            label = (
                f"{key[0]} {key[2]}x{key[3]} {key[4]}"
            )
            compatibility.extend(
                f"{label}: {note}"
                for note in notes
            )
    if compatibility:
        lines.extend(
            [
                "> Warning: environment mismatch makes timing deltas non-comparable.",
                "> " + "; ".join(compatibility),
                "",
            ]
        )

    lines.extend(
        [
            "| Backend | Scene | Baseline median | Candidate median | Delta | "
            "p95 delta | Peak RSS delta | Pixels | Status |",
            "|---|---|---:|---:|---:|---:|---:|---|---|",
        ]
    )
    regressions: list[tuple[str, float]] = []
    hash_changed = False
    for key in common:
        base_result = baseline[key][1]
        next_result = candidate[key][1]
        median_delta = percent_change(
            float(base_result["total_median_ms"]),
            float(next_result["total_median_ms"]),
        )
        p95_delta = percent_change(
            float(base_result["total_p95_ms"]),
            float(next_result["total_p95_ms"]),
        )
        rss_delta = percent_change(
            float(base_result["peak_rss_bytes"]),
            float(next_result["peak_rss_bytes"]),
        )
        same_hash = base_result["pixel_hash"] == next_result["pixel_hash"]
        hash_changed = hash_changed or not same_hash
        status = "REGRESSION" if median_delta > threshold else "ok"
        if status == "REGRESSION":
            regressions.append(
                (f"{base_result['backend']}/{base_result['scene']}", median_delta)
            )
        lines.append(
            f"| {base_result['backend']} | {base_result['scene']} | "
            f"{base_result['total_median_ms']:.3f} ms | "
            f"{next_result['total_median_ms']:.3f} ms | "
            f"{format_delta(median_delta)} | {format_delta(p95_delta)} | "
            f"{format_delta(rss_delta)} | {'same' if same_hash else 'CHANGED'} | "
            f"{status} |"
        )

    missing_candidate = sorted(set(baseline) - set(candidate))
    new_candidate = sorted(set(candidate) - set(baseline))
    lines.extend(
        [
            "",
            f"Matched scenes: {len(common)}. "
            f"Missing from candidate: {len(missing_candidate)}. "
            f"New in candidate: {len(new_candidate)}.",
            "",
            "Pixel hashes are a determinism signal, not a perceptual quality score.",
            "",
        ]
    )
    return "\n".join(lines), regressions, hash_changed, incompatible


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Validate JSONL results or compare baseline and candidate directories."
    )
    parser.add_argument(
        "inputs",
        nargs="+",
        help="One or more JSONL files/directories for validation; exactly two for comparison.",
    )
    parser.add_argument(
        "--validate",
        action="store_true",
        help="Validate all inputs without comparing them.",
    )
    parser.add_argument(
        "--summary",
        action="store_true",
        help="Generate a Markdown summary for one run directory or file set.",
    )
    parser.add_argument("--output", help="Write the Markdown report to this path.")
    parser.add_argument(
        "--regression-threshold",
        type=float,
        default=10.0,
        help="Median frame-time increase treated as a regression (default: 10).",
    )
    parser.add_argument(
        "--fail-on-regression",
        action="store_true",
        help="Return a failure exit code when a median regression exceeds the threshold.",
    )
    parser.add_argument(
        "--fail-on-hash-change",
        action="store_true",
        help="Return a failure exit code when matching scene pixel hashes differ.",
    )
    parser.add_argument(
        "--allow-incompatible",
        action="store_true",
        help="Allow comparison when machine, driver, build, or sample settings differ.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_arguments()
    try:
        if args.validate and args.summary:
            raise ResultError("--validate and --summary cannot be combined")
        if args.validate:
            runs = load_runs(args.inputs)
            result_count = sum(len(run.results) for run in runs)
            print(
                f"Validated {len(runs)} run file(s), {result_count} result record(s)."
            )
            return 0
        if args.summary:
            report = make_summary(load_runs(args.inputs))
            if args.output:
                output = Path(args.output)
                output.parent.mkdir(parents=True, exist_ok=True)
                output.write_text(report, encoding="utf-8", newline="\n")
                print(f"Wrote {output}")
            else:
                print(report)
            return 0
        if len(args.inputs) != 2:
            raise ResultError(
                "comparison requires exactly two inputs: baseline and candidate"
            )
        baseline_runs = load_runs([args.inputs[0]])
        candidate_runs = load_runs([args.inputs[1]])
        report, regressions, hash_changed, incompatible = make_report(
            baseline_runs, candidate_runs, args.regression_threshold
        )
        if args.output:
            output = Path(args.output)
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_text(report, encoding="utf-8", newline="\n")
            print(f"Wrote {output}")
        else:
            print(report)
        if incompatible and not args.allow_incompatible:
            print(
                "Refusing a timing verdict for incompatible environments; "
                "use --allow-incompatible for exploratory comparison.",
                file=sys.stderr,
            )
            return 4
        if args.fail_on_regression and regressions:
            return 2
        if args.fail_on_hash_change and hash_changed:
            return 3
        return 0
    except ResultError as error:
        print(f"performance comparison error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

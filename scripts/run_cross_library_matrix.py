#!/usr/bin/env python3
"""Run a parameterized, quality-gated cross-library ABBA matrix."""

from __future__ import annotations

import argparse
import csv
import json
import os
import shlex
import statistics
import subprocess
import sys
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any, Iterable


SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import cross_library_benchmark as cross_library
import run_benchmark_matrix as performance_matrix


DEFAULT_SEEDS = {
    "smoke": (1001,),
    "standard": (1001,),
    "thorough": (1001, 2003, 3001),
}

SCENE_MAP = {
    "geometry_stress": "geometry_stress",
    "image_grid": "image_grid",
    "text_stress": "contract_text_latin",
}


@dataclass(frozen=True)
class MatrixCase:
    workload: performance_matrix.Workload
    candidate: str
    reference_median_ms: float
    reference_ci_ms: tuple[float, float]
    candidate_median_ms: float
    candidate_ci_ms: tuple[float, float]
    relative_median: float
    relative_ci: tuple[float, float]
    quality: dict[str, Any]
    summary_file: str


def build_workloads(
    preset: str, seeds: Iterable[int]
) -> list[performance_matrix.Workload]:
    workloads = performance_matrix.build_workloads(preset, seeds)
    return [
        performance_matrix.Workload(
            scene=SCENE_MAP[workload.scene],
            mode=workload.mode,
            operations=workload.operations,
            seed=workload.seed,
            texture_count=workload.texture_count,
            rounded_ratio=workload.rounded_ratio,
            state_change_rate=workload.state_change_rate,
            text_length=workload.text_length,
        )
        for workload in workloads
    ]


def adapter_argument(adapter: cross_library.Adapter) -> str:
    command = (
        subprocess.list2cmdline(adapter.command)
        if os.name == "nt"
        else shlex.join(adapter.command)
    )
    return f"{adapter.label}={command}"


def command_for(
    python: str,
    runner: Path,
    contract: Path,
    reference: cross_library.Adapter,
    adapters: list[cross_library.Adapter],
    profile: str,
    width: int,
    height: int,
    frames: int | None,
    warmup: int | None,
    repetitions: int,
    bootstrap_samples: int,
    timeout: int,
    workload: performance_matrix.Workload,
    output_dir: Path,
) -> list[str]:
    command = [
        python,
        str(runner),
        "--contract",
        str(contract),
        "--reference",
        adapter_argument(reference),
    ]
    for adapter in adapters:
        command.extend(("--adapter", adapter_argument(adapter)))
    command.extend(
        (
            "--scene",
            workload.scene,
            "--profile",
            profile,
            "--width",
            str(width),
            "--height",
            str(height),
            "--repetitions",
            str(repetitions),
            "--bootstrap-samples",
            str(bootstrap_samples),
            "--timeout",
            str(timeout),
            "--workload",
            workload.mode,
            "--operations",
            str(workload.operations),
            "--seed",
            str(workload.seed),
            "--texture-count",
            str(workload.texture_count),
            "--rounded-ratio",
            str(workload.rounded_ratio),
            "--state-change-rate",
            str(workload.state_change_rate),
            "--text-length",
            str(workload.text_length),
            "--output-dir",
            str(output_dir),
        )
    )
    if frames is not None:
        command.extend(("--frames", str(frames)))
    if warmup is not None:
        command.extend(("--warmup", str(warmup)))
    return command


def load_cases(
    summary_path: Path,
    workload: performance_matrix.Workload,
    matrix_output_dir: Path,
    bootstrap_samples: int,
) -> list[MatrixCase]:
    summary = json.loads(summary_path.read_text(encoding="utf-8"))
    cases: list[MatrixCase] = []
    for index, comparison in enumerate(summary["comparisons"]):
        references = [
            float(value)
            for value in comparison["reference_process_medians_ms"]
        ]
        candidates = [
            float(value)
            for value in comparison["candidate_process_medians_ms"]
        ]
        relatives = [
            float(value) for value in comparison["abba_relative_samples"]
        ]
        reference_ci = cross_library.bootstrap_median_ci(
            references,
            bootstrap_samples,
            seed=workload.seed + index * 9176 + 31,
        )
        cases.append(
            MatrixCase(
                workload=workload,
                candidate=str(comparison["candidate"]),
                reference_median_ms=float(statistics.median(references)),
                reference_ci_ms=reference_ci,
                candidate_median_ms=float(statistics.median(candidates)),
                candidate_ci_ms=tuple(comparison["candidate_median_ci_ms"]),
                relative_median=float(statistics.median(relatives)),
                relative_ci=tuple(comparison["relative_ci"]),
                quality=dict(comparison["quality"]),
                summary_file=summary_path.relative_to(
                    matrix_output_dir
                ).as_posix(),
            )
        )
    return cases


def verdict(case: MatrixCase, reference: str) -> str:
    if not case.quality["passed"]:
        return "invalid: quality failed"
    if case.relative_ci[0] > 1.0:
        return f"{reference} faster"
    if case.relative_ci[1] < 1.0:
        return f"{case.candidate} faster"
    return "inconclusive"


def markdown_report(
    cases: list[MatrixCase],
    reference: str,
    preset: str,
    repetitions: int,
) -> str:
    lines = [
        "# Cross-Library Parameter Matrix",
        "",
        f"Preset: `{preset}`. Reference: `{reference}`. Each matrix cell uses "
        f"{repetitions // 2} ABBA blocks and {repetitions} fresh processes "
        "per renderer.",
        "",
        "| Scene | Mode | Ops | Seed | Texture / rounded / state / text | "
        f"{reference} median (95% CI) | Candidate median (95% CI) | "
        "Candidate / reference (95% CI) | Quality | Verdict |",
        "| --- | --- | ---: | ---: | --- | ---: | ---: | ---: | --- | --- |",
    ]
    for case in cases:
        workload = case.workload
        parameters = (
            f"{workload.texture_count} / {workload.rounded_ratio:.3f} / "
            f"{workload.state_change_rate:.3f} / {workload.text_length}"
        )
        lines.append(
            f"| `{workload.scene}` | `{workload.mode}` | "
            f"{workload.operations} | {workload.seed} | {parameters} | "
            f"{case.reference_median_ms:.3f} ms "
            f"[{case.reference_ci_ms[0]:.3f}, "
            f"{case.reference_ci_ms[1]:.3f}] | "
            f"{case.candidate_median_ms:.3f} ms "
            f"[{case.candidate_ci_ms[0]:.3f}, "
            f"{case.candidate_ci_ms[1]:.3f}] | "
            f"{case.relative_median:.3f}x "
            f"[{case.relative_ci[0]:.3f}, "
            f"{case.relative_ci[1]:.3f}] | "
            f"{'PASS' if case.quality['passed'] else 'FAIL'} | "
            f"{verdict(case, reference)} |"
        )
    passed = sum(bool(case.quality["passed"]) for case in cases)
    reference_wins = sum(
        case.quality["passed"] and case.relative_ci[0] > 1.0
        for case in cases
    )
    candidate_wins = sum(
        case.quality["passed"] and case.relative_ci[1] < 1.0
        for case in cases
    )
    inconclusive = passed - reference_wins - candidate_wins
    lines.extend(
        (
            "",
            "## Summary",
            "",
            f"- Quality gates passed: {passed}/{len(cases)}.",
            f"- Reference faster with the full 95% CI above 1.0: "
            f"{reference_wins}.",
            "- Candidate faster with the full 95% CI below 1.0: "
            f"{candidate_wins}.",
            f"- Inconclusive at this repetition count: {inconclusive}.",
            "",
            "The ratio is candidate/reference inside each ABBA block, so "
            "values above 1.0 favor the reference. A quality failure "
            "invalidates timing. Each cell retains its raw frame arrays, "
            "captures, process order, and quality metrics in its own "
            "directory.",
            "",
        )
    )
    return "\n".join(lines)


def write_csv(path: Path, cases: list[MatrixCase], reference: str) -> None:
    fields = (
        "scene",
        "mode",
        "operations",
        "seed",
        "texture_count",
        "rounded_ratio",
        "state_change_rate",
        "text_length",
        "reference",
        "candidate",
        "reference_median_ms",
        "reference_ci_low_ms",
        "reference_ci_high_ms",
        "candidate_median_ms",
        "candidate_ci_low_ms",
        "candidate_ci_high_ms",
        "relative_median",
        "relative_ci_low",
        "relative_ci_high",
        "quality_passed",
        "quality_mae",
        "quality_rmse",
        "quality_changed_pixel_fraction",
        "verdict",
        "summary_file",
    )
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        for case in cases:
            workload = case.workload
            writer.writerow(
                {
                    "scene": workload.scene,
                    "mode": workload.mode,
                    "operations": workload.operations,
                    "seed": workload.seed,
                    "texture_count": workload.texture_count,
                    "rounded_ratio": workload.rounded_ratio,
                    "state_change_rate": workload.state_change_rate,
                    "text_length": workload.text_length,
                    "reference": reference,
                    "candidate": case.candidate,
                    "reference_median_ms": case.reference_median_ms,
                    "reference_ci_low_ms": case.reference_ci_ms[0],
                    "reference_ci_high_ms": case.reference_ci_ms[1],
                    "candidate_median_ms": case.candidate_median_ms,
                    "candidate_ci_low_ms": case.candidate_ci_ms[0],
                    "candidate_ci_high_ms": case.candidate_ci_ms[1],
                    "relative_median": case.relative_median,
                    "relative_ci_low": case.relative_ci[0],
                    "relative_ci_high": case.relative_ci[1],
                    "quality_passed": case.quality["passed"],
                    "quality_mae": case.quality["mean_absolute_error"],
                    "quality_rmse": case.quality["root_mean_square_error"],
                    "quality_changed_pixel_fraction": case.quality[
                        "changed_pixel_fraction"
                    ],
                    "verdict": verdict(case, reference),
                    "summary_file": case.summary_file,
                }
            )


def parse_arguments() -> argparse.Namespace:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--contract",
        default=str(root / "benchmarks/cross_library/contract.json"),
    )
    parser.add_argument(
        "--reference", required=True, type=cross_library.parse_adapter
    )
    parser.add_argument(
        "--adapter",
        action="append",
        default=[],
        type=cross_library.parse_adapter,
    )
    parser.add_argument(
        "--preset",
        choices=("smoke", "standard", "thorough"),
        default="standard",
    )
    parser.add_argument(
        "--profile",
        choices=("quick", "standard", "thorough"),
        default="standard",
    )
    parser.add_argument("--seeds", type=performance_matrix.parse_seeds)
    parser.add_argument("--width", type=int, default=1920)
    parser.add_argument("--height", type=int, default=1080)
    parser.add_argument("--frames", type=int)
    parser.add_argument("--warmup", type=int)
    parser.add_argument("--repetitions", type=int, default=4)
    parser.add_argument("--bootstrap-samples", type=int, default=10000)
    parser.add_argument("--timeout", type=int, default=300)
    parser.add_argument(
        "--output-dir", default="build/cross-library-matrix"
    )
    parser.add_argument("--dry-run", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_arguments()
    if not args.adapter:
        raise cross_library.BenchmarkError(
            "at least one --adapter is required"
        )
    if len({args.reference.label, *(item.label for item in args.adapter)}) != (
        len(args.adapter) + 1
    ):
        raise cross_library.BenchmarkError("adapter labels must be unique")
    if min(args.width, args.height, args.timeout) <= 0:
        raise cross_library.BenchmarkError(
            "dimensions and timeout must be positive"
        )
    if args.repetitions < 2 or args.repetitions % 2:
        raise cross_library.BenchmarkError(
            "repetitions must be an even number of at least 2"
        )
    if args.bootstrap_samples <= 0:
        raise cross_library.BenchmarkError(
            "bootstrap-samples must be positive"
        )

    root = Path(__file__).resolve().parents[1]
    runner = root / "scripts/cross_library_benchmark.py"
    contract = Path(args.contract).resolve()
    contract_data = json.loads(contract.read_text(encoding="utf-8"))
    effective_frames = (
        args.frames
        if args.frames is not None
        else int(contract_data["timing"]["frames"])
    )
    effective_warmup = (
        args.warmup
        if args.warmup is not None
        else int(contract_data["timing"]["warmup"])
    )
    output_dir = Path(args.output_dir)
    if not output_dir.is_absolute():
        output_dir = root / output_dir
    seeds = args.seeds or DEFAULT_SEEDS[args.preset]
    workloads = build_workloads(args.preset, seeds)
    commands: list[
        tuple[performance_matrix.Workload, Path, list[str]]
    ] = []
    for workload in workloads:
        case_dir = output_dir / "cases" / workload.file_stem
        commands.append(
            (
                workload,
                case_dir,
                command_for(
                    sys.executable,
                    runner,
                    contract,
                    args.reference,
                    args.adapter,
                    args.profile,
                    args.width,
                    args.height,
                    args.frames,
                    args.warmup,
                    args.repetitions,
                    args.bootstrap_samples,
                    args.timeout,
                    workload,
                    case_dir,
                ),
            )
        )

    if args.dry_run:
        for _, _, command in commands:
            print(subprocess.list2cmdline(command))
        process_count = (
            len(commands)
            * len(args.adapter)
            * args.repetitions
            * 2
        )
        print(
            f"{len(commands)} matrix cells, {process_count} renderer "
            "processes"
        )
        return 0

    cases: list[MatrixCase] = []
    quality_failed = False
    for index, (workload, case_dir, command) in enumerate(commands, 1):
        print(
            f"[{index}/{len(commands)}] {workload.scene}/"
            f"{workload.mode} n={workload.operations} "
            f"seed={workload.seed}",
            flush=True,
        )
        completed = subprocess.run(
            command,
            cwd=root,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=(
                args.timeout
                * len(args.adapter)
                * args.repetitions
                * 2
                + 30
            ),
            check=False,
        )
        if completed.returncode not in (0, 2):
            raise cross_library.BenchmarkError(
                f"matrix cell failed ({completed.returncode}):\n"
                f"{completed.stdout}"
            )
        quality_failed = quality_failed or completed.returncode == 2
        cases.extend(
            load_cases(
                case_dir / "cross-library-summary.json",
                workload,
                output_dir,
                args.bootstrap_samples,
            )
        )

    output_dir.mkdir(parents=True, exist_ok=True)
    summary_path = output_dir / "matrix-summary.json"
    summary_path.write_text(
        json.dumps(
            {
                "schema": 1,
                "contract": contract_data["version"],
                "preset": args.preset,
                "profile": args.profile,
                "width": args.width,
                "height": args.height,
                "frames": effective_frames,
                "warmup": effective_warmup,
                "seeds": list(seeds),
                "reference": args.reference.label,
                "candidates": [item.label for item in args.adapter],
                "repetitions_per_renderer": args.repetitions,
                "bootstrap_samples": args.bootstrap_samples,
                "cases": [
                    {
                        "workload": asdict(case.workload),
                        "candidate": case.candidate,
                        "reference_median_ms": case.reference_median_ms,
                        "reference_ci_ms": list(case.reference_ci_ms),
                        "candidate_median_ms": case.candidate_median_ms,
                        "candidate_ci_ms": list(case.candidate_ci_ms),
                        "relative_median": case.relative_median,
                        "relative_ci": list(case.relative_ci),
                        "quality": case.quality,
                        "verdict": verdict(
                            case, args.reference.label
                        ),
                        "summary_file": case.summary_file,
                    }
                    for case in cases
                ],
            },
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )
    write_csv(output_dir / "matrix-summary.csv", cases, args.reference.label)
    report = markdown_report(
        cases, args.reference.label, args.preset, args.repetitions
    )
    report_path = output_dir / "matrix-report.md"
    report_path.write_text(report, encoding="utf-8")
    print(report)
    print(f"Report: {report_path}")
    print(f"Summary: {summary_path}")
    return 2 if quality_failed else 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (
        cross_library.BenchmarkError,
        OSError,
        subprocess.SubprocessError,
    ) as error:
        print(f"CROSS_LIBRARY_MATRIX_ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)

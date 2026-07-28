#!/usr/bin/env python3
"""Run parameterized WhatsCanvas workloads and aggregate scaling results."""

from __future__ import annotations

import argparse
import csv
import html
import json
import math
import os
import statistics
import subprocess
import sys
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any, Iterable


DEFAULT_SEEDS = {
    "smoke": (1001,),
    "standard": (1001, 2003, 3001),
    "thorough": (1001, 2003, 3001, 4001, 5003),
}


@dataclass(frozen=True)
class Workload:
    scene: str
    mode: str
    operations: int
    seed: int
    texture_count: int = 1
    rounded_ratio: float = 0.0
    state_change_rate: float = 0.0
    text_length: int = 24

    @property
    def key(self) -> tuple[Any, ...]:
        return (
            self.scene,
            self.mode,
            self.operations,
            self.texture_count,
            self.rounded_ratio,
            self.state_change_rate,
            self.text_length,
        )

    @property
    def file_stem(self) -> str:
        rounded = f"{self.rounded_ratio:.3f}".replace(".", "p")
        state = f"{self.state_change_rate:.3f}".replace(".", "p")
        return (
            f"{self.scene}-{self.mode}-n{self.operations}"
            f"-tex{self.texture_count}-round{rounded}"
            f"-state{state}-text{self.text_length}-seed{self.seed}"
        )


@dataclass
class CompletedRun:
    backend: str
    workload: Workload
    metadata: dict[str, Any]
    result: dict[str, Any]
    source: str


def parse_seeds(value: str) -> tuple[int, ...]:
    try:
        seeds = tuple(int(item.strip()) for item in value.split(",") if item.strip())
    except ValueError as error:
        raise argparse.ArgumentTypeError("seeds must be comma-separated integers") from error
    if not seeds or any(seed < 0 or seed > 2_147_483_647 for seed in seeds):
        raise argparse.ArgumentTypeError("seeds must be in the range 0..2147483647")
    if len(set(seeds)) != len(seeds):
        raise argparse.ArgumentTypeError("seeds must be unique")
    return seeds


def workload_templates(preset: str) -> list[dict[str, Any]]:
    if preset == "smoke":
        geometry_counts = (256,)
        image_counts = (128,)
        text_counts = (128,)
        modes = (
            ("stable", 0.0),
            ("dynamic-structure", 0.125),
        )
    elif preset == "standard":
        geometry_counts = (256, 1024, 4096)
        image_counts = (64, 256, 1024)
        text_counts = (64, 256, 1024)
        modes = (
            ("stable", 0.0),
            ("dynamic-data", 0.0),
            ("dynamic-structure", 0.125),
        )
    elif preset == "thorough":
        geometry_counts = (64, 256, 1024, 4096, 16384)
        image_counts = (16, 64, 256, 1024, 4096)
        text_counts = (64, 256, 1024, 4096)
        modes = (
            ("stable", 0.0),
            ("dynamic-data", 0.0),
            ("dynamic-structure", 0.125),
        )
    else:
        raise ValueError(f"unknown matrix preset: {preset}")

    templates: list[dict[str, Any]] = []
    for operations in geometry_counts:
        for mode, state_rate in modes:
            templates.append(
                {
                    "scene": "geometry_stress",
                    "mode": mode,
                    "operations": operations,
                    "state_change_rate": state_rate,
                }
            )
    for operations in image_counts:
        for mode, state_rate in modes:
            texture_count = {
                "stable": 1,
                "dynamic-data": 4,
                "dynamic-structure": 32,
            }[mode]
            rounded_ratio = {
                "stable": 0.0,
                "dynamic-data": 0.25,
                "dynamic-structure": 0.5,
            }[mode]
            templates.append(
                {
                    "scene": "image_grid",
                    "mode": mode,
                    "operations": operations,
                    "texture_count": texture_count,
                    "rounded_ratio": rounded_ratio,
                    "state_change_rate": state_rate,
                }
            )
    for operations in text_counts:
        for mode, state_rate in modes:
            templates.append(
                {
                    "scene": "text_stress",
                    "mode": mode,
                    "operations": operations,
                    "state_change_rate": (
                        min(state_rate, 0.0625)
                        if mode == "dynamic-structure"
                        else 0.0
                    ),
                    "text_length": 32 if mode == "dynamic-structure" else 24,
                }
            )
    return templates


def build_workloads(preset: str, seeds: Iterable[int]) -> list[Workload]:
    workloads: list[Workload] = []
    for template in workload_templates(preset):
        for seed in seeds:
            workloads.append(Workload(seed=seed, **template))
    return workloads


def find_executable(root: Path, requested: str | None) -> Path:
    candidates = []
    if requested:
        candidates.append(Path(requested))
    candidates.extend(
        (
            root / "build/Release/WhatsCanvasPerformanceSuite.exe",
            root / "build/WhatsCanvasPerformanceSuite",
            root / "build/WhatsCanvasPerformanceSuite.exe",
        )
    )
    for candidate in candidates:
        resolved = candidate if candidate.is_absolute() else root / candidate
        if resolved.is_file():
            return resolved.resolve()
    raise FileNotFoundError("WhatsCanvasPerformanceSuite executable was not found")


def command_for(
    executable: Path,
    backend: str,
    profile: str,
    width: int,
    height: int,
    workload: Workload,
    output: Path,
) -> list[str]:
    return [
        str(executable),
        "--backend",
        backend,
        "--profile",
        profile,
        "--scene",
        workload.scene,
        "--width",
        str(width),
        "--height",
        str(height),
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
        "--output",
        str(output),
    ]


def load_run(path: Path, backend: str, workload: Workload) -> CompletedRun:
    records = [
        json.loads(line)
        for line in path.read_text(encoding="utf-8").splitlines()
        if line.strip()
    ]
    metadata = next(
        (record for record in records if record.get("type") == "metadata"), None
    )
    result = next(
        (record for record in records if record.get("type") == "result"), None
    )
    if metadata is None or result is None:
        raise RuntimeError(f"{path}: metadata or result record is missing")
    if metadata.get("build_type") != "Release":
        raise RuntimeError(f"{path}: matrix results require a Release build")
    expected = {
        "backend": backend,
        "scene": workload.scene,
        "workload_mode": workload.mode,
        "operations_per_frame": workload.operations,
        "workload_seed": workload.seed,
    }
    actual = {
        "backend": result.get("backend"),
        "scene": result.get("scene"),
        "workload_mode": result.get("workload_mode"),
        "operations_per_frame": result.get("operations_per_frame"),
        "workload_seed": result.get("workload_seed"),
    }
    if actual != expected:
        raise RuntimeError(f"{path}: workload metadata mismatch: {actual!r}")
    return CompletedRun(backend, workload, metadata, result, str(path))


def median(values: Iterable[float]) -> float:
    return float(statistics.median(values))


def aggregate_runs(runs: Iterable[CompletedRun]) -> list[dict[str, Any]]:
    grouped: dict[tuple[Any, ...], list[CompletedRun]] = {}
    for run in runs:
        grouped.setdefault((run.backend, *run.workload.key), []).append(run)

    summaries: list[dict[str, Any]] = []
    for key, group in sorted(grouped.items()):
        first = group[0]
        total_values = [
            float(run.result["total_median_ms"]) for run in group
        ]
        operation_count = first.workload.operations
        draw_calls = median(
            float(run.result.get("draw_call_count", 0)) for run in group
        )
        merged = median(
            float(run.result.get("merged_batch_count", 0)) for run in group
        )
        summaries.append(
            {
                "backend": first.backend,
                "scene": first.workload.scene,
                "workload_mode": first.workload.mode,
                "operations_per_frame": operation_count,
                "texture_count": first.workload.texture_count,
                "rounded_ratio": first.workload.rounded_ratio,
                "state_change_rate": first.workload.state_change_rate,
                "text_length": first.workload.text_length,
                "seeds": sorted(run.workload.seed for run in group),
                "process_count": len(group),
                "total_median_ms": median(total_values),
                "total_min_ms": min(total_values),
                "total_max_ms": max(total_values),
                "total_p95_ms": median(
                    float(run.result["total_p95_ms"]) for run in group
                ),
                "record_median_ms": median(
                    float(run.result["record_median_ms"]) for run in group
                ),
                "submit_median_ms": median(
                    float(run.result["submit_median_ms"]) for run in group
                ),
                "operations_per_second": median(
                    float(run.result["operations_per_second"]) for run in group
                ),
                "draw_call_count": draw_calls,
                "merged_batch_count": merged,
                "merge_fraction": (
                    min(1.0, merged / operation_count)
                    if operation_count > 0
                    else 0.0
                ),
                "draw_reduction_fraction": (
                    max(
                        0.0,
                        1.0
                        - max(0.0, draw_calls - 1.0)
                        / operation_count,
                    )
                    if operation_count > 0
                    else 0.0
                ),
                "peak_rss_bytes": int(
                    median(float(run.result["peak_rss_bytes"]) for run in group)
                ),
            }
        )
    return summaries


def environment_label(metadata: dict[str, Any]) -> str:
    return (
        f"{metadata.get('cpu')}; {metadata.get('device')} "
        f"({metadata.get('driver')}); {metadata.get('build_type')}; "
        f"{metadata.get('width')}x{metadata.get('height')}; "
        f"{metadata.get('profile')}"
    )


def markdown_report(
    summaries: list[dict[str, Any]],
    runs: list[CompletedRun],
    preset: str,
    charts: dict[tuple[str, str], str] | None = None,
) -> str:
    lines = [
        "# WhatsCanvas Parameterized Performance Matrix",
        "",
        f"Matrix preset: `{preset}`. Each row aggregates independent seeded "
        "processes; the headline time is the median of process medians.",
        "",
        "The fixed historical scenes remain unchanged. These parameterized "
        "workloads vary scale, frame-to-frame stability, texture cardinality, "
        "rounded coverage, state changes, and text length.",
        "",
    ]
    profiles = {str(run.metadata.get("profile")) for run in runs}
    if "quick" in profiles:
        lines.extend(
            [
                "> Quick profile results validate execution and report shape "
                "only; do not use them for performance claims.",
                "",
            ]
        )
    lines.extend(
        [
            "## Environments",
            "",
            "| Backend | Environment |",
            "|---|---|",
        ]
    )
    environments: dict[str, dict[str, Any]] = {}
    for run in runs:
        environments.setdefault(run.backend, run.metadata)
    for backend, metadata in sorted(environments.items()):
        lines.append(f"| {backend} | {environment_label(metadata)} |")

    scene_titles = {
        "geometry_stress": "Geometry Scaling",
        "image_grid": "Image Scaling",
        "text_stress": "Text Scaling",
    }
    for scene, title in scene_titles.items():
        rows = [row for row in summaries if row["scene"] == scene]
        if not rows:
            continue
        lines.extend(
            [
                "",
                f"## {title}",
                "",
            ]
        )
        for backend in sorted({str(row["backend"]) for row in rows}):
            chart = (charts or {}).get((backend, scene))
            if chart:
                lines.extend(
                    [
                        f"![{backend} {title}]({chart})",
                        "",
                    ]
                )
        lines.extend(
            [
                "| Backend | Mode | Ops | Textures | Rounded | State changes | "
                "Text length | Seeds | Median | Range | p95 | Ops/s | "
                "Record | Submit | Draws | Draw reduction |",
                "|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|"
                "---:|---:|---:|---:|---:|",
            ]
        )
        for row in rows:
            seeds = ",".join(str(seed) for seed in row["seeds"])
            textures = (
                str(row["texture_count"])
                if scene == "image_grid"
                else "-"
            )
            rounded = (
                f"{row['rounded_ratio']:.0%}"
                if scene == "image_grid"
                else "-"
            )
            text_length = (
                str(row["text_length"])
                if scene == "text_stress"
                else "-"
            )
            lines.append(
                f"| {row['backend']} | {row['workload_mode']} | "
                f"{row['operations_per_frame']} | {textures} | "
                f"{rounded} | "
                f"{row['state_change_rate']:.1%} | {text_length} | "
                f"{seeds} | {row['total_median_ms']:.3f} ms | "
                f"{row['total_min_ms']:.3f}-{row['total_max_ms']:.3f} ms | "
                f"{row['total_p95_ms']:.3f} ms | "
                f"{row['operations_per_second']:.0f} | "
                f"{row['record_median_ms']:.3f} ms | "
                f"{row['submit_median_ms']:.3f} ms | "
                f"{row['draw_call_count']:.0f} | "
                f"{row['draw_reduction_fraction']:.1%} |"
            )

    lines.extend(
        [
            "",
            "## Interpretation",
            "",
            "- `stable` isolates reusable command and resource paths.",
            "- `dynamic-data` changes positions, colors, or text selections while "
            "keeping command topology stable.",
            "- `dynamic-structure` changes primitive, texture, text, and state "
            "selection between frames.",
            "- Draw reduction excludes the one full-frame background operation "
            "and reports how many workload operations avoided separate draws.",
            "- Scaling curves and state-heavy rows are stronger evidence of "
            "general performance than any single fixed object count.",
            "",
        ]
    )
    return "\n".join(lines)


def write_scaling_charts(
    output_dir: Path, summaries: list[dict[str, Any]]
) -> dict[tuple[str, str], str]:
    chart_dir = output_dir / "charts"
    chart_dir.mkdir(parents=True, exist_ok=True)
    grouped: dict[tuple[str, str], list[dict[str, Any]]] = {}
    for row in summaries:
        grouped.setdefault(
            (str(row["backend"]), str(row["scene"])), []
        ).append(row)

    titles = {
        "geometry_stress": "Geometry scaling",
        "image_grid": "Image scaling",
        "text_stress": "Text scaling",
    }
    colors = {
        "stable": "#157f68",
        "dynamic-data": "#2764c4",
        "dynamic-structure": "#c14444",
    }
    paths: dict[tuple[str, str], str] = {}
    for (backend, scene), rows in sorted(grouped.items()):
        values = [
            (
                float(row["operations_per_frame"]),
                float(row["total_median_ms"]),
            )
            for row in rows
            if float(row["operations_per_frame"]) > 0
            and float(row["total_median_ms"]) > 0
        ]
        if not values:
            continue
        x_logs = [math.log10(value[0]) for value in values]
        y_logs = [math.log10(value[1]) for value in values]
        x_min, x_max = min(x_logs), max(x_logs)
        y_min, y_max = min(y_logs), max(y_logs)
        if x_min == x_max:
            x_min -= 0.5
            x_max += 0.5
        if y_min == y_max:
            y_min -= 0.5
            y_max += 0.5

        width, height = 960, 540
        left, right, top, bottom = 90, 34, 64, 72
        plot_width = width - left - right
        plot_height = height - top - bottom

        def x_position(value: float) -> float:
            return left + (
                (math.log10(value) - x_min) / (x_max - x_min)
            ) * plot_width

        def y_position(value: float) -> float:
            return top + (
                1.0
                - (math.log10(value) - y_min) / (y_max - y_min)
            ) * plot_height

        svg = [
            (
                f'<svg xmlns="http://www.w3.org/2000/svg" '
                f'width="{width}" height="{height}" '
                f'viewBox="0 0 {width} {height}">'
            ),
            '<rect width="100%" height="100%" fill="#ffffff"/>',
            (
                f'<text x="{left}" y="34" font-family="sans-serif" '
                f'font-size="22" fill="#15191f">'
                f"{html.escape(backend)} {html.escape(titles.get(scene, scene))}"
                "</text>"
            ),
        ]
        for tick in range(5):
            fraction = tick / 4
            x_log = x_min + fraction * (x_max - x_min)
            x = left + fraction * plot_width
            x_value = 10**x_log
            svg.extend(
                [
                    (
                        f'<line x1="{x:.2f}" y1="{top}" x2="{x:.2f}" '
                        f'y2="{top + plot_height}" stroke="#e5e8ec"/>'
                    ),
                    (
                        f'<text x="{x:.2f}" y="{top + plot_height + 28}" '
                        f'text-anchor="middle" font-family="sans-serif" '
                        f'font-size="13" fill="#4d5662">{x_value:.0f}</text>'
                    ),
                ]
            )
            y_log = y_min + fraction * (y_max - y_min)
            y = top + (1.0 - fraction) * plot_height
            y_value = 10**y_log
            svg.extend(
                [
                    (
                        f'<line x1="{left}" y1="{y:.2f}" '
                        f'x2="{left + plot_width}" y2="{y:.2f}" '
                        f'stroke="#e5e8ec"/>'
                    ),
                    (
                        f'<text x="{left - 12}" y="{y + 5:.2f}" '
                        f'text-anchor="end" font-family="sans-serif" '
                        f'font-size="13" fill="#4d5662">{y_value:.3g}</text>'
                    ),
                ]
            )
        svg.extend(
            [
                (
                    f'<line x1="{left}" y1="{top + plot_height}" '
                    f'x2="{left + plot_width}" y2="{top + plot_height}" '
                    f'stroke="#20262d" stroke-width="1.5"/>'
                ),
                (
                    f'<line x1="{left}" y1="{top}" x2="{left}" '
                    f'y2="{top + plot_height}" stroke="#20262d" '
                    f'stroke-width="1.5"/>'
                ),
                (
                    f'<text x="{left + plot_width / 2:.2f}" y="{height - 18}" '
                    f'text-anchor="middle" font-family="sans-serif" '
                    f'font-size="14" fill="#303841">operations/frame (log)</text>'
                ),
                (
                    f'<text x="20" y="{top + plot_height / 2:.2f}" '
                    f'transform="rotate(-90 20 {top + plot_height / 2:.2f})" '
                    f'text-anchor="middle" font-family="sans-serif" '
                    f'font-size="14" fill="#303841">median ms (log)</text>'
                ),
            ]
        )
        modes = sorted({str(row["workload_mode"]) for row in rows})
        for mode_index, mode in enumerate(modes):
            mode_rows = sorted(
                (
                    row
                    for row in rows
                    if row["workload_mode"] == mode
                ),
                key=lambda row: int(row["operations_per_frame"]),
            )
            color = colors.get(mode, "#6c7480")
            points = " ".join(
                f"{x_position(float(row['operations_per_frame'])):.2f},"
                f"{y_position(float(row['total_median_ms'])):.2f}"
                for row in mode_rows
            )
            if len(mode_rows) > 1:
                svg.append(
                    f'<polyline points="{points}" fill="none" '
                    f'stroke="{color}" stroke-width="3"/>'
                )
            for row in mode_rows:
                svg.append(
                    f'<circle cx="{x_position(float(row["operations_per_frame"])):.2f}" '
                    f'cy="{y_position(float(row["total_median_ms"])):.2f}" '
                    f'r="5" fill="{color}" stroke="#ffffff" stroke-width="1.5"/>'
                )
            legend_x = left + plot_width - 190
            legend_y = top + 8 + mode_index * 24
            svg.extend(
                [
                    (
                        f'<line x1="{legend_x}" y1="{legend_y}" '
                        f'x2="{legend_x + 24}" y2="{legend_y}" '
                        f'stroke="{color}" stroke-width="3"/>'
                    ),
                    (
                        f'<text x="{legend_x + 32}" y="{legend_y + 5}" '
                        f'font-family="sans-serif" font-size="13" '
                        f'fill="#303841">{html.escape(mode)}</text>'
                    ),
                ]
            )
        svg.append("</svg>")
        filename = f"{backend}-{scene}.svg"
        (chart_dir / filename).write_text(
            "\n".join(svg) + "\n", encoding="utf-8"
        )
        paths[(backend, scene)] = f"charts/{filename}"
    return paths


def write_csv(path: Path, summaries: list[dict[str, Any]]) -> None:
    if not summaries:
        return
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(summaries[0].keys()))
        writer.writeheader()
        for row in summaries:
            csv_row = dict(row)
            csv_row["seeds"] = ",".join(str(seed) for seed in row["seeds"])
            writer.writerow(csv_row)


def git_revision(root: Path) -> str:
    try:
        return subprocess.run(
            ["git", "-C", str(root), "rev-parse", "--verify", "HEAD"],
            check=True,
            capture_output=True,
            text=True,
            timeout=10,
        ).stdout.strip()
    except (OSError, subprocess.SubprocessError):
        return ""


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable")
    parser.add_argument(
        "--backend",
        action="append",
        choices=("software", "opengl", "vulkan"),
        default=[],
    )
    parser.add_argument(
        "--preset", choices=("smoke", "standard", "thorough"), default="standard"
    )
    parser.add_argument(
        "--profile", choices=("quick", "standard", "thorough"), default="standard"
    )
    parser.add_argument("--seeds", type=parse_seeds)
    parser.add_argument("--width", type=int, default=1920)
    parser.add_argument("--height", type=int, default=1080)
    parser.add_argument("--output-dir", default="build/performance-matrix")
    parser.add_argument("--timeout", type=int, default=300)
    parser.add_argument("--dry-run", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_arguments()
    root = Path(__file__).resolve().parents[1]
    executable = find_executable(root, args.executable)
    if args.width <= 0 or args.height <= 0:
        raise ValueError("matrix dimensions must be positive")
    if args.timeout <= 0:
        raise ValueError("timeout must be positive")
    backends = args.backend or ["vulkan"]
    seeds = args.seeds or DEFAULT_SEEDS[args.preset]
    workloads = build_workloads(args.preset, seeds)
    output_dir = Path(args.output_dir)
    if not output_dir.is_absolute():
        output_dir = root / output_dir
    raw_dir = output_dir / "raw"

    commands = []
    for backend in backends:
        for workload in workloads:
            output = raw_dir / f"{backend}-{workload.file_stem}.jsonl"
            commands.append(
                (
                    backend,
                    workload,
                    output,
                    command_for(
                        executable,
                        backend,
                        args.profile,
                        args.width,
                        args.height,
                        workload,
                        output,
                    ),
                )
            )
    if args.dry_run:
        for _, _, _, command in commands:
            print(subprocess.list2cmdline(command))
        print(f"{len(commands)} workload processes")
        return 0

    raw_dir.mkdir(parents=True, exist_ok=True)
    environment = os.environ.copy()
    environment["WHATSCANVAS_PERF_COMMIT"] = git_revision(root)
    completed: list[CompletedRun] = []
    for index, (backend, workload, output, command) in enumerate(commands, 1):
        print(
            f"[{index}/{len(commands)}] {backend}/{workload.scene} "
            f"{workload.mode} n={workload.operations} seed={workload.seed}",
            flush=True,
        )
        process = subprocess.run(
            command,
            cwd=root,
            env=environment,
            capture_output=True,
            text=True,
            timeout=args.timeout,
        )
        if process.returncode != 0:
            raise RuntimeError(
                f"benchmark failed ({process.returncode}): "
                f"{subprocess.list2cmdline(command)}\n"
                f"{process.stdout}\n{process.stderr}"
            )
        completed.append(load_run(output, backend, workload))

    summaries = aggregate_runs(completed)
    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir / "matrix-summary.json").write_text(
        json.dumps(
            {
                "schema": 1,
                "preset": args.preset,
                "profile": args.profile,
                "width": args.width,
                "height": args.height,
                "seeds": list(seeds),
                "runs": [
                    {
                        "backend": run.backend,
                        "workload": asdict(run.workload),
                        "source": run.source,
                        "result": run.result,
                    }
                    for run in completed
                ],
                "summary": summaries,
            },
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )
    write_csv(output_dir / "matrix-summary.csv", summaries)
    charts = write_scaling_charts(output_dir, summaries)
    report = markdown_report(
        summaries, completed, args.preset, charts
    )
    report_path = output_dir / "matrix-report.md"
    report_path.write_text(report, encoding="utf-8")
    print(report)
    print(f"Report: {report_path}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (FileNotFoundError, RuntimeError, ValueError, subprocess.TimeoutExpired) as error:
        print(f"PERFORMANCE_MATRIX_ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)

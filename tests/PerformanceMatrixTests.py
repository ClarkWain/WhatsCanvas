#!/usr/bin/env python3

import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPT = (
    Path(__file__).resolve().parents[1]
    / "scripts"
    / "run_benchmark_matrix.py"
)
SPEC = importlib.util.spec_from_file_location(
    "run_benchmark_matrix", SCRIPT
)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)

COMPARE_SCRIPT = (
    Path(__file__).resolve().parents[1]
    / "scripts"
    / "compare_performance.py"
)
COMPARE_SPEC = importlib.util.spec_from_file_location(
    "compare_performance_matrix_test", COMPARE_SCRIPT
)
COMPARE_MODULE = importlib.util.module_from_spec(COMPARE_SPEC)
assert COMPARE_SPEC.loader is not None
sys.modules[COMPARE_SPEC.name] = COMPARE_MODULE
COMPARE_SPEC.loader.exec_module(COMPARE_MODULE)


def completed_run(seed, total_ms):
    workload = MODULE.Workload(
        scene="image_grid",
        mode="dynamic-data",
        operations=256,
        seed=seed,
        texture_count=4,
        rounded_ratio=0.25,
    )
    metadata = {
        "cpu": "test cpu",
        "device": "test gpu",
        "driver": "test driver",
        "build_type": "Release",
        "width": 1920,
        "height": 1080,
        "profile": "standard",
    }
    result = {
        "total_median_ms": total_ms,
        "total_p95_ms": total_ms * 1.1,
        "record_median_ms": total_ms * 0.4,
        "submit_median_ms": total_ms * 0.6,
        "operations_per_second": 256000.0 / total_ms,
        "draw_call_count": 8,
        "merged_batch_count": 248,
        "peak_rss_bytes": 64 * 1024 * 1024,
    }
    return MODULE.CompletedRun(
        "vulkan", workload, metadata, result, f"seed-{seed}.jsonl"
    )


class PerformanceMatrixTests(unittest.TestCase):
    def test_smoke_matrix_covers_categories_and_dynamic_modes(self):
        workloads = MODULE.build_workloads("smoke", (1001,))
        self.assertEqual(len(workloads), 6)
        self.assertEqual(
            {workload.scene for workload in workloads},
            {"geometry_stress", "image_grid", "text_stress"},
        )
        self.assertEqual(
            {workload.mode for workload in workloads},
            {"stable", "dynamic-structure"},
        )

    def test_standard_matrix_uses_three_scales_modes_and_seeds(self):
        workloads = MODULE.build_workloads(
            "standard", (1001, 2003, 3001)
        )
        self.assertEqual(len(workloads), 81)
        geometry_counts = {
            workload.operations
            for workload in workloads
            if workload.scene == "geometry_stress"
        }
        self.assertEqual(geometry_counts, {256, 1024, 4096})

    def test_command_contains_reproducible_workload_parameters(self):
        workload = MODULE.Workload(
            "image_grid",
            "dynamic-structure",
            1024,
            2003,
            texture_count=32,
            rounded_ratio=0.5,
            state_change_rate=0.125,
        )
        command = MODULE.command_for(
            Path("suite"),
            "vulkan",
            "standard",
            1920,
            1080,
            workload,
            Path("result.jsonl"),
        )
        joined = " ".join(command)
        self.assertIn("--operations 1024", joined)
        self.assertIn("--seed 2003", joined)
        self.assertIn("--texture-count 32", joined)
        self.assertIn("--workload dynamic-structure", joined)

    def test_aggregation_uses_median_of_process_medians(self):
        summaries = MODULE.aggregate_runs(
            (completed_run(1001, 1.0), completed_run(2003, 3.0))
        )
        self.assertEqual(len(summaries), 1)
        summary = summaries[0]
        self.assertEqual(summary["total_median_ms"], 2.0)
        self.assertEqual(summary["total_min_ms"], 1.0)
        self.assertEqual(summary["total_max_ms"], 3.0)
        self.assertAlmostEqual(summary["merge_fraction"], 248 / 256)
        self.assertAlmostEqual(
            summary["draw_reduction_fraction"], 1 - 7 / 256
        )

    def test_report_explains_dynamic_modes(self):
        runs = [completed_run(1001, 1.0)]
        report = MODULE.markdown_report(
            MODULE.aggregate_runs(runs), runs, "smoke"
        )
        self.assertIn("median of process medians", report)
        self.assertIn("dynamic-data", report)
        self.assertIn("Image Scaling", report)

    def test_scaling_chart_is_generated_for_each_backend_scene(self):
        runs = [completed_run(1001, 1.0), completed_run(2003, 2.0)]
        with tempfile.TemporaryDirectory() as directory:
            charts = MODULE.write_scaling_charts(
                Path(directory), MODULE.aggregate_runs(runs)
            )
            relative = charts[("vulkan", "image_grid")]
            chart = Path(directory) / relative
            self.assertTrue(chart.is_file())
            content = chart.read_text(encoding="utf-8")
            self.assertIn("<svg", content)
            self.assertIn("Image scaling", content)

    def test_comparison_key_keeps_matrix_workloads_distinct(self):
        run = COMPARE_MODULE.Run(
            Path("sample.jsonl"),
            {"profile": "standard"},
            [],
        )
        base = {
            "backend": "vulkan",
            "scene": "image_grid",
            "width": 1920,
            "height": 1080,
            "workload_mode": "stable",
            "operations_per_frame": 256,
            "workload_seed": 1001,
        }
        changed = dict(base)
        changed["workload_seed"] = 2003
        self.assertNotEqual(
            COMPARE_MODULE.run_key(run, base),
            COMPARE_MODULE.run_key(run, changed),
        )


if __name__ == "__main__":
    unittest.main()

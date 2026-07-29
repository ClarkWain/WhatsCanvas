#!/usr/bin/env python3

import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPT = (
    Path(__file__).resolve().parents[1]
    / "scripts"
    / "run_cross_library_matrix.py"
)
SPEC = importlib.util.spec_from_file_location(
    "run_cross_library_matrix", SCRIPT
)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


class CrossLibraryMatrixTests(unittest.TestCase):
    def test_smoke_matrix_maps_text_to_contract_scene(self):
        workloads = MODULE.build_workloads("smoke", (1001,))
        self.assertEqual(len(workloads), 6)
        self.assertEqual(
            {workload.scene for workload in workloads},
            {
                "geometry_stress",
                "image_grid",
                "contract_text_latin",
            },
        )
        self.assertEqual(
            {workload.mode for workload in workloads},
            {"stable", "dynamic-structure"},
        )

    def test_standard_matrix_covers_scale_and_change_modes(self):
        workloads = MODULE.build_workloads("standard", (1001,))
        self.assertEqual(len(workloads), 27)
        self.assertEqual(
            {
                workload.operations
                for workload in workloads
                if workload.scene == "geometry_stress"
            },
            {256, 1024, 4096},
        )
        self.assertEqual(
            {workload.mode for workload in workloads},
            {"stable", "dynamic-data", "dynamic-structure"},
        )

    def test_command_carries_every_workload_parameter(self):
        workload = MODULE.performance_matrix.Workload(
            scene="image_grid",
            mode="dynamic-structure",
            operations=1024,
            seed=2003,
            texture_count=32,
            rounded_ratio=0.5,
            state_change_rate=0.125,
            text_length=24,
        )
        command = MODULE.command_for(
            "python",
            Path("runner.py"),
            Path("contract.json"),
            MODULE.cross_library.Adapter("reference", ("reference.exe",)),
            [MODULE.cross_library.Adapter("candidate", ("candidate.exe",))],
            "standard",
            1920,
            1080,
            None,
            None,
            4,
            1000,
            300,
            workload,
            Path("out"),
        )
        joined = " ".join(command)
        self.assertIn("--workload dynamic-structure", joined)
        self.assertIn("--operations 1024", joined)
        self.assertIn("--seed 2003", joined)
        self.assertIn("--texture-count 32", joined)
        self.assertIn("--rounded-ratio 0.5", joined)
        self.assertIn("--state-change-rate 0.125", joined)

    def test_report_classifies_confident_and_inconclusive_results(self):
        workload = MODULE.performance_matrix.Workload(
            "geometry_stress", "stable", 256, 1001
        )
        quality = {
            "passed": True,
            "mean_absolute_error": 0.1,
            "root_mean_square_error": 0.2,
            "changed_pixel_fraction": 0.0,
        }
        confident = MODULE.MatrixCase(
            workload,
            "candidate",
            1.0,
            (0.9, 1.1),
            1.4,
            (1.3, 1.5),
            1.4,
            (1.2, 1.6),
            quality,
            "case/summary.json",
        )
        uncertain = MODULE.MatrixCase(
            workload,
            "candidate",
            1.0,
            (0.9, 1.1),
            1.0,
            (0.9, 1.1),
            1.0,
            (0.9, 1.1),
            quality,
            "case/summary.json",
        )
        self.assertEqual(
            MODULE.verdict(confident, "reference"), "reference faster"
        )
        self.assertEqual(
            MODULE.verdict(uncertain, "reference"), "inconclusive"
        )
        report = MODULE.markdown_report(
            [confident, uncertain], "reference", "smoke", 4
        )
        self.assertIn("Quality gates passed: 2/2", report)
        self.assertIn("Inconclusive at this repetition count: 1", report)

    def test_csv_contains_workload_and_quality_fields(self):
        workload = MODULE.performance_matrix.Workload(
            "image_grid", "dynamic-data", 128, 1001, 4, 0.25
        )
        case = MODULE.MatrixCase(
            workload,
            "candidate",
            1.0,
            (0.9, 1.1),
            1.2,
            (1.1, 1.3),
            1.2,
            (1.1, 1.3),
            {
                "passed": True,
                "mean_absolute_error": 0.1,
                "root_mean_square_error": 0.2,
                "changed_pixel_fraction": 0.01,
            },
            "case/summary.json",
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "matrix.csv"
            MODULE.write_csv(path, [case], "reference")
            content = path.read_text(encoding="utf-8")
            self.assertIn("quality_passed", content)
            self.assertIn("dynamic-data", content)


if __name__ == "__main__":
    unittest.main()

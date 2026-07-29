#!/usr/bin/env python3

import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPT = (
    Path(__file__).resolve().parents[1]
    / "scripts"
    / "cross_library_benchmark.py"
)
SPEC = importlib.util.spec_from_file_location("cross_library_benchmark", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


class CrossLibraryBenchmarkTests(unittest.TestCase):
    def test_adapter_syntax(self):
        adapter = MODULE.parse_adapter("sample=renderer --backend opengl")
        self.assertEqual(adapter.label, "sample")
        self.assertEqual(adapter.command[-2:], ("--backend", "opengl"))

    @unittest.skipUnless(sys.platform == "win32", "Windows command parsing")
    def test_adapter_accepts_quoted_windows_executable(self):
        adapter = MODULE.parse_adapter(
            'sample="C:\\Program Files\\Renderer\\adapter.exe" --backend vulkan'
        )
        self.assertEqual(
            adapter.command[0],
            "C:\\Program Files\\Renderer\\adapter.exe",
        )

    def test_ppm_and_quality_metrics(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            reference = root / "reference.ppm"
            candidate = root / "candidate.ppm"
            reference.write_bytes(b"P6\n2 1\n255\n" + bytes([0, 0, 0, 255, 255, 255]))
            candidate.write_bytes(b"P6\n2 1\n255\n" + bytes([1, 2, 3, 255, 250, 255]))
            metrics = MODULE.compare_images(
                MODULE.read_ppm(reference),
                MODULE.read_ppm(candidate),
                channel_threshold=4,
            )
            self.assertAlmostEqual(metrics.mean_absolute_error, 11 / 6)
            self.assertEqual(metrics.max_channel_delta, 5)
            self.assertEqual(metrics.changed_pixel_fraction, 0.5)

    def test_ppm_preserves_leading_whitespace_pixel_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "leading-whitespace.ppm"
            pixels = bytes([10, 13, 32, 9, 0, 255])
            path.write_bytes(b"P6\r\n2 1\r\n255\r\n" + pixels)
            self.assertEqual(MODULE.read_ppm(path).pixels, pixels)

    def test_quality_gate(self):
        metrics = MODULE.QualityMetrics(1.0, 2.0, 20, 0.05)
        self.assertTrue(
            MODULE.quality_passes(
                metrics,
                {
                    "max_mean_absolute_error": 1.5,
                    "max_root_mean_square_error": 3.0,
                    "max_changed_pixel_fraction": 0.1,
                },
            )
        )

    def test_relative_quality_gate_rejects_a_blank_renderer(self):
        signal = MODULE.QualityMetrics(20.0, 40.0, 220, 0.2)
        candidate = MODULE.QualityMetrics(12.0, 25.0, 220, 0.18)
        thresholds = {
            "max_mean_absolute_error_fraction_of_reference_signal": 0.85,
            "max_root_mean_square_error_fraction_of_reference_signal": 0.85,
        }
        self.assertTrue(
            MODULE.quality_passes(candidate, thresholds, signal)
        )
        self.assertFalse(
            MODULE.quality_passes(signal, thresholds, signal)
        )

    def test_abba_schedule_balances_process_order(self):
        reference = MODULE.Adapter("a", ("a",))
        candidate = MODULE.Adapter("b", ("b",))
        schedule = MODULE.abba_schedule(reference, candidate, 4)
        self.assertEqual(
            [adapter.label for _, adapter in schedule],
            ["a", "b", "b", "a", "a", "b", "b", "a"],
        )

    def test_bootstrap_median_ci_is_deterministic(self):
        samples = [1.0, 2.0, 3.0, 4.0]
        first = MODULE.bootstrap_median_ci(samples, 500, seed=7)
        second = MODULE.bootstrap_median_ci(samples, 500, seed=7)
        self.assertEqual(first, second)
        self.assertLessEqual(first[0], 2.5)
        self.assertGreaterEqual(first[1], 2.5)

    def test_text_contract_accepts_rasterizer_delta_but_rejects_blank(self):
        contract_path = (
            Path(__file__).resolve().parents[1]
            / "benchmarks"
            / "cross_library"
            / "contract.json"
        )
        contract = json.loads(contract_path.read_text(encoding="utf-8"))
        self.assertEqual(
            contract["assets"]["font_sha256"],
            "797E35F7F5D6020A5C6EA13B42ECD668BCFB3BBC4BAA0E74773527E5B6CB3174",
        )
        thresholds = contract["scenes"]["contract_text_latin"]["quality"]
        nanovg = MODULE.QualityMetrics(7.744, 26.566, 225, 0.08551)
        blank = MODULE.QualityMetrics(9.917, 32.413, 225, 0.10246)
        self.assertTrue(MODULE.quality_passes(nanovg, thresholds))
        self.assertFalse(MODULE.quality_passes(blank, thresholds))
        parameterized = contract["scenes"]["contract_text_latin"][
            "parameterized_quality"
        ]
        dynamic_nanovg = MODULE.QualityMetrics(
            9.285, 34.609, 229, 0.08758
        )
        dynamic_blank = MODULE.QualityMetrics(
            11.007, 40.960, 229, 0.07992
        )
        candidate_signal = MODULE.QualityMetrics(
            11.2, 41.2, 229, 0.081
        )
        empty_signal = MODULE.QualityMetrics(0.0, 0.0, 0, 0.0)
        self.assertTrue(
            MODULE.quality_passes(
                dynamic_nanovg,
                parameterized,
                dynamic_blank,
                candidate_signal,
            )
        )
        self.assertFalse(
            MODULE.quality_passes(
                dynamic_blank,
                parameterized,
                dynamic_blank,
                empty_signal,
            )
        )

    def test_large_text_raster_delta_requires_matching_ink_signal(self):
        contract_path = (
            Path(__file__).resolve().parents[1]
            / "benchmarks"
            / "cross_library"
            / "contract.json"
        )
        contract = json.loads(contract_path.read_text(encoding="utf-8"))
        thresholds = contract["scenes"]["contract_text_latin"][
            "parameterized_quality"
        ]
        reference_signal = MODULE.QualityMetrics(
            5.493, 28.411, 229, 0.04277
        )
        candidate_signal = MODULE.QualityMetrics(
            5.747, 29.161, 229, 0.04438
        )
        raster_delta = MODULE.QualityMetrics(
            5.660, 27.210, 229, 0.0513
        )
        self.assertTrue(
            MODULE.quality_passes(
                raster_delta,
                thresholds,
                reference_signal,
                candidate_signal,
            )
        )

    def test_parameterized_image_quality_is_signal_relative(self):
        contract_path = (
            Path(__file__).resolve().parents[1]
            / "benchmarks"
            / "cross_library"
            / "contract.json"
        )
        contract = json.loads(contract_path.read_text(encoding="utf-8"))
        thresholds = contract["scenes"]["image_grid"][
            "parameterized_quality"
        ]
        signal = MODULE.QualityMetrics(50.76, 66.75, 233, 0.755)
        nanovg = MODULE.QualityMetrics(3.50, 11.37, 229, 0.093)
        self.assertTrue(
            MODULE.quality_passes(nanovg, thresholds, signal)
        )
        self.assertFalse(
            MODULE.quality_passes(signal, thresholds, signal)
        )

    def test_jsonl_rejects_debug_build(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "debug.jsonl"
            metadata = {
                "type": "metadata",
                "backend": "software",
                "library": "sample",
                "library_version": "1",
                "synchronization": "gpu_complete",
                "cross_library_contract": "1.0.0",
                "build_type": "Debug",
                "profile": "quick",
                "width": 2,
                "height": 1,
                "frames": 3,
                "warmup": 1,
            }
            result = {
                "type": "result",
                "scene": "image_grid",
                "total_median_ms": 1.0,
                "total_p95_ms": 1.1,
                "pixel_hash": "abc",
            }
            path.write_text(
                "\n".join((json.dumps(metadata), json.dumps(result))),
                encoding="utf-8",
            )
            with self.assertRaises(MODULE.BenchmarkError):
                MODULE.load_jsonl(path)


if __name__ == "__main__":
    unittest.main()

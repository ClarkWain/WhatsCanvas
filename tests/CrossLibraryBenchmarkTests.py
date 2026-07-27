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

    def test_text_contract_accepts_rasterizer_delta_but_rejects_blank(self):
        contract_path = (
            Path(__file__).resolve().parents[1]
            / "benchmarks"
            / "cross_library"
            / "contract.json"
        )
        contract = json.loads(contract_path.read_text(encoding="utf-8"))
        thresholds = contract["scenes"]["contract_text_latin"]["quality"]
        nanovg = MODULE.QualityMetrics(7.744, 26.566, 225, 0.08551)
        blank = MODULE.QualityMetrics(9.917, 32.413, 225, 0.10246)
        self.assertTrue(MODULE.quality_passes(nanovg, thresholds))
        self.assertFalse(MODULE.quality_passes(blank, thresholds))

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

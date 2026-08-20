#!/usr/bin/env python3

import importlib.util
import contextlib
import io
import json
import pathlib
import struct
import sys
import tempfile
import types
import unittest
import zlib


ROOT = pathlib.Path(__file__).resolve().parents[1]
TOOL_PATH = ROOT / "tools" / "visual_parity" / "visual_parity.py"
SPEC = importlib.util.spec_from_file_location("visual_parity", TOOL_PATH)
assert SPEC is not None and SPEC.loader is not None
visual_parity = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = visual_parity
SPEC.loader.exec_module(visual_parity)


def png_chunk(chunk_type: bytes, payload: bytes) -> bytes:
    return (struct.pack(">I", len(payload)) + chunk_type + payload
            + struct.pack(">I", zlib.crc32(chunk_type + payload) & 0xFFFFFFFF))


def write_rgba_png(path: pathlib.Path, width: int, height: int, rgba: bytes) -> None:
    rows = bytearray()
    for y in range(height):
        rows.append(0)
        start = y * width * 4
        rows.extend(rgba[start:start + width * 4])
    path.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
        + png_chunk(b"IDAT", zlib.compress(bytes(rows)))
        + png_chunk(b"IEND", b"")
    )


def write_pam(path: pathlib.Path, width: int, height: int, rgba: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(
        (f"P7\nWIDTH {width}\nHEIGHT {height}\nDEPTH 4\nMAXVAL 255\n"
         "TUPLTYPE RGB_ALPHA\nENDHDR\n").encode("ascii") + rgba
    )


class VisualParityToolTests(unittest.TestCase):
    def test_repository_contract_is_valid(self) -> None:
        contract = json.loads(
            (ROOT / "tests" / "visual_parity" / "scenes.json").read_text(encoding="utf-8")
        )
        self.assertEqual([], visual_parity.validate_contract(contract))

    def test_png_ppm_and_pam_loaders_preserve_rgba(self) -> None:
        rgba = bytes((10, 20, 30, 40, 50, 60, 70, 80))
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            png = root / "sample.png"
            write_rgba_png(png, 2, 1, rgba)
            self.assertEqual(rgba, visual_parity.read_image(png).rgba)

            ppm = root / "sample.ppm"
            ppm.write_bytes(b"P6\n2 1\n255\n" + bytes((10, 20, 30, 50, 60, 70)))
            self.assertEqual(bytes((10, 20, 30, 255, 50, 60, 70, 255)),
                             visual_parity.read_image(ppm).rgba)

            pam = root / "sample.pam"
            pam.write_bytes(
                b"P7\nWIDTH 2\nHEIGHT 1\nDEPTH 4\nMAXVAL 255\n"
                b"TUPLTYPE RGB_ALPHA\nENDHDR\n" + rgba
            )
            self.assertEqual(rgba, visual_parity.read_image(pam).rgba)

    def test_truncated_ppm_reports_value_error(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "truncated.ppm"
            for payload in (b"P6", b"P6\n# unterminated", b"P6\n2 1\n"):
                path.write_bytes(payload)
                with self.subTest(payload=payload), self.assertRaises(ValueError):
                    visual_parity.read_image(path)

    def test_malformed_search_radius_returns_contract_error(self) -> None:
        for search_radius in (None, "2", True):
            contract = {
                "schema_version": 1,
                "profiles": {
                    "graphics": {
                        "search_radius": search_radius,
                        "bad_channel_threshold": 0,
                        "max_mean_delta": 0.0,
                        "max_bad_pixel_ratio": 0.0,
                    }
                },
                "scenes": [{
                    "id": "sample",
                    "required_platforms": ["android", "ios", "desktop"],
                    "samples": [{"id": "t0000"}],
                    "viewports": [{
                        "id": "portrait",
                        "width": 1,
                        "height": 1,
                        "regions": [{"id": "all", "profile": "graphics"}],
                    }],
                }],
            }
            with self.subTest(search_radius=search_radius):
                errors = visual_parity.validate_contract(contract)
                self.assertIn(
                    "profile graphics search_radius must be between 0 and 4", errors
                )

    def test_metadata_crop_and_resize_are_deterministic(self) -> None:
        rgba = bytes((
            255, 0, 0, 255, 0, 255, 0, 255,
            0, 0, 255, 255, 255, 255, 255, 255,
        ))
        image = visual_parity.Image(2, 2, rgba)
        cropped = visual_parity.crop(image, [1, 0, 1, 2])
        self.assertEqual(bytes((0, 255, 0, 255, 255, 255, 255, 255)), cropped.rgba)
        resized = visual_parity.resize_bilinear(cropped, 1, 1)
        self.assertEqual(bytes((128, 255, 128, 255)), resized.rgba)

    def test_fuzzy_region_accepts_one_pixel_edge_shift(self) -> None:
        width = 8
        height = 4
        reference = bytearray(bytes((0, 0, 0, 255)) * width * height)
        actual = bytearray(reference)
        for y in range(height):
            reference[(y * width + 3) * 4:(y * width + 3) * 4 + 3] = b"\xff\xff\xff"
            actual[(y * width + 4) * 4:(y * width + 4) * 4 + 3] = b"\xff\xff\xff"
        profile = {
            "search_radius": 1,
            "bad_channel_threshold": 4,
            "max_mean_delta": 1.0,
            "max_bad_pixel_ratio": 0.01,
        }
        result = visual_parity.compare_region(
            visual_parity.Image(width, height, bytes(reference)),
            visual_parity.Image(width, height, bytes(actual)),
            [0.0, 0.0, 1.0, 1.0], profile,
        )
        self.assertEqual("PASS", result["status"])

    def test_fuzzy_region_rejects_large_color_regression(self) -> None:
        width = 6
        height = 6
        reference = visual_parity.Image(width, height, bytes((0, 0, 0, 255)) * width * height)
        actual = visual_parity.Image(width, height, bytes((255, 0, 0, 255)) * width * height)
        profile = {
            "search_radius": 1,
            "bad_channel_threshold": 16,
            "max_mean_delta": 2.0,
            "max_bad_pixel_ratio": 0.03,
        }
        result = visual_parity.compare_region(
            reference, actual, [0.0, 0.0, 1.0, 1.0], profile
        )
        self.assertEqual("FAIL", result["status"])
        self.assertEqual(1.0, result["bad_pixel_ratio"])

    def test_matrix_requires_and_compares_every_platform(self) -> None:
        contract = {
            "schema_version": 1,
            "profiles": {
                "graphics": {
                    "search_radius": 0,
                    "bad_channel_threshold": 0,
                    "max_mean_delta": 0.0,
                    "max_bad_pixel_ratio": 0.0,
                }
            },
            "scenes": [{
                "id": "sample",
                "version": 1,
                "required_platforms": ["android", "ios", "desktop"],
                "samples": [{"id": "t0000", "time_seconds": 0.0}],
                "viewports": [
                    {"id": "landscape", "width": 2, "height": 1,
                     "regions": [{"id": "all", "rect": [0, 0, 1, 1],
                                  "profile": "graphics"}]},
                    {"id": "portrait", "width": 1, "height": 2,
                     "regions": [{"id": "all", "rect": [0, 0, 1, 1],
                                  "profile": "graphics"}]},
                ],
            }],
        }
        rgba = bytes((10, 20, 30, 255)) * 2
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            contract_path = root / "contract.json"
            contract_path.write_text(json.dumps(contract), encoding="utf-8")
            captures = root / "captures"
            for platform in ("android", "ios", "desktop"):
                for viewport in ("landscape", "portrait"):
                    write_pam(captures / platform / "sample" / viewport / "t0000.pam",
                              2 if viewport == "landscape" else 1,
                              1 if viewport == "landscape" else 2, rgba)
            output = root / "out"
            with contextlib.redirect_stdout(io.StringIO()):
                result = visual_parity.matrix_command(types.SimpleNamespace(
                    contract=contract_path,
                    captures=captures,
                    output=output,
                    reference_platform="desktop",
                ))
            self.assertEqual(0, result)
            summary = json.loads((output / "summary.json").read_text(encoding="utf-8"))
            self.assertEqual(4, summary["comparisons"])
            self.assertEqual("PASS", summary["status"])


if __name__ == "__main__":
    unittest.main()

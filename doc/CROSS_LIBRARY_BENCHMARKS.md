# Cross-Library Benchmark Contract

WhatsCanvas includes an executable contract for comparing complete-frame 2D
rendering performance without rewarding lower output quality. The runner first
captures every adapter, checks its pixels against a designated reference, and
reports timing only beside the quality result.

The machine-readable contract is
[`benchmarks/cross_library/contract.json`](../benchmarks/cross_library/contract.json).
It fixes the 1920 x 1080 default, scene operations, assets, blending and
sampling requirements, synchronization scope, and per-scene quality limits.

## Contract scenes

| Scene | Workload | Fixed input |
| --- | --- | --- |
| `geometry_stress` | 2,304 mixed anti-aliased shapes | Deterministic geometry and colors |
| `image_grid` | 96 scaled and clipped image draws | Procedural 128 x 128 straight-alpha RGBA8 image |
| `contract_text_latin` | 576 shaped Latin text calls | Vendored `Roboto-Regular.ttf`, grayscale AA |

These scenes are intentionally a focused common denominator. Library-specific
effects remain in the native WhatsCanvas suite and must not be replaced with a
cheaper approximation in a cross-library result.

## Adapter interface

An adapter is an optimized executable that accepts the same command-line
arguments as `WhatsCanvasPerformanceSuite`:

```text
--profile quick|standard|thorough
--scene SCENE
--width PIXELS
--height PIXELS
--output RESULT.jsonl
--capture-dir DIRECTORY
```

The JSONL output must contain one `metadata` record and one `result` record.
Metadata must identify the library, version, backend, build type, dimensions,
sample counts, contract version, and synchronization mode. The required
`synchronization` value is `gpu_complete`; asynchronous submit-only timing and
Debug builds are rejected. The result must include `scene`,
`total_median_ms`, `total_p95_ms`, and a non-empty `pixel_hash`.

The capture is binary RGB8 PPM at:

```text
CAPTURE_DIRECTORY/BACKEND_SCENE.ppm
```

Adapters receive the absolute contract path in
`WHATSCANVAS_CROSS_LIBRARY_CONTRACT`. They should load the declared font and
procedural image definition from that contract rather than substituting system
assets.

## Run a comparison

Build WhatsCanvas in Release, then provide one reference and any number of
candidate adapters:

```powershell
python scripts\cross_library_benchmark.py `
  --reference "whatscanvas-gl=build/Release/WhatsCanvasPerformanceSuite.exe --backend opengl" `
  --adapter "other-gl=C:/renderers/other_adapter.exe --backend opengl" `
  --profile standard `
  --output-dir build/cross-library-results
```

The runner executes each scene in a fresh process. It rejects mismatched
resolution, profile, warmup, measured-frame count, contract version, and
synchronization. It then computes RGB mean absolute error, RMSE, maximum
channel delta, and the fraction of pixels exceeding the scene's channel
threshold.

Text thresholds allow normal grayscale rasterizer and kerning differences
between FreeType/HarfBuzz and stb/fontstash implementations. They remain below
the measured error of an empty background-only capture, so omitting the text
still fails the quality gate.

Exit code `0` means every candidate passed its quality gates. Exit code `2`
means execution was valid but at least one image failed quality. Other nonzero
codes indicate an invalid adapter, missing output, or execution failure.

## NanoVG adapter

NanoVG is the first checked adapter implementation. Its upstream source remains
an external checkout and is never copied into WhatsCanvas:

```powershell
git clone https://github.com/memononen/nanovg .nanovg
cmake -S . -B build `
  -DWHATSCANVAS_BUILD_NANOVG_BENCHMARK_ADAPTER=ON `
  -DWHATSCANVAS_NANOVG_SOURCE_DIR="$PWD/.nanovg"
cmake --build build --config Release `
  --target WhatsCanvasPerformanceSuite WhatsCanvasNanoVGBenchmarkAdapter
python scripts/cross_library_benchmark.py `
  --reference "whatscanvas=build/Release/WhatsCanvasPerformanceSuite.exe --backend opengl" `
  --adapter "nanovg=build/Release/WhatsCanvasNanoVGBenchmarkAdapter.exe --backend opengl" `
  --profile standard `
  --output-dir build/cross-library-nanovg
```

The adapter uses NanoVG GL3 with geometry antialiasing and stencil strokes. It
does not enable NanoVG debug checks or window MSAA. Both sides use the same
hidden OpenGL 3.3 framebuffer and wait for `glFinish`.

The checked-in
[Windows i7-8700 / GTX 1060 NanoVG comparison](../benchmarks/baselines/cross-library-nanovg-windows-i7-8700-gtx1060/README.md)
preserves three independent Standard runs and all raw JSONL records.

## Publishing results

A reviewable comparison includes:

- adapter source and exact dependency revisions;
- compiler, optimization flags, device, driver, and operating system;
- raw JSONL records and PPM captures;
- the generated `cross-library-report.md`;
- unsupported scenes explicitly marked as unsupported.

Use the same physical machine, power state, API/backend class, pixel dimensions,
and standard 30 + 5 profile. Relative timing is candidate median divided by
reference median, so lower is faster. A failed quality gate invalidates that
timing comparison.

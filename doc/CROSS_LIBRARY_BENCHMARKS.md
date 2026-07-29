# Cross-Library Benchmark Contract

WhatsCanvas includes an executable contract for comparing complete-frame 2D
rendering performance without rewarding lower output quality. The runner first
captures every adapter, checks its pixels against a designated reference, and
reports timing only beside the quality result.

The machine-readable contract is
[`benchmarks/cross_library/contract.json`](../benchmarks/cross_library/contract.json).
It fixes the 1920 x 1080 default, scene operations, assets, font SHA-256,
text sizing/baseline semantics, full-frame draw clear, blending and sampling
requirements, synchronization scope, and per-scene quality limits.

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
--frames COUNT
--warmup COUNT
--workload fixed|stable|dynamic-data|dynamic-structure
--operations COUNT
--seed VALUE
--texture-count COUNT
--rounded-ratio 0..1
--state-change-rate 0..1
--text-length COUNT
--output RESULT.jsonl
--capture-dir DIRECTORY
```

The JSONL output must contain one `metadata` record and one `result` record.
Metadata must identify the library, version, backend, build type, dimensions,
sample counts, contract version, clear/text contract fields, parameterized
workload fields, and synchronization mode. The required
`synchronization` value is `gpu_complete`; asynchronous submit-only timing and
Debug builds are rejected. The result must include `scene`,
`total_median_ms`, `total_p95_ms`, a non-empty `pixel_hash`, and complete
`record_samples_ms`, `submit_samples_ms`, and `total_samples_ms` arrays whose
length equals the measured frame count.

The capture is binary RGB8 PPM at:

```text
CAPTURE_DIRECTORY/BACKEND_SCENE.ppm
```

Adapters receive the absolute contract path, contract version, and verified
font digest in `WHATSCANVAS_CROSS_LIBRARY_CONTRACT`,
`WHATSCANVAS_CROSS_LIBRARY_CONTRACT_VERSION`, and
`WHATSCANVAS_CROSS_LIBRARY_FONT_SHA256`. The verified absolute font path is
passed in `WHATSCANVAS_CROSS_LIBRARY_FONT_PATH`. Adapters must use the declared
font and procedural image definition rather than substituting system assets.

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

For every reference/candidate pair, the runner defaults to four independent
ABBA blocks: `reference, candidate, candidate, reference`. This produces eight
fresh processes per renderer while balancing startup order and thermal drift.
It rejects mismatched resolution, profile, warmup, measured-frame count,
workload parameters, contract version, font hash, clear/text semantics, and
synchronization. Reference and candidate validation hashes must each remain
stable across fresh processes. It computes RGB mean absolute error, RMSE,
maximum channel delta, and the fraction of pixels exceeding the scene's
channel threshold.
The report publishes deterministic bootstrap 95% confidence intervals for
fresh-process medians and within-block candidate/reference ratios.

Text thresholds allow normal grayscale rasterizer and kerning differences
between FreeType/HarfBuzz and stb/fontstash implementations. They remain below
the measured error of an empty background-only capture, so omitting the text
still fails the quality gate.

Parameterized runs use the same random function and option semantics in the
WhatsCanvas and NanoVG adapters. For example:

```powershell
python scripts/cross_library_benchmark.py `
  --reference "whatscanvas=build/Release/WhatsCanvasPerformanceSuite.exe --backend opengl" `
  --adapter "nanovg=build/Release/WhatsCanvasNanoVGBenchmarkAdapter.exe --backend opengl" `
  --scene geometry_stress `
  --workload dynamic-structure `
  --operations 4096 `
  --seed 7 `
  --state-change-rate 0.0625 `
  --output-dir build/cross-library-geometry-dynamic
```

To test a range instead of one hand-picked workload, run the parameter matrix:

```powershell
python scripts/run_cross_library_matrix.py `
  --reference "whatscanvas=build/Release/WhatsCanvasPerformanceSuite.exe --backend opengl" `
  --adapter "nanovg=build/Release/WhatsCanvasNanoVGBenchmarkAdapter.exe --backend opengl" `
  --preset standard --profile standard `
  --repetitions 4 `
  --output-dir build/cross-library-matrix
```

The standard preset evaluates 27 quality-gated cells across geometry, images,
and text; three operation scales; and stable, dynamic-data, and
dynamic-structure modes. Each cell gets its own ABBA schedule and bootstrap
confidence interval. `--seeds` adds deterministic content variants, while
`--repetitions` independently increases fresh-process samples. Use
`--dry-run` to audit the complete command list and expected process count.

Fixed contract scenes use absolute pixel-error limits. Parameterized image and
text scenes instead normalize MAE and RMSE against the reference image's
distance from the declared solid background. This keeps the gate meaningful as
draw density changes: normal rasterizer and edge-antialiasing differences scale
with visible content. Text also requires the candidate's own ink signal to stay
within a bounded fraction of the reference signal. A blank renderer has zero
candidate ink and fails even where large-glyph rasterizer differences require
an error allowance near `1.0x`.

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
hidden OpenGL 3.3 framebuffer and wait for `glFinish`. Both record the opaque
background as a measured full-frame src-over draw; NanoVG's stencil maintenance
clear is excluded from the Canvas workload interval.

The checked-in
[Windows i7-8700 / GTX 1060 ABBA comparison](../benchmarks/baselines/cross-library-nanovg-abba-windows-i7-8700-gtx1060/README.md)
preserves all 48 Standard-run JSONL records, including every measured frame,
plus the machine-readable summary and report. The
[earlier three-run baseline](../benchmarks/baselines/cross-library-nanovg-windows-i7-8700-gtx1060/README.md)
remains available as optimization history.

## Publishing results

A reviewable comparison includes:

- adapter source and exact dependency revisions;
- compiler, optimization flags, device, driver, and operating system;
- raw JSONL records and PPM captures;
- the generated `cross-library-report.md`;
- the generated `cross-library-summary.json`;
- unsupported scenes explicitly marked as unsupported.

Use the same physical machine, power state, API/backend class, pixel dimensions,
and standard 30 + 5 profile. Relative timing is the candidate/reference
geometric-mean ratio inside each ABBA block, so lower is faster. A failed
quality gate invalidates that timing comparison.

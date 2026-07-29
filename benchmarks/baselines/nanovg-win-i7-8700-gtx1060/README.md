# WhatsCanvas / NanoVG Parameter Matrix

This baseline extends the fixed Contract 1.2.0 comparison across operation
scale and frame-to-frame change modes. It is intended to expose optimizations
that only work for one object count or a fully stable command stream.

## Environment

- Windows 10, Intel Core i7-8700, NVIDIA GeForce GTX 1060 3GB
- NVIDIA driver 560.94, OpenGL 3.3
- MSVC 19.43, `Release`, 1920 x 1080
- 5 warmup frames and 30 measured frames per process
- 2 ABBA blocks and 4 fresh processes per renderer per matrix cell
- 10,000 deterministic bootstrap resamples
- Workload seed `1001`
- NanoVG `ce3bf745eb2d2dbc14a50bf2446783f691ac4353`

## Coverage

The standard preset contains 27 cells: geometry, image, and text scenes; three
operation scales per scene; and stable, dynamic-data, and dynamic-structure
modes. All 27 cells passed their parameterized pixel-quality gate.

| Scene | Cells | WhatsCanvas faster | NanoVG faster | Inconclusive | Median NanoVG / WhatsCanvas ratio |
| --- | ---: | ---: | ---: | ---: | ---: |
| `geometry_stress` | 9 | 2 | 6 | 1 | 0.733x |
| `image_grid` | 9 | 3 | 6 | 0 | 0.471x |
| `contract_text_latin` | 9 | 7 | 0 | 2 | 1.083x |
| **Total** | **27** | **12** | **12** | **3** | - |

A win requires the complete paired-ratio 95% confidence interval to remain on
one side of `1.0`. Ratios above `1.0` favor WhatsCanvas.

## Interpretation

WhatsCanvas is consistently faster for stable single-texture image streams and
for medium/high-density text, including dynamic-structure text. NanoVG remains
faster for image streams that change texture cardinality, rounded coverage, or
blend state, and for medium/high-scale geometry whose data or topology changes.
At 256 and 1,024 dynamic-data image operations, NanoVG takes about 35% and 40%
of WhatsCanvas time respectively. This identifies dynamic resource/state
handling and large geometry recording as the next optimization targets.

These results use one deterministic content seed and two ABBA blocks. They are
stronger evidence of workload breadth than a fixed object count, but are not a
cross-hardware ranking. Use more seeds and `--repetitions 8` for release
publication on additional machines.

## Artifacts

- `matrix-summary.json`, `matrix-summary.csv`, and `matrix-report.md` contain
  all aggregate cells and confidence verdicts.
- Every case directory contains its ABBA order, quality metrics, confidence
  intervals, and eight raw JSONL files with all measured frame samples.
- PPM captures are omitted from Git because 216 uncompressed 1080p images
  require roughly 1.3 GiB. They were generated and checked during this run.

```powershell
python scripts/run_cross_library_matrix.py `
  --reference "whatscanvas=build/Release/WhatsCanvasPerformanceSuite.exe --backend opengl" `
  --adapter "nanovg=build/Release/WhatsCanvasNanoVGBenchmarkAdapter.exe --backend opengl" `
  --preset standard --profile standard `
  --repetitions 4 --bootstrap-samples 10000 `
  --output-dir build/cross-library-matrix-standard
```

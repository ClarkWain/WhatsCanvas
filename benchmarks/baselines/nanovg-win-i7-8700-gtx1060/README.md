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
| `geometry_stress` | 9 | 8 | 0 | 1 | 1.340x |
| `image_grid` | 9 | 9 | 0 | 0 | 1.411x |
| `contract_text_latin` | 9 | 9 | 0 | 0 | 1.200x |
| **Total** | **27** | **26** | **0** | **1** | - |

A win requires the complete paired-ratio 95% confidence interval to remain on
one side of `1.0`. Ratios above `1.0` favor WhatsCanvas.

## Interpretation

WhatsCanvas is conclusively faster in all nine image cells, all nine text
cells, and eight of nine geometry cells. The 256-operation
dynamic-structure geometry cell is statistically inconclusive at this
repetition count. The matrix changes scale, content, texture cardinality,
rounded coverage, topology, and blend state, so the result is not tied to one
fixed object count or an entirely stable command stream.

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

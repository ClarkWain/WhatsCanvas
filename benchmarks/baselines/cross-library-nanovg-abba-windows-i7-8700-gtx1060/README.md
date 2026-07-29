# WhatsCanvas / NanoVG ABBA Baseline

This is the first Contract 1.2.0 baseline produced by the automated
cross-library runner. It records synchronized complete-frame timing only after
the candidate passes the scene quality gate.

## Environment

- Windows 10, Intel Core i7-8700, NVIDIA GeForce GTX 1060 3GB
- NVIDIA driver 560.94, OpenGL 3.3
- MSVC 19.43, `Release`, 1920 x 1080
- 5 warmup frames and 30 measured frames per process
- 4 ABBA blocks, 8 fresh processes per renderer and scene
- 10,000 deterministic bootstrap resamples
- NanoVG `ce3bf745eb2d2dbc14a50bf2446783f691ac4353`
- Roboto SHA-256 `797E35F7F5D6020A5C6EA13B42ECD668BCFB3BBC4BAA0E74773527E5B6CB3174`

## Results

| Scene | WhatsCanvas OpenGL median (95% CI) | NanoVG GL3 median (95% CI) | Paired NanoVG / WhatsCanvas (95% CI) |
| --- | ---: | ---: | ---: |
| `geometry_stress` | **2.617 ms** (2.572-2.764) | 3.705 ms (3.605-3.807) | 1.407x (1.273-1.441) |
| `image_grid` | **0.272 ms** (0.262-0.304) | 0.383 ms (0.380-0.388) | 1.369x (1.347-1.417) |
| `contract_text_latin` | **2.878 ms** (2.670-3.021) | 3.292 ms (3.255-3.337) | 1.153x (1.105-1.169) |

All 48 process runs passed their quality gate. Every JSONL file preserves all
30 record, submit, and total frame samples. `cross-library-summary.json`
contains the ABBA order, process medians, paired ratios, confidence intervals,
quality metrics, and relative paths to every raw result.

PPM captures are intentionally not committed because 48 uncompressed 1080p
files would add about 285 MiB. The runner generated and validated every capture
during this run; rerunning the documented command reproduces them locally.

```powershell
python scripts/cross_library_benchmark.py `
  --reference "whatscanvas=build/Release/WhatsCanvasPerformanceSuite.exe --backend opengl" `
  --adapter "nanovg=build/Release/WhatsCanvasNanoVGBenchmarkAdapter.exe --backend opengl" `
  --profile standard `
  --repetitions 8 `
  --bootstrap-samples 10000 `
  --output-dir build/cross-library-abba
```

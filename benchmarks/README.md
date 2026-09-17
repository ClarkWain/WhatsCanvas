# WhatsCanvas Benchmarks

This directory contains repeatable performance workloads. Always build and
publish benchmark results from an optimized `Release` configuration.

## Benchmark targets

- `WhatsCanvasPerformanceSuite`: unified frame workloads for Software, OpenGL,
  and Vulkan. Its 13 scenes default to 1920 x 1080 and include dense
  multilingual text and mixed-geometry stress workloads. This is the primary
  performance-evaluation entry point.
- `WhatsCanvasCoreBenchmarks`: CPU microbenchmarks for layout, shaping,
  rasterization, path metrics, command recording, and other isolated costs.
- `WhatsCanvasImageFilterBenchmarks`: focused end-to-end frosted-glass and
  inner-shadow filter measurements retained for filter tuning.

The unified suite writes one metadata record followed by one result record per
scene in JSONL. It reports cold-frame time, record/submit/total distributions,
FPS, operation throughput, readback latency, pixel hash, process memory, and
public `RenderStats`.

Use the platform runner for comparable results. It starts every scene in a
fresh process so caches and process high-water memory from one workload do not
contaminate the next:

```powershell
cmake --build build --config Release --target WhatsCanvasPerformanceSuite
.\scripts\run_performance_suite.ps1 `
  -Profile standard -Backends software,opengl,vulkan
```

```sh
cmake --build build --config Release --target WhatsCanvasPerformanceSuite
PROFILE=standard BACKENDS="software opengl vulkan" \
  ./scripts/run_performance_suite.sh
```

See [Performance Benchmarks](../doc/public/performance/PERFORMANCE_BENCHMARKS.md) for the scene
matrix, metric definitions, `--summary` report generation, revision comparison,
and publication rules.

The checked-in
[Windows i7-8700 / GTX 1060 reference run](baselines/windows-i7-8700-gtx1060/README.md)
contains the generated report and all 33 raw JSONL records. It is a historical
960 x 540 reference from the earlier 11-scene suite and must not be compared
directly with the current 1080p default; create a fresh matching baseline for
timing verdicts.

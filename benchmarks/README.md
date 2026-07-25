# WhatsCanvas Benchmarks

This directory is reserved for repeatable local and CI benchmark assets.

## Current Signals

- `build.bat` / `build.sh` emit structured `BUILD_*` timings.
- `scripts/smoke_test.bat` / `scripts/smoke_test.sh` emit structured `SMOKE_*` timings.
- `scripts/examples_smoke.bat` / `scripts/examples_smoke.sh` emit structured `EXAMPLES_SMOKE_*` timings.

## Intended Growth

- record-time benchmarks for heavy path, text, and image scenes;
- submit-time benchmarks for layer-heavy and clip-heavy workloads;
- state-change and draw-call counters per scene;
- readback and saveLayer latency tracking;
- stable benchmark output suitable for local history comparison and future CI reporting.

`WhatsCanvasCoreBenchmarks` now includes
`software_backdrop_blur_320x180_r24`, a repeatable real-filter workload that
reports elapsed time together with filter pass and pixel-pass diagnostics.

`WhatsCanvasImageFilterBenchmarks` is the dedicated end-to-end filter harness.
It renders four sequentially overlapping frosted-glass panels and a grid of 24
inner-shadow controls through Software, OpenGL, or Vulkan:

```sh
./build/WhatsCanvasImageFilterBenchmarks \
  --backend software --warmup 3 --frames 10 --width 960 --height 540
```

Every workload emits one `FILTER_BENCHMARK` line containing median, p95, min,
max, FPS, output hash, filter/pass counts, downsample count, and pixel-work
metrics. GPU completion is included in OpenGL timings; pixel readback and
hashing are excluded. CI treats the short Software run as a functional smoke
test and intentionally does not enforce machine-dependent timing thresholds.

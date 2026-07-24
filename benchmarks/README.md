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

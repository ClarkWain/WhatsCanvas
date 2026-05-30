# PrismCanvas Benchmarks

This directory is reserved for repeatable local and CI benchmark assets.

## Current Signals

- `build.bat` / `build.sh` emit structured `BUILD_*` timings.
- `smoke_test.bat` / `smoke_test.sh` emit structured `SMOKE_*` timings.
- `examples_smoke.bat` / `examples_smoke.sh` emit structured `EXAMPLES_SMOKE_*` timings.

## Intended Growth

- record-time benchmarks for heavy path, text, and image scenes;
- submit-time benchmarks for layer-heavy and clip-heavy workloads;
- state-change and draw-call counters per scene;
- readback and saveLayer latency tracking;
- stable benchmark output suitable for local history comparison and future CI reporting.
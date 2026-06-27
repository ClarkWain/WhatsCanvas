# WhatsCanvas Performance Benchmarks

WhatsCanvas now includes a lightweight benchmark executable for core CPU-side costs:

```sh
cmake --build build --target WhatsCanvasCoreBenchmarks --config Release
WHATSCANVAS_BENCHMARK_ITERATIONS=10000 ./build/WhatsCanvasCoreBenchmarks
```

On Windows Debug builds:

```powershell
cmake --build build --target WhatsCanvasCoreBenchmarks --config Debug
$env:WHATSCANVAS_BENCHMARK_ITERATIONS = "10000"
build\Debug\WhatsCanvasCoreBenchmarks.exe
```

## Current Metrics

`WhatsCanvasCoreBenchmarks` prints machine-readable lines with `BENCHMARK` and `BENCHMARK_DETAIL` prefixes.

Current benchmark cases:

- `text_layout`: bounded multiline layout with line height and ellipsis.
- `path_metrics`: path length and midpoint queries over a mixed curve path.
- `pixel_hash_rgba_800x600`: RGBA framebuffer hash helper over an 800x600 buffer.
- `command_record_rect`: CPU-side rectangle command recording without flushing a GL context.

## Scope

This benchmark intentionally avoids requiring a live OpenGL context. That keeps it useful on build machines and during API work.

The remaining GPU-sensitive benchmark targets still need dedicated harnesses:

- glyph cache hit rate once glyph atlas ownership lands
- image upload cost with a current GL/GLES context
- frame flush cost with real draw execution
- backend-specific draw-call counters under stable validation scenes

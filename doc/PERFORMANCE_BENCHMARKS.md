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
- `text_cache_hit_path`: repeated text render calls over the cached backend path.
- `font_glyph_metrics_cache`: registered-font glyph metric lookup with loaded-face cache stats.
- `font_glyph_rasterize`: registered-font glyph rasterization cost with loaded-face cache stats.
- `portable_glyph_atlas_text`: portable text shaping/rasterization/upload planning through the CPU glyph atlas.
- `path_metrics`: path length and midpoint queries over a mixed curve path.
- `pixel_hash_rgba_800x600`: RGBA framebuffer hash helper over an 800x600 buffer.
- `command_record_rect`: CPU-side rectangle command recording without flushing a GL context.
- `image_upload_rgba_64x64`: backend-neutral image resource creation/update cost through the renderer abstraction.
- `frame_flush_single_rect`: command flush overhead with a lightweight renderer.
- `software_backdrop_blur_320x180_r24`: real separable-Gaussian backdrop blur
  through the Software backend, including filter pass and pixel-pass details.

## Scope

This benchmark intentionally avoids requiring a live OpenGL context. That keeps it useful on build machines and during API work.

The remaining GPU-sensitive benchmark targets still need dedicated harnesses:

- image upload cost with a current GL/GLES context
- frame flush cost with real draw execution
- backend-specific draw-call counters under stable validation scenes

`WhatsCanvasImageFilterShowcase` complements the headless benchmark with a real
OpenGL framebuffer workload. Its diagnostic line includes filter executions,
passes, downsample hits, and pixel-pass work for local GPU profiling.

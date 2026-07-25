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

## Image-filter frame benchmarks

`WhatsCanvasImageFilterBenchmarks` measures complete filter frames:

```sh
cmake --build build --target WhatsCanvasImageFilterBenchmarks --config Release
./build/WhatsCanvasImageFilterBenchmarks \
  --backend software --warmup 3 --frames 10 --width 960 --height 540
```

On a multi-config Windows build, run
`build\Release\WhatsCanvasImageFilterBenchmarks.exe`.

The supported backends are `software`, `opengl`, and `vulkan` when the
corresponding backend was compiled and is available on the host. The executable
runs two fixed workloads:

- `overlapping_frosted_glass`: four separately captured, overlapping backdrop
  layers with deterministic zero-grain frosted glass.
- `inner_shadow_grid`: 24 independently filtered controls.

Each `FILTER_BENCHMARK` line includes `median_ms`, `p95_ms`, `min_ms`,
`max_ms`, `fps`, a pixel `hash`, and the public filter/pass/downsample/pixel-work
statistics. `beginFrame`, drawing, `endFrame`, and OpenGL `glFinish` are timed;
readback and hashing are not.

CI runs a small Software smoke workload to catch broken benchmark wiring. It
does not compare elapsed time because shared hosted runners are not stable
performance machines. Use identical hardware, build type, dimensions, backend,
warmup, and frame count for local trend comparisons.

## Scope

`WhatsCanvasCoreBenchmarks` intentionally avoids a live graphics context.
`WhatsCanvasImageFilterBenchmarks` supplies the real Software/OpenGL/Vulkan
filter path. Dedicated image-upload and presentation latency harnesses remain
future work.

`WhatsCanvasImageFilterShowcase` complements the headless benchmark with a real
OpenGL framebuffer workload. Its diagnostic line includes filter executions,
passes, downsample hits, and pixel-pass work for local GPU profiling.

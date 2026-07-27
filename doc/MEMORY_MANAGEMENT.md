# Memory Management

WhatsCanvas bounds its main retained CPU and GPU caches and exposes their
estimated footprint through `Canvas::RenderStats`. These limits control
long-running, content-heavy applications without requiring callers to manage
backend resources directly.

## Retained resource budgets

| Resource | Default bound | Policy |
| --- | ---: | --- |
| Fill tessellation cache | 8 MiB, 256 entries | LRU by retained vector capacity |
| Stroke tessellation cache | 8 MiB, 256 entries | LRU by retained vector capacity |
| GPU bitmap-text cache | 32 MiB, 256 entries | LRU by uploaded RGBA byte count |
| Render-target pool | 32 MiB, 12 targets | Oldest idle target is evicted first |
| Portable glyph atlas | 2048 x 2048 initially, up to 4096 x 4096 | Grows on demand and rebuilds retained glyphs |

An oversized tessellation can remain as the cache's sole entry because the
current draw needs a stable reference. A bitmap-text texture or render target
larger than its entire budget is used but not retained. The render-target byte
estimate is conservative across backends because attachment formats and driver
allocation details differ.

## Runtime diagnostics

Call `Canvas::getRenderStats()` after a frame. The memory-related fields are:

- `glyphAtlasTextureCount` and `glyphAtlasTextureBytes`
- `pooledRenderTargetCount` and `pooledRenderTargetBytes`
- `renderTargetPoolReuseCount`, `renderTargetPoolAllocationCount`, and
  `renderTargetPoolEvictionCount`
- `tessellationCacheBytes` and `strokeTessellationCacheBytes`
- `bitmapTextCacheSize` and `bitmapTextCacheBytes`

The values describe resources retained by WhatsCanvas. They are not process
RSS or exact driver heap measurements. Use platform profilers for allocator
overhead, driver-internal allocations, mapped memory, and fragmentation.

## Temporary working memory

The software Gaussian blur uses two floating-point working images. The vertical
pass writes directly to the final RGBA8 destination, avoiding a third
full-resolution floating-point image. Inner-shadow filtering follows the same
two-buffer strategy for alpha coverage.

OpenGL and Vulkan image filters reuse temporary render targets. The pool keeps
targets for 64 usage cycles so multiple filters in one frame do not age each
other out, while the byte and target-count budgets remain hard upper bounds.
Pooled targets are cleared before the backend device or graphics context is
released.

## Measuring changes

Build and run the end-to-end filter benchmark with identical backend, build
type, dimensions, warmup, and frame count:

```sh
cmake --build build --target WhatsCanvasImageFilterBenchmarks --config Release
./build/WhatsCanvasImageFilterBenchmarks \
  --backend software --warmup 3 --frames 10 --width 1920 --height 1080
```

Each `FILTER_BENCHMARK` line includes a deterministic output hash, pooled
render-target diagnostics, and retained cache estimates. Compare the hash
before comparing timing or process memory so a visual regression cannot be
mistaken for an optimization.

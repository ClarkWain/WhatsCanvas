# WhatsCanvas Performance Evaluation

Performance claims need reproducible evidence. WhatsCanvas therefore separates
microbenchmarks, complete-frame measurements, rendering validation, and
environment metadata instead of presenting one favorable FPS number.

## Evaluation layers

| Layer | Target | Question answered |
| --- | --- | --- |
| CPU micro | `WhatsCanvasCoreBenchmarks` | Which isolated layout, font, path, recording, upload, or filter operation changed? |
| Complete frame | `WhatsCanvasPerformanceSuite` | How long does a representative frame take on Software, OpenGL, or Vulkan? |
| Focused filter | `WhatsCanvasImageFilterBenchmarks` | What is the cost and pixel workload of frosted glass and inner shadow? |
| Evidence | JSONL + pixel hash + memory + environment | Can another developer reproduce and audit the result? |

The complete-frame suite is the primary public benchmark. The smaller targets
remain useful when a frame regression needs to be localized.

A checked-in [Windows i7-8700 / GTX 1060 reference run](../benchmarks/baselines/windows-i7-8700-gtx1060/README.md)
demonstrates the complete report format and preserves all raw JSONL records.
It is a reproducible single-machine baseline, not a universal score or
cross-library ranking.

## Standard scene matrix

The default resolution is 960 x 540. Every scene is deterministic and produces
a fixed validation-frame hash after timing has completed.

| Scene | Coverage | Cache mode | Operations/frame |
| --- | --- | --- | ---: |
| `solid_rects` | Dense filled rectangles and command submission | churn | 576 |
| `rounded_ui` | Rounded UI surfaces and anti-aliased edges | churn | 120 |
| `path_cached` | Repeated complex path geometry | hot | 160 |
| `path_churn` | Per-frame path construction and tessellation | churn | 160 |
| `image_grid` | Reused RGBA texture scaling and sampling | hot | 96 |
| `clip_layers` | Nested clips, transforms, and layers | churn | 144 |
| `shadow_grid` | Shape shadows with varied radii | churn | 36 |
| `text_cached` | Repeated shaped text and glyph-atlas reuse | hot | 120 |
| `text_churn` | Changing text content and glyph lookup pressure | churn | 120 |
| `frosted_glass` | Backdrop capture, blur, and layer composition | hot | 4 |
| `inner_shadow` | Filtered controls with inner shadows | hot | 24 |

Hot and churn variants are deliberate. A renderer should show both steady-state
cache efficiency and the cost of changing content.

## Profiles

| Profile | Timed frames | Warmup frames | Intended use |
| --- | ---: | ---: | --- |
| `quick` | 3 | 1 | Build/schema/readback smoke only |
| `standard` | 30 | 5 | Normal local comparison and reference reports |
| `thorough` | 120 | 20 | Stable release investigation on dedicated hardware |

Each scene also records one pre-warmup cold frame. For official runs, the
provided runner launches each scene in a fresh process. This prevents a prior
text scene's glyph atlas, a prior path scene's caches, or process peak RSS from
changing the next scene's result.

## Metrics

- `record_*_ms`: `beginFrame` plus public Canvas draw calls.
- `submit_*_ms`: `endFrame` plus backend completion. OpenGL uses `glFinish`;
  Vulkan waits for its queue, so reported GPU time is not deferred work.
- `total_*_ms`: complete synchronized frame time.
- `median`, `p90`, `p95`, `p99`, `mean`, `min`, `max`, `stddev`, `cv`:
  distribution statistics over measured frames.
- `cold_total_ms`: first frame before warmup.
- `fps`: `1000 / total_median_ms`; this is throughput, not display refresh.
- `operations_per_second`: declared scene operations divided by median time.
- `readback_ms`: separately timed RGBA readback, excluded from frame timing.
- `pixel_hash`: fixed-frame RGBA hash used to detect missing or changed output.
- `rss_*`, `peak_rss_bytes`, `private_or_virtual_bytes`: process memory
  observations. The last field follows the closest portable OS counter and is
  deliberately not presented as identical private-memory semantics everywhere.
- `command_count`, `draw_call_count`, cache bytes, filter/pass/pixel counts,
  and render-target statistics: public `Canvas::RenderStats` diagnostics.

Pixel equality is not a perceptual quality score. A fast blank frame can be
fast for the wrong reason, so every result must have a non-empty readback and a
stable hash, while visual regression remains a separate quality gate.

## Build and run

Use `Release`; Debug results are not suitable for performance claims.

```powershell
cmake -S . -B build -DWHATSCANVAS_BUILD_BENCHMARKS=ON
cmake --build build --config Release --target WhatsCanvasPerformanceSuite
.\scripts\run_performance_suite.ps1 `
  -Profile standard `
  -Backends software,opengl,vulkan `
  -OutputDir build/performance-results
```

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DWHATSCANVAS_BUILD_BENCHMARKS=ON
cmake --build build --target WhatsCanvasPerformanceSuite
PROFILE=standard BACKENDS="software opengl vulkan" \
  OUTPUT_DIR=build/performance-results \
  ./scripts/run_performance_suite.sh
```

The runner defaults to Software because it is available on every supported
host. Request only GPU backends compiled into the current build. To run one
case directly:

```powershell
build\Release\WhatsCanvasPerformanceSuite.exe `
  --backend opengl --profile standard --scene text_cached `
  --output build\text-cached.jsonl `
  --capture-dir build\performance-captures
```

Use `--list-scenes` to inspect scene names. `--frames`, `--warmup`, `--width`,
and `--height` override a profile for investigation, but custom settings should
not be mixed into a standard reference report.

## Compare revisions

Capture baseline and candidate results on the same idle machine with the same
driver, power mode, build type, backend, dimensions, and profile:

```powershell
python scripts\compare_performance.py `
  build\perf-baseline build\perf-candidate `
  --regression-threshold 10 `
  --output build\performance-comparison.md
```

Optional gates:

```powershell
python scripts\compare_performance.py `
  build\perf-baseline build\perf-candidate `
  --regression-threshold 10 `
  --fail-on-regression --fail-on-hash-change
```

The comparison tool matches backend, scene, dimensions, and profile; reports
median, p95, peak RSS, and hash changes. It refuses a timing verdict when
machine, driver, compiler, build, dimensions, or sample settings differ.
`--allow-incompatible` is available only for explicitly exploratory reports.
Raw files can be checked independently:

```sh
python3 scripts/compare_performance.py --validate build/performance-results
```

A single run directory can be turned into a reviewable Markdown report without
manually selecting or copying numbers:

```sh
python3 scripts/compare_performance.py \
  --summary build/performance-results \
  --output build/performance-summary.md
```

## Cross-library comparisons

The JSONL schema is intentionally simple enough for an adapter implemented with
another 2D library. A fair adapter must render the same scene at the same
resolution with equivalent anti-aliasing, clipping, blending, text, filtering,
and synchronization. It must publish:

- adapter source and exact dependency revisions;
- optimized compiler flags and backend/device metadata;
- warmup and sample counts;
- raw JSONL, output images, and a description of unsupported operations;
- both quality evidence and timing, without silently simplifying a scene.

Do not compare raw draw-call counts across libraries unless an operation has the
same semantics. Do not compare asynchronous GPU submission against synchronized
complete-frame time. When an effect has no equivalent implementation, mark the
scene unsupported instead of substituting an easier effect.

## CI policy

Shared hosted runners execute the `quick` Software subset to validate build
wiring, JSON schema, pixel readback, and hashes. CI intentionally does not gate
elapsed time: noisy virtual machines are unsuitable performance laboratories.
Timing thresholds belong on stable, dedicated hardware or in controlled local
baseline/candidate runs.

## Existing microbenchmarks

`WhatsCanvasCoreBenchmarks` continues to cover text layout, text cache hits,
font glyph metrics and rasterization, portable glyph-atlas text, path metrics,
pixel hashing, command recording, image upload, lightweight frame flush, and a
Software backdrop-blur workload. Set `WHATSCANVAS_BENCHMARK_ITERATIONS` to
control its iteration count.

`WhatsCanvasImageFilterBenchmarks` continues to emit focused
`FILTER_BENCHMARK` records for overlapping frosted glass and inner-shadow
controls. Its timings include frame completion and exclude readback/hash work.

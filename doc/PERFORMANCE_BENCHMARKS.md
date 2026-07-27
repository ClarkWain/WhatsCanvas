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
cross-library ranking. That historical run used the former 960 x 540,
11-scene matrix, so it documents earlier work but is not dimension-compatible
with the current 1080p suite.

## Verified hotspot optimization

The checked-in reference run also exposed two genuine hotspots: rounded images
were routed through full clip masks, and Software/Vulkan shadows processed much
more of the canvas than the shadow could affect. The optimized implementation
uses native uniform-rounded image coverage, crops shadow work to expanded
silhouette bounds, keeps Vulkan blur on the GPU, defers temporary target
reclamation until GPU completion, and batches path-shadow silhouette passes.

The following candidate was rerun on 2026-07-26 on the exact reference machine
and driver, in `Release`, at 960 x 540 with the `standard` 30 + 5 frame profile.
The baseline remains checked in rather than being overwritten, so these deltas
remain auditable.

| Backend | Scene | Reference median | Optimized median | Delta |
| --- | --- | ---: | ---: | ---: |
| OpenGL | `image_grid` | 191.084 ms | 0.533 ms | -99.7% |
| Software | `image_grid` | 94.119 ms | 42.938 ms | -54.4% |
| Vulkan | `image_grid` | 47.344 ms | 0.290 ms | -99.4% |
| OpenGL | `shadow_grid` | 11.764 ms | 10.571 ms | -10.1% |
| Software | `shadow_grid` | 1003.717 ms | 70.411 ms | -93.0% |
| Vulkan | `shadow_grid` | 1129.557 ms | 14.947 ms | -98.7% |

These results do not show Vulkan as generally slower than OpenGL:
`image_grid` is faster on Vulkan in this run. The remaining `shadow_grid`
difference, 14.947 ms versus 10.571 ms, is concentrated in 36 small
render-target and Gaussian-filter jobs. On this GTX 1060 driver their Vulkan
render-pass, descriptor, and command-buffer fixed costs remain more visible
than the OpenGL driver's internal scheduling.

Software and OpenGL shadow hashes are unchanged. Image hashes intentionally
changed at rounded edges when clip-mask rasterization was replaced by native
shader coverage. Vulkan shadow output changed when the CPU blur was replaced
by the GPU path; its OpenGL comparison has a maximum channel difference of 1.
The optimized revision passed all 64 Release CTest entries, including all 22
Vulkan tests, Software golden images, OpenGL/Vulkan filter pixel parity, text,
Unicode, examples, API documentation, and installed-package consumers.

## Verified 1080p stress optimization

The 1080p stress scenes exposed a second set of bottlenecks that smaller scenes
hid: OpenGL recreated sprite GPU objects during every flush, text recorded one
command per atlas glyph, repeated translated shapes rebuilt identical meshes,
and Software rasterization paid triangle interpolation costs for simple quads
and uniform anti-aliased interiors.

The candidate was measured on 2026-07-27 on the same Windows i7-8700 / GTX 1060
machine in `Release`. Each scene ran in a fresh process at 1920 x 1080. The
standard profile uses 30 timed frames after 5 warmup frames; the more variable
Software text result uses the thorough 120 + 20 profile.

| Backend | Scene | Before median | Optimized median | Delta | Commands / draws |
| --- | --- | ---: | ---: | ---: | ---: |
| OpenGL | `geometry_stress` | 43.890 ms | 24.759 ms | -43.6% | 2,305 / 2,305 |
| OpenGL | `text_stress` | 893.640 ms | 13.459 ms | -98.5% | 577 / 194 |
| Vulkan | `geometry_stress` | 50.940 ms | 25.990 ms | -49.0% | 2,305 / 1 |
| Vulkan | `text_stress` | 34.910 ms | 16.134 ms | -53.8% | 577 / 577 |
| Software | `geometry_stress` | 157.090 ms | 103.599 ms | -34.1% | 2,305 / 2,305 |
| Software | `text_stress` | 79.960 ms | 60.348 ms | -24.5% | 577 / 577 |

OpenGL now retains one sprite batch's program, VAO, and buffers across frames.
Glyph runs sharing atlas and render state use one compact image-batch command.
Vulkan lowers compatible solid geometry to one primitive without losing
per-vertex color or analytic-AA coverage. Translation-normalized fill and AA
meshes are reused under byte-bounded LRU caches, and Software has direct raster
paths for axis-aligned image quads and uniform interior triangles.

OpenGL and Vulkan validation hashes stayed stable through these optimizations.
Software's normalized translated geometry and direct quad interpolation can
differ from the former two-triangle arithmetic by at most one channel value;
the measured comparison changed 0.001543% of pixels with maximum channel delta
1 and mean channel delta 0.000005. Text output converges to the same validation
hash across Software, OpenGL, and Vulkan.

The subsequent batching pass reduced OpenGL `geometry_stress` from 2,305 GPU
draws to 9 by flattening compatible 2D affine transforms into bounded
per-vertex batches. Standard medians observed during the pass ranged from
19.777 ms to 25.565 ms, with the previous 24.759 ms result inside that range,
so the draw-count reduction is verified but no stable frame-time improvement
is claimed. The pixel hash remained `44121eb5a074425f`.

Vulkan now combines compatible atlas/image quads using packed RGBA8 per-vertex
tints and reuses identical sampled-image descriptor sets within a frame.
`text_stress` fell from 577 GPU draws to 194 while retaining pixel hash
`6554c1da7b50ade0`. Its final serial standard median was 16.320 ms versus the
previous 16.134 ms, a 1.2% difference within run variability; this is likewise
a structural batching improvement, not a timing-speedup claim.

## Standard scene matrix

The default resolution is 1920 x 1080. Every scene is deterministic and
produces a fixed validation-frame hash after timing has completed. The default
matches a widely deployed desktop display workload while `--width` and
`--height` remain available for controlled investigations.

| Scene | Coverage | Cache mode | Operations/frame |
| --- | --- | --- | ---: |
| `solid_rects` | Dense filled rectangles and command submission | churn | 576 |
| `rounded_ui` | Rounded UI surfaces and anti-aliased edges | churn | 120 |
| `path_cached` | Repeated complex path geometry | hot | 160 |
| `path_churn` | Per-frame path construction and tessellation | churn | 160 |
| `geometry_stress` | 2,304 mixed rectangles, rounded rectangles, circles, ovals, and custom paths | churn | 2,304 |
| `image_grid` | Reused RGBA texture scaling and sampling | hot | 96 |
| `clip_layers` | Nested clips, transforms, and layers | churn | 144 |
| `shadow_grid` | Shape shadows with varied radii | churn | 36 |
| `text_cached` | Repeated shaped text and glyph-atlas reuse | hot | 120 |
| `text_churn` | Changing text content and glyph lookup pressure | churn | 120 |
| `text_stress` | 576 multilingual text calls, expanding to roughly 8,000 cached glyph commands | hot | 576 |
| `contract_text_latin` | 576 fixed Roboto Latin calls for cross-library comparison | hot | 576 |
| `frosted_glass` | Backdrop capture, blur, and layer composition | hot | 4 |
| `inner_shadow` | Filtered controls with inner shadows | hot | 24 |

Hot and churn variants are deliberate. A renderer should show both steady-state
cache efficiency and the cost of changing content. The stress scenes are also
deliberately large enough to expose command recording, glyph-atlas switching,
batching, tessellation, and submission bottlenecks that small UI samples can
hide.

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

The executable
[`cross_library_benchmark.py`](../scripts/cross_library_benchmark.py) runner and
machine-readable
[`contract.json`](../benchmarks/cross_library/contract.json) quality-gate
external adapters before comparing synchronized complete-frame timing. See the
[cross-library benchmark contract](CROSS_LIBRARY_BENCHMARKS.md) for the adapter
CLI, fixed assets, required JSONL metadata, quality thresholds, and publication
rules.

A fair adapter must render the same scene at the same resolution with
equivalent anti-aliasing, clipping, blending, text, sampling, and
synchronization. It must publish:

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

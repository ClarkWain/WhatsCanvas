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

The active bottleneck analysis, prioritized implementation backlog, and
quantitative acceptance targets are tracked in
[Performance Optimization TODO](PERFORMANCE_OPTIMIZATION_TODO.md).

A checked-in [Windows i7-8700 / GTX 1060 reference run](../benchmarks/baselines/cross-library-nanovg-windows-i7-8700-gtx1060/README.md)
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

## Parameterized workload matrix

Fixed scenes are useful regression anchors, but a single object count can be
overfit. `geometry_stress`, `image_grid`, and `text_stress` therefore also
provide an opt-in parameterized path. Their default behavior and historical
pixel hashes remain unchanged unless workload options are supplied.

| Option | Meaning |
| --- | --- |
| `--workload stable` | Geometry, resources, and command topology remain stable between frames |
| `--workload dynamic-data` | Positions, colors, and text selection change while topology remains stable |
| `--workload dynamic-structure` | Primitive, texture, text, and render-state selection can change each frame |
| `--operations N` | Draw operations in the selected stress scene |
| `--seed N` | Deterministic content seed recorded in JSONL |
| `--texture-count N` | Image resource cardinality, from 1 through 256 |
| `--rounded-ratio 0..1` | Fraction of image operations using rounded coverage |
| `--state-change-rate 0..1` | Deterministic fraction of operations changing blend state |
| `--text-length N` | ASCII characters in each generated text sample |

The matrix runner launches every workload and seed in a fresh Release process,
then reports the median of process medians. It preserves raw JSONL and writes
JSON, CSV, Markdown, and per-backend log-log SVG scaling charts:

```powershell
python scripts\run_benchmark_matrix.py `
  --executable build\Release\WhatsCanvasPerformanceSuite.exe `
  --backend opengl --backend vulkan `
  --preset standard --profile standard `
  --output-dir build\performance-matrix
```

The `smoke` preset covers all three categories and stable/structure-changing
paths with one seed. `standard` uses three operation scales, all three workload
modes, and three seeds. `thorough` expands the scale range and uses five seeds.
Use `--dry-run` to audit every command before a long measurement.

One matrix case remains directly reproducible:

```powershell
build\Release\WhatsCanvasPerformanceSuite.exe `
  --backend vulkan --profile standard --scene image_grid `
  --workload dynamic-structure --operations 1024 --seed 2003 `
  --texture-count 32 --rounded-ratio 0.5 `
  --state-change-rate 0.125 `
  --output build\image-grid-dynamic.jsonl
```

Reports include scale, dynamic mode, texture cardinality, rounded ratio, state
change rate, text length, process range, synchronized frame timing, throughput,
draw calls, and draw reduction. This makes stable-cache wins visible without
hiding dynamic topology or high-state-churn costs.

The native parameterized matrix evaluates WhatsCanvas backends and revisions.
For external libraries whose adapters implement the same parameter semantics,
the cross-library matrix runner applies the pixel-quality contract and an
independent ABBA comparison to every matrix cell:

```powershell
python scripts\run_cross_library_matrix.py `
  --reference "whatscanvas=build/Release/WhatsCanvasPerformanceSuite.exe --backend opengl" `
  --adapter "nanovg=build/Release/WhatsCanvasNanoVGBenchmarkAdapter.exe --backend opengl" `
  --preset standard --profile standard `
  --repetitions 4 `
  --output-dir build\cross-library-matrix
```

`standard` contains 27 cells: three scenes, three operation scales, and three
change modes. Its default workload seed is `1001`; use `--seeds
1001,2003,3001` to test content sensitivity. ABBA repetition controls process
noise separately from content diversity. The default two ABBA blocks use four
fresh processes per renderer per cell; use `--repetitions 8` for a publishable
four-block run. The runner preserves each cell's raw JSONL and capture, then
writes aggregate JSON, CSV, and Markdown with quality status and 95% confidence
verdicts.

The first checked Windows i7-8700 / GTX 1060 run passed all 27 quality gates.
With two ABBA blocks per cell, WhatsCanvas OpenGL was conclusively faster in 12
cells, NanoVG GL3 in 12, and 3 crossed the paired-ratio 95% confidence boundary.
The breakdown was 2/9 WhatsCanvas wins in geometry, 3/9 in images, and 7/9 in
text. Stable single-texture images and most text workloads favored WhatsCanvas;
dynamic multi-texture/state image streams and medium/high-scale dynamic
geometry favored NanoVG. See the
[raw parameter-matrix baseline](../benchmarks/baselines/nanovg-win-i7-8700-gtx1060/README.md)
for every cell and all 216 process JSONL records.

### Post-optimization parameter matrix (`cac08c1`)

The same 27-cell Standard matrix was rerun after ordered eight-slot OpenGL
multi-texture batching, a dedicated batch sampler, and one shared path
attribute/index upload stream. Each cell used two ABBA blocks and four fresh
processes per renderer. All 27 quality gates passed. WhatsCanvas won 12 cells,
NanoVG won 11, and 4 were inconclusive:

| Category | WhatsCanvas faster | NanoVG faster | Inconclusive |
| --- | ---: | ---: | ---: |
| Geometry | 2 | 6 | 1 |
| Images | 6 | 3 | 0 |
| Text | 4 | 2 | 3 |
| **Total** | **12** | **11** | **4** |

The image result changed materially rather than moving inside noise. All three
stable and all three dynamic-data image cells now favor WhatsCanvas. At 1,024
operations, dynamic-data fell from 4.273 ms to 0.773 ms while NanoVG measured
1.667 ms. The remaining conclusive NanoVG wins are:

| Scene | Mode | Operations | WhatsCanvas | NanoVG | WhatsCanvas gap |
| --- | --- | ---: | ---: | ---: | ---: |
| `geometry_stress` | dynamic-structure | 256 | 1.254 ms | 0.748 ms | 65.8% slower |
| `geometry_stress` | dynamic-data | 1,024 | 1.890 ms | 1.575 ms | 20.1% slower |
| `geometry_stress` | dynamic-structure | 1,024 | 2.912 ms | 1.661 ms | 72.4% slower |
| `geometry_stress` | stable | 4,096 | 6.619 ms | 4.542 ms | 44.8% slower |
| `geometry_stress` | dynamic-data | 4,096 | 6.636 ms | 4.711 ms | 38.8% slower |
| `geometry_stress` | dynamic-structure | 4,096 | 9.425 ms | 5.647 ms | 65.6% slower |
| `image_grid` | dynamic-structure | 64 | 0.494 ms | 0.411 ms | 21.0% slower |
| `image_grid` | dynamic-structure | 256 | 0.997 ms | 0.779 ms | 28.6% slower |
| `image_grid` | dynamic-structure | 1,024 | 2.338 ms | 1.652 ms | 37.6% slower |
| `contract_text_latin` | stable | 1,024 | 5.905 ms | 5.874 ms | 0.7% slower |
| `contract_text_latin` | dynamic-data | 1,024 | 6.178 ms | 5.898 ms | 4.5% slower |

This changes the optimization priority. Large and structurally changing
geometry is the dominant gap. Dynamic-structure images are next: their
deliberate blend barriers and changing texture sets still produce many small
batches even though ordinary multi-texture streams no longer do. The two text
losses are small and lower priority; the 1,024-operation dynamic-structure text
cell still favors WhatsCanvas, 6.652 ms versus 8.794 ms.

The renderer counters locate those gaps more precisely:

| Workload | Record | Submit | Draws | Structural evidence |
| --- | ---: | ---: | ---: | --- |
| Geometry dynamic-structure, 256 | 0.180 ms | 1.050 ms | 46 | State/topology changes already fragment a small frame |
| Geometry dynamic-structure, 1,024 | 0.680 ms | 2.250 ms | 237 | Both compilation and small submissions scale |
| Geometry dynamic-structure, 4,096 | 2.810 ms | 7.010 ms | 950 | 97,636 vertices and about 2.1 MiB of path upload |
| Geometry stable, 4,096 | 2.730 ms | 4.280 ms | 2 | About 2.0 MiB upload: draw count is not the main cost |
| Images dynamic-structure, 1,024 | 0.310 ms | 2.060 ms | 283 | Real blend barriers dominate submit after multi-texture batching |
| Text dynamic-data, 1,024 | 4.140 ms | 2.080 ms | 2 | Remaining text gap is primarily record-side work |

These counters are from representative WhatsCanvas processes in the same
matrix. They are diagnostic, not additional independent samples. They rule out
one generic fix: geometry needs less frame compilation and attribute expansion,
image dynamic-structure needs cheaper barrier-bounded batches, and high-count
text needs record-path profiling rather than another draw-call optimization.

### Current parameter matrix

The Standard matrix was rerun after multi-packet topology reuse, GPU shape
parameters for large affine batches, compact on-demand path gradient storage,
persistent OpenGL sprite-sequence state, short-path allocation cleanup, and
redundant GL-state removal. The same two-block ABBA schedule and four fresh
processes per renderer were used for every cell. All 27 quality gates passed:

| Category | WhatsCanvas faster | NanoVG faster | Inconclusive |
| --- | ---: | ---: | ---: |
| Geometry | 8 | 0 | 1 |
| Images | 9 | 0 | 0 |
| Text | 9 | 0 | 0 |
| **Total** | **26** | **0** | **1** |

This run closes every previous conclusive image, text, and geometry loss.
Reusing the sprite program, VAO, projection, sampler uniforms, sampler
bindings, and unchanged texture slots across an ordered image sequence makes
even the deliberately fragmented image workload competitive without
reordering transparent draws:

| Scene | Mode | Operations | WhatsCanvas | NanoVG | Result |
| --- | --- | ---: | ---: | ---: | --- |
| `geometry_stress` | stable | 4,096 | **3.971 ms** | 5.417 ms | WhatsCanvas 26.7% faster |
| `geometry_stress` | dynamic-data | 4,096 | **3.893 ms** | 5.175 ms | WhatsCanvas 24.8% faster |
| `image_grid` | dynamic-structure | 1,024 | **1.328 ms** | 1.869 ms | WhatsCanvas 28.9% faster |
| `contract_text_latin` | dynamic-structure | 1,024 | **6.886 ms** | 10.132 ms | WhatsCanvas 32.0% faster |

The two previously slower geometry cells now favor WhatsCanvas:

| Mode | Operations | WhatsCanvas | NanoVG | Result |
| --- | ---: | ---: | ---: | ---: |
| `dynamic-structure` | 1,024 | **1.730 ms** | 1.906 ms | WhatsCanvas 9.2% faster |
| `dynamic-structure` | 4,096 | **5.751 ms** | 6.029 ms | WhatsCanvas 4.6% faster |

At 256 operations the same workload remains statistically inconclusive
(0.788 versus 0.811 ms). The implementation does not weaken AA, reorder blend
barriers, or branch on benchmark sizes. It removes general short-path costs:
`Path` reserves the common compact verb count, contour extraction moves storage
and reserves from the known verb count, and simple fills consume the parsed
contour directly. OpenGL also caches the unchanged additive blend equation and
returns immediately for an already-empty clip/scissor state.

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
- `end_frame_cpu_*_ms`: CPU wall time spent inside `Canvas::endFrame()` before
  the benchmark's explicit backend completion wait.
- `gpu_wait_*_ms`: time spent in the benchmark's completion barrier. OpenGL
  uses `glFinish`; Vulkan waits for its queue. This is a blocking benchmark
  diagnostic, not the normal presentation path.
- `submit_*_ms`: backward-compatible sum of `end_frame_cpu_*_ms` and
  `gpu_wait_*_ms`.
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
- `image_batch_quad_count`, `image_batch_instanced_quad_count`, and
  `image_batch_upload_bytes`: SpriteBatch input quads, the subset represented
  by one 12-float GPU instance rather than four 14-float vertices, and the
  resulting per-frame vertex/instance upload traffic.
- `flush_cpu_ns`: Renderer flush wall time. `frame_compile_cpu_ns` isolates
  command-to-packet lowering where a `FrameCompiler` path is active, while
  `device_execution_cpu_ns` isolates device-command execution.
- `gpu_time_available` and `gpu_time_ns`: a delayed, non-blocking backend timer
  result. OpenGL uses a three-query ring; unsupported backends report
  `false` rather than substituting CPU time. GPU timing is disabled by default;
  pass `--gpu-timing` for a diagnostic run so comparative baselines do not pay
  timer-query overhead.
- `compiled_packet_count`, `compiled_vertex_bytes`, and
  `compiled_index_bytes`: the submitted compact-packet footprint.
- `text_normalization_count`, shape/layout cache hits and misses,
  `text_layout_view_hits` (cache hits consumed without copying the cached quad
  vector),
  `glyph_atlas_hits/misses`, `glyph_rasterization_count`,
  `zero_area_glyph_hits`, `generated_glyph_quad_count`, and
  `glyph_atlas_dirty_bytes`: per-frame portable text-pipeline work. Native text
  backends may report zero for stages hidden behind the platform API.
- `text_normalization_cpu_ns`, `text_layout_cache_cpu_ns`,
  `text_shaping_cpu_ns`, `glyph_cache_lookup_cpu_ns`, `glyph_raster_cpu_ns`,
  and `glyph_atlas_upload_cpu_ns`: CPU-time split for the main portable text
  stages. `text_bidi_cpu_ns`, `text_font_fallback_cpu_ns`,
  `text_font_data_cpu_ns`, and `text_shape_engine_cpu_ns` subdivide shaping so
  provider matching is not mistaken for HarfBuzz/simple-shaper execution.
- `path_input_vertex_count`, `path_tessellated_vertex_count`,
  `path_aa_expanded_vertex_count`, `path_merged_vertex_count`, and
  `path_uploaded_vertex_count`: the per-frame path geometry funnel. The AA
  value is the unique vertex count after indexing, so it can be lower than the
  pre-index triangle soup. Merged is also retained under the historical
  `path_vertex_count` name. Uploaded counts position records transferred to the
  active backend; use the byte counters to analyze total attribute bandwidth.
- `command_object_count`, `command_allocation_count`, and
  `command_pool_reuse_count`: distinguish logical command construction from
  actual system-heap traffic. A high object count with zero allocations means
  the command pool is working and is not evidence for another allocation
  optimization.
- `command_clone_count`, `payload_copy_bytes`, and `staging_capacity_bytes`:
  retained-Picture cloning, known CPU payload materialization/copy traffic, and
  retained reusable staging capacity. Copy bytes are a diagnostic lower bound,
  not total process memory bandwidth. Staging includes queued image-batch quad
  capacity as well as renderer/device staging. It is not an arena metric;
  WhatsCanvas does not yet use a unified frame arena.
- `batch_break_command_type_count`, `batch_break_state_count`,
  `batch_break_texture_limit_count`, and `batch_break_vertex_limit_count`:
  reasons an otherwise consecutive OpenGL path/sprite sequence stopped
  batching. Natural end-of-frame termination is not counted as a break.
- `tracked_resource_bytes`: the sum of glyph-atlas, pooled render-target,
  tessellation, stroke, and bitmap-text cache bytes owned by WhatsCanvas.

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
and `--height` override a profile for investigation. Workload options are
recorded in every JSONL result, but custom settings should not be mixed into a
fixed-scene standard reference report; use the matrix runner for parameterized
publication.

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

The first quality-gated NanoVG GL3 baseline is now checked in. On the Windows
i7-8700 / GTX 1060 machine, the median of three independent 1080p Standard
process medians was:

| Scene | WhatsCanvas OpenGL | NanoVG GL3 | Result |
| --- | ---: | ---: | --- |
| `geometry_stress` | 25.659 ms | 4.316 ms | NanoVG 5.95x faster |
| `image_grid` | 0.308 ms | 0.372 ms | WhatsCanvas 1.21x faster |
| `contract_text_latin` | 15.911 ms | 3.334 ms | NanoVG 4.77x faster |

All three NanoVG captures passed their scene quality gates. This exposes a real
WhatsCanvas weakness in per-frame path construction/tessellation and text
recording; the image batching path is already competitive. See the
[raw baseline and methodology](../benchmarks/baselines/cross-library-nanovg-windows-i7-8700-gtx1060/README.md).

The first optimization pass has now been remeasured with five independent
processes on the same machine. The median of process medians was:

| Scene | Before | After | Improvement | NanoVG GL3 | Remaining gap |
| --- | ---: | ---: | ---: | ---: | ---: |
| `geometry_stress` | 25.659 ms | 15.73 ms | 38.7% | 4.316 ms | 3.64x |
| `image_grid` | 0.308 ms | 0.29 ms | within noise | 0.372 ms | none demonstrated |
| `contract_text_latin` | 15.911 ms | 4.78 ms | 70.0% | 3.334 ms | 1.43x |

Every process produced the same scene hashes. The text improvement comes from
resolved glyph-layout caching, zero-area glyph caching, and four-vertex indexed
sprite quads. Geometry improved through move-only command handoff, append-only
per-frame path streams, and removal of deterministic AA-cache churn. Geometry
still expands simple primitives into generic triangle-soup paths, so its
remaining gap requires the semantic primitive and indexed-AA work tracked in
the [performance optimization backlog](PERFORMANCE_OPTIMIZATION_TODO.md).

The second geometry pass retained the same quality contract and again used the
median of five independent process medians:

| Scene | Original | Pass 1 | Pass 2 | NanoVG GL3 | Pass 2 gap |
| --- | ---: | ---: | ---: | ---: | ---: |
| `geometry_stress` | 25.659 ms | 15.73 ms | 8.68 ms | 4.316 ms | 2.01x |
| `image_grid` | 0.308 ms | 0.29 ms | 0.31 ms | 0.372 ms | within noise |
| `contract_text_latin` | 15.911 ms | 4.78 ms | 4.42 ms | 3.334 ms | 1.33x |

Indexed AA reduced the geometry stream from 269,598 duplicated vertices to
62,984 vertices plus indices. Immutable shared cache geometry removed repeated
per-command vector copies, and pre-sized affine batch assembly reduced submit
work. The scene now uses one draw and uploads 2,841,944 path bytes instead of
7,548,744. Captured hashes remain `44121eb5a074425f`, `432ad28b33a51375`,
and `737cad1b0d1169f2` for geometry, image, and text respectively.

The third pass added 16-bit merged index packets and a guarded simple solid-fill
path. Five-process geometry measurement and a nine-process text check produced:

| Scene | Original | Pass 2 | Pass 3 | NanoVG GL3 | Pass 3 gap |
| --- | ---: | ---: | ---: | ---: | ---: |
| `geometry_stress` | 25.659 ms | 8.68 ms | 6.45 ms | 4.316 ms | 1.49x |
| `image_grid` | 0.308 ms | 0.31 ms | 0.27 ms | 0.372 ms | within noise |
| `contract_text_latin` | 15.911 ms | 4.42 ms | 4.81 ms | 3.334 ms | 1.44x |

The geometry scene records in 3.07 ms and submits in 3.40 ms. Its 269,598
indices occupy 539,196 bytes, confirming a 16-bit stream; total path upload is
2,302,748 bytes, down 69.5% from the original. Complex geometry semantics keep
the generic path pipeline, and all captures retain their prior hashes.

The fourth geometry pass introduced parameterized local-space primitive meshes,
normalized RGBA8/coverage8 attributes for merged solid packets, a bounded
thread-local path-command pool, and a Release trusted-index fast path. Seven
alternating WhatsCanvas/NanoVG processes produced:

| Scene | Original | Pass 3 | Pass 4 | Paired NanoVG GL3 | Pass 4 gap |
| --- | ---: | ---: | ---: | ---: | ---: |
| `geometry_stress` | 25.659 ms | 6.45 ms | 4.682 ms | 3.965 ms | 1.18x |

The current record/submit medians are 1.767/2.956 ms. The scene still uses one
draw, 62,984 vertices, and 269,598 16-bit indices, but total path upload is now
1,357,988 bytes: 41.0% below pass 3 and 82.0% below the original. All seven
processes produced `5e7e67fb8b9ca579`.

The parameterized curve cache changes only 171 of 2,073,600 pixels relative to
pass 3 (0.0082%). Every changed channel is one 8-bit level, with RMSE 0.0069.
The complete Release build and all 66 Release tests pass.

The fifth geometry pass retained the merged path packet across frames, reused
its stable shared-geometry index topology and packed coverage stream, and
removed full `Paint` copies and general 4 x 4 matrix multiplication from the
simple solid-fill recording path. Eight WhatsCanvas and eight NanoVG processes
run in ABBA order produced:

| Scene | Original | Pass 4 | Pass 5 | Paired NanoVG GL3 | Pass 5 result |
| --- | ---: | ---: | ---: | ---: | ---: |
| `geometry_stress` | 25.659 ms | 4.682 ms | 2.690 ms | 3.947 ms | WhatsCanvas 31.8% faster |

Pass 5 records in 1.486 ms and submits in 1.199 ms, compared with NanoVG's
1.611/2.372 ms on the same machine and run sequence. Upload volume and draw
structure are unchanged at 1,357,988 bytes, four uploads, and one draw: the
gain comes from eliminating CPU allocation and reconstruction work before the
same GPU packet is uploaded. All eight WhatsCanvas processes retained
`5e7e67fb8b9ca579`; image and text control hashes remain
`432ad28b33a51375` and `737cad1b0d1169f2`. The complete Release build and all
66 Release tests pass.

The cross-library runner now automates that methodology instead of requiring a
manual process script. Four ABBA blocks launch eight fresh processes per
renderer, retain every measured frame sample and quality capture, calculate
within-block geometric-mean ratios, and publish deterministic bootstrap 95%
confidence intervals. The contract also fixes full-frame draw clear semantics,
the Roboto SHA-256, text size/baseline/shaping/raster modes, and parameterized
workload fields. The NanoVG adapter implements the same scale/seed/data/
structure options.

The latest 1080p Standard run on the same reference machine produced:

| Scene | WhatsCanvas OpenGL median (95% CI) | NanoVG GL3 median (95% CI) | Paired NanoVG / WhatsCanvas (95% CI) |
| --- | ---: | ---: | ---: |
| `geometry_stress` | **2.617 ms** (2.572-2.764) | 3.705 ms (3.605-3.807) | 1.407x (1.273-1.441) |
| `image_grid` | **0.272 ms** (0.262-0.304) | 0.383 ms (0.380-0.388) | 1.369x (1.347-1.417) |
| `contract_text_latin` | **2.878 ms** (2.670-3.021) | 3.292 ms (3.255-3.337) | 1.153x (1.105-1.169) |

All 48 process runs passed their quality gate. These intervals quantify this
machine and contract; they are not a cross-hardware ranking.

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

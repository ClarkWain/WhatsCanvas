# Performance Optimization TODO

This document records the verified bottlenecks exposed by the 1920 x 1080
cross-library benchmark. It is an engineering backlog, not a marketing score.
Every optimization must preserve pixel validation, pass the Release test suite,
and include before/after measurements from the same machine and driver.

## Current baseline

Windows 10, Intel i7-8700, NVIDIA GTX 1060 3GB, OpenGL 3.3, Release, 30 timed
frames after 5 warmup frames:

| Scene | WhatsCanvas | NanoVG reference | Current gap |
| --- | ---: | ---: | ---: |
| `geometry_stress` | 25.659 ms | 4.316 ms | 5.95x |
| `contract_text_latin` | 15.911 ms | 3.334 ms | 4.77x |
| `image_grid` | 0.308 ms | 0.372 ms | within run-to-run noise |

The text ratio above is the historical aggregate. A paired-run median gives
4.34x and is the preferred interpretation until the benchmark runner adopts
paired confidence intervals.

## Optimization pass 1

The first P0 pass was measured with five independent processes using the same
machine, driver, resolution, warmup, and sample count as the baseline. The
table reports the median of the five process medians:

| Scene | Baseline | Pass 1 | Improvement | NanoVG reference | Remaining gap |
| --- | ---: | ---: | ---: | ---: | ---: |
| `geometry_stress` | 25.659 ms | 15.73 ms | 38.7% | 4.316 ms | 3.64x |
| `contract_text_latin` | 15.911 ms | 4.78 ms | 70.0% | 3.334 ms | 1.43x |
| `image_grid` | 0.308 ms | 0.29 ms | within noise | 0.372 ms | none demonstrated |

All five runs produced the same captured pixel hash for every scene. The
Release build and all 66 tests passed after the changes.

Pass 1 removed repeated warm-frame glyph layout and space rasterization,
changed sprite quads from six duplicated vertices to four indexed vertices,
moved owning path payloads instead of copying them, appended path streams by
offset after one per-frame orphan, and raised the AA cache capacity enough to
hold the 253 stable stress-scene entries. Geometry record and submit medians
fell from 12.212/13.067 ms to 8.66/6.95 ms respectively.

## Optimization pass 2

The indexed-AA and shared-geometry pass was measured with another five
independent processes under the same contract:

| Scene | Baseline | Pass 1 | Pass 2 | NanoVG reference | Remaining gap |
| --- | ---: | ---: | ---: | ---: | ---: |
| `geometry_stress` | 25.659 ms | 15.73 ms | 8.68 ms | 4.316 ms | 2.01x |
| `contract_text_latin` | 15.911 ms | 4.78 ms | 4.42 ms | 3.334 ms | 1.33x |
| `image_grid` | 0.308 ms | 0.29 ms | 0.31 ms | 0.372 ms | none demonstrated |

Geometry is now 66.2% faster than the original baseline and 44.8% faster than
pass 1. The AA triangle order and coverage are unchanged, but duplicate
triangle vertices are represented once and referenced by an index stream.
Cached AA geometry is immutable and shared by commands, so a cache hit no
longer allocates and copies point, coverage, and index vectors. Batch assembly
uses direct affine writes into pre-sized storage.

The stress scene now submits 62,984 vertices and 269,598 indices in one draw,
instead of 269,598 duplicated vertices in nine draws. Path upload traffic fell
from 7,548,744 to 2,841,944 bytes (62.4%). All five runs retained the original
pixel hashes, and all 66 Release tests passed.

## Optimization pass 3

The third pass added automatic 16/32-bit index packets and a guarded simple
fill path for solid single-contour geometry. Rectangles, rounded rectangles,
circles, ovals, triangles, diamonds, and other eligible fills skip Path object
construction or the generic multi-contour setup. Strokes, shadows, gradients,
path effects, even-odd fills, and multi-contour paths retain the full pipeline.

| Scene | Baseline | Pass 2 | Pass 3 | NanoVG reference | Remaining gap |
| --- | ---: | ---: | ---: | ---: | ---: |
| `geometry_stress` | 25.659 ms | 8.68 ms | 6.45 ms | 4.316 ms | 1.49x |
| `contract_text_latin` | 15.911 ms | 4.42 ms | 4.81 ms | 3.334 ms | 1.44x |
| `image_grid` | 0.308 ms | 0.31 ms | 0.27 ms | 0.372 ms | none demonstrated |

Geometry is now 74.9% faster than the original baseline. Its record/submit
medians are 3.07/3.40 ms. The 269,598-element index stream uses 539,196 bytes,
and total path upload traffic is 2,302,748 bytes, 69.5% below the original.
All quality hashes remain unchanged and all 66 Release tests pass.

## Optimization pass 4

The fourth pass keeps reusable Rect, RRect, Circle, and Oval meshes under
parameterized local-space keys, pools fixed-size path command allocations, and
uses normalized RGBA8/coverage8 attributes for merged solid-color OpenGL
packets. Float vertex colors remain active for arbitrary per-vertex data and
whenever gamma correction is enabled. Release builds also skip the redundant
full index-bound scan for library-generated packets; Debug retains it.

Seven alternating WhatsCanvas/NanoVG processes on the reference machine
produced the following median of process medians:

| Scene | Original | Pass 3 | Pass 4 | Paired NanoVG | Current gap |
| --- | ---: | ---: | ---: | ---: | ---: |
| `geometry_stress` | 25.659 ms | 6.45 ms | 4.682 ms | 3.965 ms | 1.18x |

Pass 4 records in 1.767 ms and submits in 2.956 ms. Total path upload fell from
2,302,748 to 1,357,988 bytes, while retaining 62,984 vertices, 269,598 16-bit
indices, one draw, and four stream uploads. Compared with the original result,
the scene is 81.8% faster and uploads 82.0% fewer bytes.

Canonical local-space curve evaluation changed 171 of 2,073,600 pixels
(0.0082%) relative to pass 3. Every changed channel differs by exactly one
8-bit level (RMSE 0.0069), so the quality gate remains comfortably satisfied.
All seven runs produced the same `5e7e67fb8b9ca579` hash, the complete Release
build passed, and all 66 Release tests passed.

## Optimization pass 5

The fifth pass keeps the OpenGL merged path packet alive across frames, so its
large vectors retain capacity. When an immutable shared-geometry sequence is
unchanged, the renderer also reuses the already rebased 16-bit index topology
and packed coverage bytes. Shared ownership of the topology entries prevents
pointer-reuse ambiguity, and a dedicated OpenGL regression covers both a cache
hit and a same-size, reordered-topology invalidation.

Simple solid fills now compute their final color and blend mode directly from
the immutable `Paint` plus current graphics state. Translated primitive meshes
update only the matrix translation column instead of constructing and
multiplying two general 4 x 4 matrices.

Eight WhatsCanvas and eight NanoVG processes run in ABBA order produced:

| Scene | Original | Pass 4 | Pass 5 | Paired NanoVG | Result |
| --- | ---: | ---: | ---: | ---: | ---: |
| `geometry_stress` | 25.659 ms | 4.682 ms | 2.690 ms | 3.947 ms | WhatsCanvas 31.8% faster |

Pass 5 record/submit medians are 1.486/1.199 ms, versus NanoVG's
1.611/2.372 ms. The draw count, upload count, upload bytes, vertex count, and
index count are unchanged; the improvement is entirely reduced CPU staging
work. All eight runs produced `5e7e67fb8b9ca579`, and the image/text control
hashes also remain unchanged. The complete Release build and all 66 Release
tests pass.

## Parameter-matrix closure

The two remaining losses after the multi-packet/GPU-parameter pass were
`geometry_stress` dynamic-structure at 1,024 and 4,096 operations. The common
causes were short-path allocation/copy overhead and redundant OpenGL state
submission, not the operation counts themselves.

`Path` now reserves the common compact verb count. Contour extraction reserves
from the known verb count, moves completed point storage, and lets the simple
fill path consume the parsed contour without rebuilding it. `RenderContext`
also caches the additive blend equation and fast-paths an already-empty
clip/scissor state.

The final 1920 x 1080 Standard matrix passed all 27 quality gates:

| Category | WhatsCanvas faster | NanoVG faster | Inconclusive |
| --- | ---: | ---: | ---: |
| Geometry | 8 | 0 | 1 |
| Images | 9 | 0 | 0 |
| Text | 9 | 0 | 0 |
| **Total** | **26** | **0** | **1** |

The 1,024 dynamic-structure cell moved from 1.875/1.753 ms to
1.730/1.906 ms (WhatsCanvas/NanoVG). The 4,096 cell moved from
6.242/5.934 ms to 5.751/6.029 ms. The 256 cell remains statistically
inconclusive at 0.788/0.811 ms.

## Root cause

The common problem is **early expansion and late batching**. Images retain
their semantic quad representation until submission and perform well. Geometry
and text are expanded into owning vectors and per-call command objects in the
Canvas layer, then copied, transformed, widened, and merged again during
`Renderer::flush()`.

### Verified geometry costs

- The original path generated more than 262,144 vertices for 2,304 simple
  shapes; indexed AA now reduces this to 62,984 vertices.
- Analytic AA is expanded from triangle soup. A clean convex `n`-gon uses
  approximately `9n - 6` vertices instead of a contour fill plus fringe strip.
- Path merge still performs another complete transform, attribute expansion,
  and index-offset pass.
- The current scene uses one indexed draw and four stream uploads.
- OpenGL streams append by offset after one orphan per frame.
- Rect, rounded rect, circle, and oval now use parameterized reusable meshes.
- Fixed-size path commands use a bounded thread-local reuse pool.
- Merged vectors retain capacity across frames. Stable immutable geometry
  sequences reuse rebased indices and packed coverage; transformed positions
  and per-shape colors are still rebuilt because they may change each frame.
- Generic paths retain exact float-bit content keys; typed primitives no longer
  fragment their cache entries by translation.

### Verified text costs

- Before pass 1, zero-area glyphs such as spaces were not represented in the
  atlas cache. The Latin stress scene therefore repeated about 1,632 FreeType
  raster calls per frame after warmup.
- Shape-cache hits return a copied `ShapedTextRun`.
- Pass 1 caches resolved glyph layouts, but a layout miss still repeats face
  resolution, atlas lookup, and quad construction.
- One label creates several temporary vectors and one heap command; the scene
  records 577 commands before merging to two draws.
- Pass 1 emits four indexed vertices instead of six, but the generic sprite
  vertex remains 13 floats and text still uploads unused rounded-image
  attributes.

## Required instrumentation

- [ ] Separate `endFrame` CPU work from GPU completion wait.
- [ ] Count input, tessellated, AA-expanded, merged, and uploaded vertices.
- [x] Count path upload calls and bytes.
- [x] Report fill and AA cache hits and misses separately.
- [ ] Count text normalization, shape cache hits, atlas hits, raster calls,
      zero-area glyph hits, generated quads, and atlas dirty bytes.
- [ ] Count command allocations, payload copy bytes, arena high-water marks,
      and batch-break reasons.
- [ ] Add delayed OpenGL timer queries for GPU execution time.

## P0: remove proven repeated work

- [x] Cache valid zero-area glyphs without allocating atlas texture space.
- [x] Cache resolved glyph layouts, keyed by text and immutable font state.
- [ ] Return stable shape/layout views instead of copying glyph vectors.
- [ ] Use O(1) LRU maintenance and interned face identifiers.
- [x] Add move construction for owning draw command payloads.
- [x] Reuse frame-level path merge staging storage.
- [ ] Reuse frame-level text staging storage.
- [x] Append OpenGL path streams by offset; orphan once per frame or use a
      fenced ring buffer instead of overwriting offset zero per batch.
- [ ] Emit compact text vertices or instanced glyph quads. Pass 1 changed
      sprite quads from six duplicated vertices to four indexed vertices;
      splitting the 13-float generic vertex remains.

Acceptance target: eliminate warm-frame FreeType calls for spaces, reduce text
record time by at least 40%, and reduce geometry submit time by at least 25%
without changing the captured pixel hash or quality thresholds.

Pass 1 status: met. Text record time fell by 82.8%, geometry submit time fell
by 46.8%, and captured hashes remained unchanged.

## P1: preserve semantic primitives

- [ ] Record Rect, RRect, Circle, Oval, GlyphRun, and Image as typed commands.
- [x] Add a `FrameCompiler` producing and measuring backend-neutral compact
      draw packets. OpenGL full-canvas offscreen replay consumes the portable
      output with a correctness fallback, while Vulkan retains its optimized
      lowering and reports the same packet/byte contract. Frame arenas remain
      a separate allocation optimization.
- [ ] Use indexed convex fills and contour AA strips. Pass 2 indexes the
      existing AA triangle ordering and removes duplicate vertices; producing
      the fill and fringe directly from contours remains.
- [ ] Keep generic Path tessellation as the complex-shape fallback.
- [ ] Store color and affine transform as packed instance/uniform data rather
      than expanding them to every vertex.
- [x] Use parameterized primitive cache keys and reusable local-space geometry.

Acceptance target: reduce geometry vertex count by at least 60%, upload bytes
by at least 65%, and bring the 1080p geometry scene below 10 ms on the reference
machine while preserving the quality gate.

Pass 3 status: met. Vertex count fell by 76.6%, upload bytes fell by 69.5%,
and the scene reached 6.45 ms with unchanged hashes.

Pass 4 status: the scene reached 4.682 ms, 1.18x the paired NanoVG result.
Upload bytes are 82.0% below the original, with a maximum one-level channel
difference in 0.0082% of pixels.

Pass 5 status: the scene reached 2.690 ms, 31.8% faster than the paired NanoVG
result under the same 1080p geometry contract. This is a scene-specific result,
not a claim that every WhatsCanvas workload is faster than every NanoVG
workload.

## Current parameter-matrix priorities (`12628e2`)

The post-optimization 27-cell ABBA matrix passed all quality gates and produced
24 WhatsCanvas wins, 2 NanoVG wins, and 1 inconclusive cell. Images and text
both win 9/9 cells. Geometry stable and dynamic-data win all six tested cells;
only the 1,024/4,096-operation dynamic-structure cells remain behind.

### P0: structurally changing geometry

- [x] Avoid rebuilding transformed positions and colors in large affine solid
      batches by using GPU shape parameter tables.
- [x] Retain compiled topology for every stable batch after the 65,536-vertex
      split instead of letting one `pathBatchTopology_` slot overwrite another.
- [x] Expose topology hit/miss counters in the public performance result.
- [ ] Add timer-query and frame-compile timing before changing the AA format.
- [x] Compact the per-frame command stream for many short blend runs without
      changing blend order or weakening AA.

The 1,024/4,096 losses were closed by short-path allocation and GL state
caching. The final 256-operation inconclusive cell was then closed by
conservatively grouping pairwise-disjoint simple fills by blend mode, caching
shared-geometry bounds and packed AA coverage, and bulk-remapping indices. A
four-block, eight-process-per-renderer ABBA follow-up measured 0.605 ms for
WhatsCanvas and 0.807 ms for NanoVG with non-overlapping confidence intervals.
Overlapping, clipped, scissored, stroked, and shader-backed paths remain strict
ordering barriers.

### P1: high-state-churn image batches

- [x] Batch an ordered stream containing up to eight textures.
- [x] Use a sampler object instead of repeatedly mutating texture parameters.
- [x] Cover slot reuse, the ninth-texture boundary, draw count, and pixels.
- [x] Reduce setup cost for the small batches that remain between real blend
      barriers; do not reorder translucent commands across those barriers.

Persistent Sprite program/VAO/projection/sampler state and batching single-image
runs changed all three `dynamic-structure` cells to conclusive wins.

### P2: high-count stable text

- [x] Profile the 1,024-label stable and dynamic-data record path before adding
      another cache layer.

All nine text cells now favor WhatsCanvas. Further text work should be driven
by broader script and shaping coverage, not this Latin performance contract.

## P3: backend convergence

- [ ] Make OpenGL, Vulkan, and Software consume the same compiled draw packets.
- [ ] Preserve strict clip, layer, filter, blend, target, and snapshot barriers.
- [x] Add a backend-neutral retained `Picture`/display-list API for static Canvas operations.
- [ ] Retain shaped `GlyphRun`/`TextBlob` data inside Picture text operations.
- [x] Add context/content-generation-keyed compiled Picture packets; derived
      resources are purged before orderly backend teardown.
- [x] Add an explicit RepaintBoundary-like rasterized Picture path with a
      configurable per-Canvas 32 MB soft budget, conservative local bounds,
      zero-budget bypass, LRU eviction, memory statistics, pressure/pixel tests,
      and Android pause/resume validation.
- [x] Snapshot owned CPU-backed Images into backend-neutral Picture operations;
      source mutation is copy-on-write and external textures remain rejected.
- [x] Require a second observation before admitting final fill/stroke AA
      geometry, preventing one-shot animation keys from polluting stable caches.
- [x] Add a no-GL-delete abandon-context path for involuntary EGL loss.
- [ ] Reduce cold first-raster latency after a genuine Context loss.
- [ ] Evaluate parallel frame compilation only after the single-threaded packet
      path no longer performs redundant copies.

## Benchmark corrections

- [x] Use paired ABBA process ordering and publish confidence intervals.
- [x] Save per-frame samples and all quality captures.
- [x] Use identical clear semantics on both adapters.
- [x] Fix font file hash, size/baseline formulas, and shaping mode in the contract.
- [x] Implement the same parameterized workload options in the NanoVG adapter.
- [ ] Split simple Latin kerning from full OpenType shaping.
- [ ] Replace hand-tuned text thresholds with foreground-region metrics defined
      before measuring a candidate adapter.

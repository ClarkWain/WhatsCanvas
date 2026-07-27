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

## Root cause

The common problem is **early expansion and late batching**. Images retain
their semantic quad representation until submission and perform well. Geometry
and text are expanded into owning vectors and per-call command objects in the
Canvas layer, then copied, transformed, widened, and merged again during
`Renderer::flush()`.

### Verified geometry costs

- More than 262,144 vertices are generated for 2,304 simple shapes.
- Analytic AA is expanded from triangle soup. A clean convex `n`-gon uses
  approximately `9n - 6` vertices instead of a contour fill plus fringe strip.
- Path merge performs another complete transform, color, and coverage expansion.
- Nine batches upload three separate streams, producing about 27 buffer uploads.
- Each upload currently overwrites offset zero, which can make the driver wait
  for an earlier draw using the same storage.
- Rect, rounded rect, circle, and oval use the generic Path pipeline.
- Short-lived Path, contour, mesh, command, and merged vectors create allocation
  and memory-copy pressure.
- Exact float-bit cache keys fragment equivalent translated primitive meshes;
  the 64-entry AA cache can churn under the stress scene.

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
- [ ] Report fill and AA cache hits, misses, and evictions separately.
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
- [ ] Reuse frame-level path merge and text staging storage.
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
- [ ] Add a `FrameCompiler` producing compact draw packets backed by frame arenas.
- [ ] Use indexed convex fills and contour AA strips. Pass 2 indexes the
      existing AA triangle ordering and removes duplicate vertices; producing
      the fill and fringe directly from contours remains.
- [ ] Keep generic Path tessellation as the complex-shape fallback.
- [ ] Store color and affine transform as packed instance/uniform data rather
      than expanding them to every vertex.
- [ ] Quantize primitive cache keys and build reusable local-space geometry.

Acceptance target: reduce geometry vertex count by at least 60%, upload bytes
by at least 65%, and bring the 1080p geometry scene below 10 ms on the reference
machine while preserving the quality gate.

Pass 3 status: met. Vertex count fell by 76.6%, upload bytes fell by 69.5%,
and the scene reached 6.45 ms with unchanged hashes.

## P2: backend convergence

- [ ] Make OpenGL, Vulkan, and Software consume the same compiled draw packets.
- [ ] Preserve strict clip, layer, filter, blend, target, and snapshot barriers.
- [ ] Add optional retained `GlyphRun`, `TextBlob`, and display-list APIs.
- [ ] Evaluate parallel frame compilation only after the single-threaded packet
      path no longer performs redundant copies.

## Benchmark corrections

- [ ] Use paired ABBA process ordering and publish confidence intervals.
- [ ] Save per-frame samples and all quality captures.
- [ ] Use identical clear semantics on both adapters.
- [ ] Fix font file hash, cap height, baseline, and shaping mode in the contract.
- [ ] Split simple Latin kerning from full OpenType shaping.
- [ ] Replace hand-tuned text thresholds with foreground-region metrics defined
      before measuring a candidate adapter.

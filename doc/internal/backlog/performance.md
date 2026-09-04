# Optional Performance Improvement Register

Status reviewed: 2026-09-04.

There is no open performance blocker for the stable v1 scope. The published
quality-matched matrix reports 26 WhatsCanvas wins, 0 losses, and 1 tie against
NanoVG, with all 27 pixel-quality checks passing. Android and Spider Solitaire
device measurements also meet their recorded targets.

Verified results and completed optimization passes live in
[`performance-optimization-log.md`](../../archive/implementation/performance-optimization-log.md).
The public methodology and current claims live in
[`PERFORMANCE_BENCHMARKS.md`](../../public/performance/PERFORMANCE_BENCHMARKS.md).

The items below are investigation candidates, not prioritized work. Start one
only when a reproducible production workload or benchmark identifies the
corresponding cost.

## CPU scaling candidates

- Replace linear LRU maintenance and repeated face identifiers with O(1)
  bookkeeping and interned identity.
- Evaluate a frame arena if allocation profiles show material frame-time or
  retained-capacity cost.
- Preserve semantic Rect, RRect, Circle, Oval, GlyphRun, and Image commands
  longer when profiling shows generic path expansion is significant.
- Evaluate indexed convex fills and indexed contour AA strips for workloads
  where tessellation remains measurable.

## Backend convergence candidates

- Share compiled draw packets among OpenGL, Vulkan, and Software where doing so
  removes measured duplicate work without weakening backend-specific paths.
- Preserve clip, layer, filter, blend, target, and snapshot barriers in any
  shared packet representation.
- Retain shaped GlyphRun/TextBlob data in Picture operations if text recording
  appears in a measured hot path.
- Reduce cold first-raster latency after genuine context loss when a target
  application reports it as a user-visible problem.

## Measurement refinements

- Split simple Latin kerning from full OpenType shaping when a benchmark needs
  to attribute those costs separately.
- Replace hand-tuned text thresholds with foreground-region metrics when the
  new metric is validated against the existing quality contract.
- Consider parallel frame compilation only after a production trace shows the
  stable single-threaded packet path is a bottleneck.

## Promotion rules

- Every started investigation needs a baseline, hypothesis, owner, target
  workload, and acceptance threshold.
- An optimization must improve outside the confidence interval while pixel and
  memory gates remain green.
- Do not prioritize work solely because it appears in this register, and do not
  reopen completed performance goals without new evidence.

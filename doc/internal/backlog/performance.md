# Active Performance Backlog

Status reviewed: 2026-09-04.

Verified historical measurements and completed optimization passes live in
`doc/archive/implementation/performance-optimization-log.md`. Public methodology
and current published results live in
[`PERFORMANCE_BENCHMARKS.md`](../../public/performance/PERFORMANCE_BENCHMARKS.md).

Every item below must preserve pixel validation and be measured on the same
machine, driver, scene parameters, warmup, and sample profile as its baseline.

## P0 — Remove known CPU scaling costs

- [ ] Replace linear LRU maintenance and repeated face identifiers with O(1)
  bookkeeping and interned identity.
- [ ] Introduce a frame arena before publishing allocator high-water metrics.
- [ ] Preserve semantic Rect, RRect, Circle, Oval, GlyphRun, and Image commands
  long enough to avoid unnecessary generic path expansion.
- [ ] Use indexed convex fills and indexed contour AA strips, keeping generic
  path tessellation as the complex-shape fallback.

Exit: the targeted scene improves outside the confidence interval, pixel gates
remain green, and memory/high-water metrics do not regress materially.

## P1 — Converge backend compilation

- [ ] Make OpenGL, Vulkan, and Software consume the same compiled draw packets
  where their semantics match.
- [ ] Preserve strict clip, layer, filter, blend, target, and snapshot barriers
  while sharing packets.
- [ ] Retain shaped GlyphRun/TextBlob data in Picture text operations.
- [ ] Reduce cold first-raster latency after genuine context loss.

Exit: backend parity scenes remain within tolerance and the change removes a
measured duplicate compilation or upload cost.

## P2 — Improve benchmark decision quality

- [ ] Split simple Latin kerning from full OpenType shaping in text benchmarks.
- [ ] Replace hand-tuned text thresholds with foreground-region metrics defined
  by the benchmark contract.
- [ ] Consider parallel frame compilation only after the single-threaded packet
  path is measured and stable.

Exit: the benchmark contract states what each scene isolates, reports raw data
and confidence intervals, and cannot improve by silently reducing visual work.

## Backlog rules

- Link each started item to an issue and record baseline, hypothesis, owner, and
  acceptance threshold.
- Move completed narrative into the archived optimization log or a focused case
  study; do not accumulate completed checkboxes here.
- Reject optimizations that win only by changing scene content or quality.

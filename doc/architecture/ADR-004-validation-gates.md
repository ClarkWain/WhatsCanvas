# ADR-004: Validation and Quality Gates

## Status

Accepted.

## Context

The current smoke and pixel-hash hooks are useful, but they are not enough to prove professional engine quality on their own. The project needs validation that covers architecture boundaries, semantic correctness, rendering stability, and performance regressions.

## Decision

PrismCanvas will adopt a layered verification model.

## Validation Layers

### 1. Architecture checks

- public API must not expose backend-specific resource handles;
- backend additions must not force changes in Canvas-facing APIs;
- module dependency direction must stay one-way.
- shared engine changes must continue to build the independently configured examples, not just the root demo.

### 2. Unit tests

- path math and bounds;
- state stack semantics;
- clip and transform mapping;
- text metrics and line breaking;
- color and sampling helpers.

### 3. Rendering regression tests

- fixed-scene golden images;
- perceptual diff with tolerances;
- exact pixel hash only as a fast local gate.

### 4. Backend consistency tests

- same display list rendered through multiple backends;
- tolerated differences documented explicitly.

### 5. Stability tests

- resize and viewport changes;
- saveLayer / clip stress loops;
- resource lifetime and leak checks;
- future context-loss / device-loss recovery tests.

### 6. Performance gates

- record-time benchmarks;
- submit-time benchmarks;
- draw-call and state-change counters;
- readback latency;
- layer-heavy and text-heavy scenes.

## Consequences

### Positive

- New features can be judged against explicit, repeatable quality gates.
- Architecture work becomes measurable instead of purely stylistic.

### Negative

- The project will need dedicated test scenes, benchmark tooling, and baseline management.

## Follow-up

1. Promote the new `tests/` and `benchmarks/` top-level structure into a broader render-test and benchmark harness.
2. Add scene-driven tests that can run through `ctest` without recursively rebuilding via wrapper scripts.
3. Add structured benchmark output and history tracking for CI and local regression analysis.

## Current Implemented Gates

- `build.bat` / `build.sh`: root engine + demo build gate.
- `smoke_test.bat` / `smoke_test.sh`: fixed-time first-frame render gate for the root demo.
- `clip_path_smoke.bat` / `clip_path_smoke.sh`: fixed-time first-frame render gate for the root demo with stacked non-rect `clipPath` explicitly enabled.
- `regression_smoke.bat` / `regression_smoke.sh`: strict local pixel-baseline gate that runs both the default smoke scene and the stacked non-rect `clipPath` smoke scene with expected hashes.
- `examples_smoke.bat` / `examples_smoke.sh`: aggregated independent example build gate for Tetris, Racer, and Bubble Shooter.
- `PrismCanvasGraphicsStateStackTests`: lightweight unit-test executable, registered through `ctest`, that currently covers `GraphicsStateStack` save/restore semantics plus header-only `Path` behaviors such as even-odd contains, strokeContains, trim, and reversed traversal.
- `ctest` registration: when `PRISMCANVAS_ENABLE_SCRIPT_TESTS=ON`, the root CMake project now exposes the existing smoke/example script gates through standard `ctest` entry points; the strict local regression gate is opt-in through `PRISMCANVAS_ENABLE_LOCAL_REGRESSION_TEST=ON`.
- `tests/README.md` / `benchmarks/README.md`: top-level documentation and growth points for validation and benchmark work.
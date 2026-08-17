# WhatsCanvas Architecture

This directory captures the Phase 0 architecture decisions for turning WhatsCanvas from a demo-first OpenGL playground into a reusable, backend-aware 2D canvas engine.

## Goals

- Build the project as reusable libraries first, demos second.
- Keep the public canvas API stable while allowing multiple render backends.
- Make text, font discovery, and shaping pluggable instead of hard-coded into the canvas core.
- Add a quality system that covers correctness, stability, and performance regression gates.

## Layer Map

1. Public API layer
   - Canvas, Paint, Path, Image, TextLayout-facing APIs.
2. Recording layer
   - Display list / command recording and state snapshot boundaries.
   - `Canvas::recordPicture()` produces an immutable backend-neutral `Picture`.
     Context-owned images/layers are rejected instead of being retained as
     invalid GPU resources; see [Retained Picture](../RETAINED_PICTURE.md).
   - `Canvas::drawPicture()` may derive context/content-generation-keyed
     compiled commands. `drawPictureRasterized()` may additionally derive a
     bounded raster layer for an explicitly isolated static boundary. Both are
     disposable Canvas caches, never portable `Picture` state.
3. Graphics model layer
   - Backend-neutral draw state, blend, clip, sampling, and effect models.
4. Text layer
   - Ordered font providers (dynamic, asset, test, platform/system), resolver,
     shaping, layout, glyph cache, and family/cluster fallback.
5. Render abstraction layer
   - Renderer interfaces, device/resource abstractions, command execution.
6. Backend layer
   - OpenGL, OpenGLES, Software, and optional Vulkan today; Metal and WebGPU
     remain future extension points.
7. Platform layer
   - Windowing, system fonts, image decode, timing, diagnostics.

## Current Phase 0 Outcomes

- Top-level and example builds now consume a shared CMake module instead of duplicating WhatsCanvas target setup.
- The reusable OpenGL implementation is built as a library target before demo executables.
- Canvas now depends on an `IRenderer` abstraction instead of embedding the concrete `Renderer` implementation directly.
- Canvas text measurement and render planning now flow through an `ITextBackend` abstraction, with shared `TextUtils` and `NativeText` modules under `src/text`.
- Portable text selection now flows through the public `FontResolver` contract.
  File/memory `FontManager` instances are providers, while Android API 29+
  performs locale/style/cluster-aware system matching through a core
  `AFontMatcher` provider. System-font refresh advances the resolver generation
  and invalidates selection, shaping, layout, loaded-face, and atlas caches.
  Android XML/NDK variable-font instance coordinates are stored on `FontFace`,
  consumed by both HarfBuzz and FreeType, and included in face/glyph identity.
  Missing Android XML style metadata is recovered from bounded SFNT table reads
  before matching; platform configuration remains authoritative when present.
  Application assets can use `LazyFontProvider`: source metadata is registered
  without reading bytes, loader callbacks run outside provider locks on first
  family match, and family-scoped generations avoid invalidating unrelated
  portable text caches. `Canvas::addFontProvider` is the public attachment
  point. DirectWrite preserves the same lazy first-family-load behavior by
  bridging the winning provider family into a generation-tracked native custom
  collection. `RemoteFontProvider` extends the same contract for asynchronous
  hosts: matching queues coverage-aware source requests, the browser/native
  host performs I/O, and completion publishes memory faces while advancing only
  the affected family generation. The core enforces deduplication, concurrency,
  retry, deterministic candidate, and cumulative transfer-budget policy without
  embedding a networking stack. Optional source fingerprints distinguish
  harmless manifest replay from real content replacement, and per-attempt
  tokens prevent late callbacks from publishing stale bytes.

## ADR Index

- [ADR-001](ADR-001-library-first-modules.md): library-first target layout and build ownership.
- [ADR-002](ADR-002-renderer-abstraction.md): renderer abstraction and backend boundary.
- [ADR-003](ADR-003-text-architecture.md): pluggable text, font, and shaping stack.
- [ADR-004](ADR-004-validation-gates.md): architecture, correctness, and performance verification.
- [ADR-005](ADR-005-distribution-ready-library-packaging.md): packaging the project as installable static/shared libraries with public headers.
- [ADR-006](ADR-006-backend-neutral-command-layer.md): backend-neutral command layer to unblock cross-backend command replay (Vulkan).

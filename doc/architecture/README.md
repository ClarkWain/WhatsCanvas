# PrismCanvas Architecture

This directory captures the Phase 0 architecture decisions for turning PrismCanvas from a demo-first OpenGL playground into a reusable, backend-aware 2D canvas engine.

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
3. Graphics model layer
   - Backend-neutral draw state, blend, clip, sampling, and effect models.
4. Text layer
   - Font manager, shaping, layout, glyph cache, fallback.
5. Render abstraction layer
   - Renderer interfaces, device/resource abstractions, command execution.
6. Backend layer
   - OpenGL today, GLES / Metal / Vulkan / WebGPU later.
7. Platform layer
   - Windowing, system fonts, image decode, timing, diagnostics.

## Current Phase 0 Outcomes

- Top-level and example builds now consume a shared CMake module instead of duplicating PrismCanvas target setup.
- The reusable OpenGL implementation is built as a library target before demo executables.
- Canvas now depends on an `IRenderer` abstraction instead of embedding the concrete `Renderer` implementation directly.
- Canvas text measurement and render planning now flow through an `ITextBackend` abstraction, with shared `TextUtils` and `NativeText` modules under `src/text`.

## ADR Index

- [ADR-001](ADR-001-library-first-modules.md): library-first target layout and build ownership.
- [ADR-002](ADR-002-renderer-abstraction.md): renderer abstraction and backend boundary.
- [ADR-003](ADR-003-text-architecture.md): pluggable text, font, and shaping stack.
- [ADR-004](ADR-004-validation-gates.md): architecture, correctness, and performance verification.
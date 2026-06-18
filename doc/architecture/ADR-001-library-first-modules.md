# ADR-001: Library-First Module Layout

## Status

Accepted.

## Context

The project historically built demo executables by compiling WhatsCanvas source files directly into each sample. That made the demo target the unit of ownership instead of the canvas engine itself.

This blocked several medium-term goals:

- adding backend implementations without touching every sample build;
- shipping the engine as a library;
- building test-only or headless targets;
- reusing a single canonical dependency setup across root and sample projects.

## Decision

WhatsCanvas will move to a library-first build layout.

The first concrete step is already in place:

- the reusable OpenGL-backed implementation is built as `WhatsCanvasOpenGL`;
- the top-level demo links the library instead of owning the source list;
- standalone examples share the same target setup through `cmake/WhatsCanvasOpenGL.cmake`.

The intended next module split is:

- `whatscanvas_core`
- `whatscanvas_recording`
- `whatscanvas_backend_opengl`
- `whatscanvas_text_basic`
- `whatscanvas_capi`
- `whatscanvas_demo`

## Consequences

### Positive

- One canonical place defines WhatsCanvas source ownership and dependencies.
- Example projects stop drifting from the root build.
- Future tests and tools can link the same engine targets as the demos.

### Negative

- Current naming still reflects the existing OpenGL-specific implementation.
- The code is not yet fully split into backend-neutral core and backend-specific modules.

## Follow-up

1. Introduce `whatscanvas_core` and move backend-neutral data models there.
2. Move OpenGL-only sources under a backend-specific subtree.
3. Add install/export rules once the target graph stabilizes.
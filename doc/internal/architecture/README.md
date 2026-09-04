# WhatsCanvas Architecture

This directory records the architectural boundaries that remain relevant to
the current implementation. It is maintainer documentation, not part of the
published product contract.

The stable v1 product scope is complete. References to follow-up work in these
records describe optional refactoring, backend convergence, or additional
platform coverage; they are not open product-completion requirements.

## Core boundaries

1. **Public API** — stable application-facing types live under `include/wsc/`.
2. **Recording** — Canvas records immutable drawing state and backend-neutral
   commands; retained pictures cannot own context-bound GPU resources.
3. **Graphics model** — paint, blend, clip, sampling, filters, and resource
   ownership remain independent of a specific graphics API.
4. **Text** — font providers, resolution, shaping, layout, and rasterization
   are isolated behind pluggable text backends.
5. **Render abstraction** — `IRenderer` owns command submission while
   `IRenderDevice` owns backend resources and execution.
6. **Backends and hosts** — OpenGL, OpenGL ES, Software, Vulkan, and Metal stay
   behind the device boundary; windowing and platform lifecycle belong to host
   adapters rather than the Canvas API.

## Packaging decisions

The durable decisions from the original packaging ADR are:

- `include/wsc/` is the only installed public-header surface; headers under
  `src/` are implementation details.
- Installed CMake targets and `find_package(WhatsCanvas CONFIG REQUIRED)` are
  first-class product interfaces and must remain covered by an external
  package-consumer smoke test.
- Demo, example, benchmark, test, and tooling targets are repository assets,
  not installed library components.
- Backend-specific implementations may depend on the backend-neutral core;
  public/core code must not depend back on a graphics API or window library.
- `WhatsCanvas::OpenGL`, `WhatsCanvas::OpenGLES`, and
  `WhatsCanvas::Software` remain the supported consumer targets. Optional
  Vulkan is currently selected at runtime from the Vulkan-enabled OpenGL
  package rather than exposed as a separate installed target.
- Release artifacts must carry headers, libraries, CMake package metadata,
  licenses, and consistent version information. Release-specific evidence
  belongs under `doc/archive/releases/`.
- Further target splitting is an implementation backlog, not a promised public
  package layout.

Current packaging commands and consumer examples live in
[`GETTING_STARTED_AS_LIBRARY.md`](../../public/getting-started/GETTING_STARTED_AS_LIBRARY.md).
The repeatable maintainer procedure lives in
[`release-checklist.md`](../operations/release-checklist.md).

## Current decision records

- [Renderer boundary](renderer-boundary.md) — ownership and dependency boundary
  between Canvas, renderer, and backend devices.
- [Text architecture](text-architecture.md) — pluggable font, shaping, layout,
  and raster backends.
- [Backend-neutral command layer](command-layer.md) — shared draw
  representation used for cross-backend replay.
- [Windowed presentation](windowed-presentation.md) — implemented output-target
  and native presentation design, plus optional platform expansion areas.

Historical Phase 0 module sketches and the original validation/packaging ADRs
were retired after their lasting rules were absorbed here or into the current
validation and release procedures. Git history remains the audit trail.

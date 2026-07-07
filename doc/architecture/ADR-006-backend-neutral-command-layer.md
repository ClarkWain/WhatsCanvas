# ADR-006: Backend-Neutral Command Layer

## Status

Accepted / in progress.

## Context

The Vulkan backend (`VulkanRenderDevice`, branch `feature/vulkan-backend`) now
implements most of the `IRenderDevice` surface: device bring-up, offscreen
render targets and readback, a graphics pipeline for solid/gradient/blended
geometry, sampled textures, offscreen-layer compositing, and coverage-mask
clipping. Each capability is validated on real hardware (see the Vulkan roadmap).

The command layer is being decoupled from OpenGL in reviewable slices. Vulkan
already translates the real WhatsCanvas `Command` stream into backend-neutral
draw primitives for its offscreen execution path, and OpenGL now uses the shared
`CommandDrawListEncoder` for `renderCommandsToImageResource` layer/snapshot
replay. The regular onscreen OpenGL flush path still executes commands directly
through `Command::execute(RenderContext&)`, which keeps the production path
stable while the shared primitive stream is hardened.

## Decision

Introduce a backend-neutral draw representation that sits between command
recording and backend execution. Commands will emit backend-neutral draw
payloads; each backend (OpenGL today, Vulkan next) consumes those payloads and
translates them into its own API calls. Concretely:

1. Define backend-neutral draw primitives (a small tagged set): solid/gradient
   triangle batches, textured quads, layer composites, and coverage-mask clips,
   each carrying vertex/index data, resource handles (`ImageResource`,
   clip-mask resource), blend/sampling/scissor state, and transforms.
2. Change `Command::execute` to emit these primitives into a backend-neutral
   sink (a "draw list" / encoder) instead of calling OpenGL directly.
3. Give each backend a translator that consumes the draw list:
   - the OpenGL translator reproduces today's GL behavior (no visible change);
   - the Vulkan translator maps primitives onto the pipelines already built in
     `VulkanRenderDevice` (M3–M7).
4. Implement `renderCommandsToImageResource` on both backends by recording the
   command stream into a draw list and executing it against an offscreen target.

### Chosen strategy: incremental extraction, not a big-bang rewrite

Two strategies were considered (roadmap section 3):

- **A. Backend-neutral command layer (clean, larger).** Full refactor of the
  command/recording layer up front.
- **B. Parallel Vulkan command translator (pragmatic, faster).** Keep GL
  commands untouched; add a Vulkan-side path that reads the same `DrawData`
  payloads.

We adopt a phased path that starts like **B** and converges to **A**:

- The Vulkan-specific helper methods already added (M3–M7) are the initial
  translator surface and de-risk the pipeline shapes.
- We then extract the backend-neutral primitive set from the existing
  `DrawData` structures. Vulkan uses this direction for command execution, and
  OpenGL now consumes the shared encoder for offscreen layer/snapshot replay
  while the regular onscreen GL path remains direct.

This keeps the shipping OpenGL path working at every step and avoids a large,
risky simultaneous rewrite.

## Why Not Now (scope and risk)

Executing the full extraction in a single change would touch every `Draw*`
command and the `RenderContext` execution path, with real regression risk to the
OpenGL renderer that ships today. That is disproportionate to a single
increment. Recording the decision now lets the extraction proceed in reviewable
slices, each validated against the existing OpenGL smoke/pixel-hash gates and the
Vulkan `ctest -L vulkan` suite.

## Consequences

### Positive

- Unblocks `renderCommandsToImageResource` (and therefore true `saveLayer`
  command replay) on Vulkan, and any future backend (D3D, WebGPU, Metal).
- Commands stop being OpenGL-specific, completing the direction started in
  ADR-002 (renderer abstraction) and ADR-003 (text architecture).
- The Vulkan pipelines built in M3–M7 become the Vulkan translator, so little of
  that work is thrown away.

### Negative

- Adds an intermediate representation and a per-backend translator, i.e. one more
  layer to maintain.
- The extraction spans many `Draw*` types; until it completes, regular onscreen
  OpenGL flushing still uses its direct command execution path.
- Some GL fast paths (e.g. direct stencil clip work) must be re-expressed as
  backend-neutral primitives without regressing OpenGL output.

## Follow-up

1. Keep growing the backend-neutral primitive set from the existing `DrawData`
   shapes.
2. Continue hardening the OpenGL translator beyond offscreen layer/snapshot
   replay, validated by existing smoke and visual gates.
3. Keep Vulkan command execution aligned with the shared primitive semantics.
4. Expand `renderCommandsToImageResource` coverage on both backends as new
   command semantics are added.
5. Keep backend selection, demo coverage, and CI gates aligned with the supported
   backend matrix.

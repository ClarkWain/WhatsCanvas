# Vulkan Backend Roadmap

Status: **Draft / in progress** · Branch: `feature/vulkan-backend` · Done: M1, M2, M3 (solid fills), M4 (gradients + blend), M5 (images), M6 (layer composite), M7 (coverage-mask clip), M8 (windowed present)

This document tracks the work needed to bring the Vulkan render backend
(`VulkanRenderDevice`) to functional parity with the existing OpenGL backend
(`OpenGLRenderDevice`). It is the source of truth for scope, ordering, and the
validation gate for each milestone.

## 1. Goal

Make `RenderBackendType::Vulkan` a first-class, selectable backend that produces
pixel output equivalent to the OpenGL path for the full WhatsCanvas Canvas API,
validated on real hardware and guarded by automated tests.

"Parity" is defined against the 11 `IRenderDevice` entry points plus the five
draw-command families, not against internal implementation details.

## 2. Current state (baseline)

Completed on this branch:

- `VulkanRenderDevice` implements `IRenderDevice` and is wired through
  `RenderDeviceFactory` (`create` / `isBackendSupported` / `isAvailable`).
- The source compiles unconditionally as an inert stub; real Vulkan is only
  enabled with `-DWHATSCANVAS_ENABLE_VULKAN=ON` + a Vulkan SDK.
- `initializeBackend()` performs real device bring-up: instance, physical-device
  selection (discrete-GPU preferred), graphics queue-family discovery, logical
  device and graphics queue creation.
- `WhatsCanvasVulkanDeviceTests` (CTest label `vulkan`) validates bring-up on
  real hardware (verified on NVIDIA GeForce RTX 2080 Ti).

Not started: everything that produces or reads pixels. Ten of the eleven
`IRenderDevice` methods are stubs.

### 2.1 Parity gap

| `IRenderDevice` method | OpenGL | Vulkan | Milestone |
| --- | --- | --- | --- |
| `initializeBackend` | full (state + 5 shader programs + buffers) | device bring-up + render core (M1) | M1 ✅ |
| `readPixelsRGBA` | `glReadPixels` + flip | image->staging->host copy (M2) | M2 ✅ |
| `createRenderTarget` | FBO + texture + stencil | offscreen image + view + render pass + framebuffer (M2) | M2 ✅ |
| `createImageResourceRGBA` | GL texture | sampled VkImage + upload (M5) | M5 ✅ |
| `createImageResourceFromImageData` | GL texture + mipmap | sampled VkImage (M5) | M5 ✅ |
| `updateImageResourceRGBA` | partial texture update | staging copy (M5) | M5 ✅ |
| `wrapExternalImageResource` | wrap handle | stub | M8 |
| `createClipMaskResource` | AA coverage mask | clip resource + coverage-mask draw (M7) | M7 ✅ |
| `resourceStats` | live counts | render-target + texture counts | M2/M5 ✅ |
| `renderCommandsToImageResource` | offscreen replay | native compositeLayer (M6); command replay pending §3 | M6 ◑ |
| Draw commands (points/lines/path/image/text) | 5 GL programs | solid triangle pipeline (M3) | M3–M5 |

## 3. Architectural constraint (must decide before M3)

The five draw commands (`DrawPoints`, `DrawLines`, `DrawPath`, `DrawImage`,
`DrawText`) currently call OpenGL directly inside `Command::execute(RenderContext)`.
They are GL-coupled and cannot be reused by Vulkan as-is. ADR-002 introduced the
device abstraction, but the command/recording layer is still GL-specific.

Two viable strategies:

- **A. Backend-neutral command layer (clean, larger).** Refactor commands to emit
  backend-neutral draw primitives (vertex/index/uniform payloads + pipeline
  state) that each backend consumes. Highest long-term value; unblocks Metal/D3D
  too. Larger up-front refactor and regression risk to the shipping GL path.
- **B. Parallel Vulkan command translator (pragmatic, faster).** Keep GL commands
  untouched; add a Vulkan-side translation that reads the same `DrawData`
  payloads and records Vulkan draws. Faster to a visible result; some duplicated
  dispatch logic.

Recommendation: start with **B** to reach visible parity quickly (M3–M5), then
extract the common seam toward **A** once the Vulkan pipeline shapes are known.
This decision is a hard dependency for M3 and should be recorded as an ADR.
Recorded in [ADR-006](architecture/ADR-006-backend-neutral-command-layer.md).

## 4. Milestones

Each milestone lists deliverables and a concrete validation gate. Gates should be
added to CTest under the `vulkan` label so regressions are caught automatically.

### M1 — Render core bring-up · **Done**
Depends on: device bring-up (done).
- Vulkan Memory Allocator (or a minimal allocator) for device/host memory.
- Command pool + command buffer allocation; fences/semaphores for submission.
- A single-time-submit helper and a per-frame submit path.
- Extend `resourceStats` scaffolding.
Gate: unit test allocates a buffer, submits an empty command buffer, waits on a
fence — all succeed on hardware. **Met** (covered by M2 test path).

### M2 — Offscreen render target + readback · **Done**
Depends on: M1.
- `createRenderTarget(w,h)`: offscreen `VkImage` (color, optionally
  depth/stencil), `VkImageView`, `VkFramebuffer`, a clear-only render pass.
- `IRenderTarget` begin/activate/end semantics mapped to render-pass begin/end.
- `readPixelsRGBA`: copy the color image to a host-visible staging buffer,
  handle row layout and top-left origin to match OpenGL output.
Gate: clear the target to a known color, read it back, assert exact RGBA — mirror
of the existing pixel-hash smoke approach. First real Vulkan pixels.
**Met** by `WhatsCanvasVulkanRenderTargetTests` on NVIDIA RTX 2080 Ti (exact
RGBA for a solid fill and a render-pass clear-to-zero).

### M3 — First geometry: solid fills (points/lines/path fill) · **In progress (solid fills done)**
Depends on: M2 and the §3 decision.
- SPIR-V shaders compiled from GLSL with `glslc` (add a CMake shader-compile
  step; embed or load `.spv`). **Done**: `solid.vert`/`solid.frag` compiled to an
  embedded SPIR-V header (`shaders/SolidShaderSpv.h`).
- Graphics pipeline(s) for solid-colored triangles/lines/points; vertex/index
  buffers; a push-constant or UBO for the transform/color. **Done** for a
  triangle-list solid pipeline (per-vertex color, dynamic viewport/scissor).
- Translate `DrawPoints`, `DrawLines`, and solid `DrawPath` fill into Vulkan draws.
  *Pending*: wired via `renderSolidTriangles`; command-layer hookup still to come.
- Blend state matching GL `SRC_ALPHA, ONE_MINUS_SRC_ALPHA` default. **Done**.
Gate: render a filled polygon + polyline offscreen; pixel-compare (fuzzy) against
the OpenGL output of the same scene. **First step met** by
`WhatsCanvasVulkanSolidGeometryTests` (triangle interior = fill color, corner =
clear) on NVIDIA RTX 2080 Ti.

### M4 — Paint features on geometry · **In progress (gradients + blend done)**
Depends on: M3.
- Per-Paint anti-aliased fill/stroke (coverage feathering) to match GL analytic AA.
  *Pending*.
- Linear/radial multi-stop gradients (fragment evaluation; texel-buffer or UBO).
  **Done** for per-vertex color interpolation (`renderGradientTriangles`);
  fragment texel-buffer multi-stop still pending.
- Blend modes (Porter-Duff subset + Add/Multiply/Screen). **Done**: pipelines
  cached per (topology, blend mode); `renderBlendedOverlay` validates SrcOver/Src/
  Add/Multiply/Screen via draw-over-draw.
- Alpha, stroke mesh (reuse Polyline2D tessellation output). *Pending*.
Gate: fuzzy pixel-compare of an AA + gradient + blend scene vs OpenGL.
**First step met** by `WhatsCanvasVulkanPaintTests` (gradient interpolation,
SrcOver and Add results within tolerance) on NVIDIA RTX 2080 Ti.

### M5 — Images and text · **In progress (images done)**
Depends on: M4.
- `createImageResourceRGBA` / `createImageResourceFromImageData` / `updateImageResourceRGBA`:
  `VkImage` + `VkImageView` + `VkSampler`, staging upload, optional mipmaps.
  **Done** (mipmap generation still pending; channels 1/2/3 expanded to RGBA).
- `DrawImage`: sampling modes (Linear/Nearest/Mipmap), tile modes
  (Clamp/Repeat/Mirror/Decal), tint, color matrix. *Pending* (basic textured
  quad with nearest/linear + clamp done via `renderTexturedQuad`).
- `DrawText`: sample the existing GPU glyph atlas as a Vulkan sampled image.
  *Pending*.
Gate: fuzzy pixel-compare of an image + text scene vs OpenGL. **First step met**
by `WhatsCanvasVulkanTextureTests` (2x2 texture sampled into quadrants + partial
update) on NVIDIA RTX 2080 Ti.

### M6 — Offscreen command replay (saveLayer) · **Mechanism done; generic replay pending §3**
Depends on: M5.
- `renderCommandsToImageResource`: record a command list into an offscreen image
  and return it as a `SharedImageResource` for `saveLayer` composition.
  *Pending*: the WhatsCanvas `Command` objects call OpenGL directly and cannot be
  replayed on Vulkan until the backend-neutral command layer lands (§3 / ADR).
- Vulkan-native saveLayer mechanism: **Done** via `compositeLayer`, which samples
  an already-rendered offscreen layer and composites it over a background with a
  layer alpha (fragment push-constant).
Gate: a `saveLayer` scene composites correctly vs OpenGL. **Mechanism met** by
`WhatsCanvasVulkanLayerTests` (red layer @50% over blue -> purple) on NVIDIA RTX
2080 Ti.

### M7 — Anti-aliased path clipping · **Mechanism done**
Depends on: M6.
- `createClipMaskResource`: **Done** — returns a Vulkan clip-mask resource
  holding the analytic-AA path data.
- Coverage-mask draw: **Done** via `renderClippedSolid`, which samples a coverage
  mask (red channel) and modulates the fill alpha per fragment, giving
  path-shaped clipping. True analytic-AA feathering and nested-clip intersection
  are follow-ups; the mask-multiply mechanism matches the GL clip model.
- Rectangular clip fast path via `VkRect2D` scissor. *Pending* (dynamic scissor
  already available in all pipelines).
Gate: fuzzy pixel-compare of the clip-path scene vs OpenGL. **Mechanism met** by
`WhatsCanvasVulkanClipTests` (green fill clipped to a triangle mask: center
green, corner clear) on NVIDIA RTX 2080 Ti.

### M8 — Windowed presentation + external images · **Present done (standalone)**
Depends on: M2 (swapchain can proceed in parallel after M2).
- GLFW Vulkan surface (`glfwCreateWindowSurface`) + swapchain + present queue.
  **Done** as a standalone example (`examples/vulkan_present`): instance with the
  GLFW surface extensions, surface, present-capable device, swapchain, and a
  cleared frame acquired/submitted/presented with semaphores + a fence.
- Frame loop: acquire/record/submit/present with proper synchronization + resize.
  *Partial*: single-frame present; a continuous loop + resize/recreate is a
  follow-up.
- `wrapExternalImageResource` for externally-provided images. *Pending*: the
  `ImageResourceHandle` is 32-bit and cannot carry a 64-bit `VkImage`; needs an
  interface change.
Note: presentation needs its own instance/device (surface extensions), so it is
not wired into `VulkanRenderDevice` (whose instance is headless). Not a CTest gate
(windowed present is environment dependent); verified manually on NVIDIA RTX 2080
Ti (3 swapchain images, B8G8R8A8_UNORM).

### M9 — Integration, selection, and CI
Depends on: M3+ (progressively).
- Allow selecting the Vulkan backend at runtime/build (factory + demo wiring).
- Add a Vulkan smoke gate to CI where a Vulkan-capable runner exists (or software
  Vulkan / lavapipe as a fallback).
- Documentation: update README backend section from "reserved" to "experimental".
Gate: `ctest -L vulkan` green; CI job green.

## 5. Ordering and dependencies

```
device bring-up (done)
      -> M1 render core
            -> M2 render target + readback
                  -> M3 solid geometry ---> M4 paint features -> M5 images/text -> M6 saveLayer -> M7 clip
                  -> M8 swapchain/present (parallel after M2)
                                                                          -> M9 integration + CI
```

## 6. Cross-cutting concerns

- **Shaders:** add a CMake step invoking `glslc` (already installed with the SDK)
  to compile GLSL → SPIR-V; decide embed-as-header vs load-from-file.
- **Validation layers:** enable `VK_LAYER_KHRONOS_validation` in debug builds and
  fail tests on validation errors.
- **Coordinate/clip origin:** Vulkan clip space Y is inverted vs OpenGL; normalize
  so pixel output matches the OpenGL reference (readback origin already handled in
  M2).
- **Parity signal:** prefer the existing fuzzy-PPM / pixel-hash comparison
  infrastructure so Vulkan scenes are checked against OpenGL references directly.
- **Keep GL green:** every milestone must leave the OpenGL path and the default
  (Vulkan-off) build unchanged and passing.

## 7. Definition of done (parity)

- All 11 `IRenderDevice` methods implemented for Vulkan.
- All five draw-command families render on Vulkan.
- A representative scene set (fills, AA, gradients, shadows, images, text, clip,
  saveLayer) matches OpenGL within the fuzzy-compare tolerance.
- `ctest -L vulkan` passes on hardware; CI covers it where a runner allows.
- README documents Vulkan as an available (experimental) backend.

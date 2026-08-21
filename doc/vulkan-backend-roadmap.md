# Vulkan Backend Roadmap

Status: **Functional parity complete** · Done: M1, M2, M3 (solid fills), M4 (gradients + blend), M5 (images), M6 (layer composite), M7 (coverage-mask clip), M8 (windowed present), plus command-stream translation (path/points/lines/image/text/shadow), full 14-mode blend parity across all pipelines, analytic-AA coverage, mipmapped sampling, complete clip translation (all fill types), GPU image/backdrop filters, and `wrapExternalImageResource` (IRenderDevice 12/12).

This document tracks the work needed to bring the Vulkan render backend
(`VulkanRenderDevice`) to functional parity with the existing OpenGL backend
(`OpenGLRenderDevice`). It is the source of truth for scope, ordering, and the
validation gate for each milestone.

## 1. Goal

Make `RenderBackendType::Vulkan` a first-class, selectable backend that produces
pixel output equivalent to the OpenGL path for the full WhatsCanvas Canvas API,
validated on real hardware and guarded by automated tests.

"Parity" is defined against the 12 required `IRenderDevice` entry points plus the five
draw-command families, not against internal implementation details.

## 2. Current state (baseline)

Completed in the current tree:

- `VulkanRenderDevice` implements `IRenderDevice` and is wired through
  `RenderDeviceFactory` (`create` / `isBackendSupported` / `isAvailable`).
- The source compiles unconditionally as an inert stub; real Vulkan is only
  enabled with `-DWHATSCANVAS_ENABLE_VULKAN=ON` + a Vulkan SDK.
- `initializeBackend()` performs real device bring-up: instance, physical-device
  selection (discrete-GPU preferred), graphics queue-family discovery, logical
  device and graphics queue creation.
- `WhatsCanvasVulkanDeviceTests` (CTest label `vulkan`) validates bring-up on
  real hardware (verified on NVIDIA GeForce RTX 2080 Ti).

All twelve required `IRenderDevice` methods are implemented, and the full draw-command
stream (path / points / lines / image / text / shadow) translates to Vulkan via
the backend-neutral command layer (ADR-006). See
[vulkan-backend-status.md](vulkan-backend-status.md) for the current capability
matrix and test inventory.

### 2.1 Capability matrix

| `IRenderDevice` method | OpenGL | Vulkan | Milestone |
| --- | --- | --- | --- |
| `initializeBackend` | full (state + 5 shader programs + buffers) | device bring-up + render core (M1) | M1 ✅ |
| `readPixelsRGBA` | `glReadPixels` + flip | image->staging->host copy (M2) | M2 ✅ |
| `createRenderTarget` | FBO + texture + stencil | offscreen image + view + render pass + framebuffer (M2) | M2 ✅ |
| `createImageResourceRGBA` | GL texture | sampled VkImage + upload (M5) | M5 ✅ |
| `createImageResourceFromImageData` | GL texture + mipmap | sampled VkImage (M5) | M5 ✅ |
| `updateImageResourceRGBA` | partial texture update | staging copy (M5) | M5 ✅ |
| `wrapExternalImageResource` | wrap handle | non-owning wrapper over a foreign VkImage (64-bit handle) | ✅ |
| `createClipMaskResource` | AA coverage mask | clip resource + coverage-mask draw (M7) | M7 ✅ |
| `resourceStats` | live counts | render-target + texture counts | M2/M5 ✅ |
| `renderCommandsToImageResource` | offscreen replay | command translation into an owned sampled texture | ✅ |
| Draw commands (points/lines/path/image/text/shadow) | GL programs | command translation to solid/textured/gradient/clip pipelines | ✅ |

## 3. Architectural decision (implemented)

The recorded commands retain the OpenGL execution path while also exposing the
backend-neutral draw data consumed by Vulkan. Vulkan translates the same command
stream into backend-specific pipelines through the command layer; it does not
call OpenGL from the Vulkan path. This is the implemented decision recorded in
[ADR-006](architecture/ADR-006-backend-neutral-command-layer.md).

The remaining architectural follow-up is to reduce duplicated backend-specific
translation and extend the seam to future Metal/D3D/WebGPU backends. That is
not a prerequisite for the current Vulkan implementation.

## 4. Milestones

Each milestone lists deliverables and a concrete validation gate. Gates should be
added to CTest under the `vulkan` label so regressions are caught automatically.

### M1 — Render core bring-up · **Done**
Depends on: device bring-up (done).
- Vulkan Memory Allocator (or a minimal allocator) for device/host memory.
- Command pool + command buffer allocation; fences/semaphores for submission.
- A single-time-submit helper and a per-frame submit path.
- Keep `resourceStats` tracking aligned with newly added resource types.
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

### M3 — First geometry: solid fills (points/lines/path fill) · **Done**
Depends on: M2 and the §3 decision.
- SPIR-V shaders compiled from GLSL with `glslc` and embedded as headers.
  **Done**: `solid.vert`/`solid.frag` are represented by the checked-in
  `shaders/SolidShaderSpv.h`; future shader changes must regenerate that header
  and keep the source/header pair in sync.
- Graphics pipeline(s) for solid-colored triangles/lines/points; vertex/index
  buffers; a push-constant or UBO for the transform/color. **Done** for a
  triangle-list solid pipeline (per-vertex color, dynamic viewport/scissor).
- Translate `DrawPoints`, `DrawLines`, and solid `DrawPath` fill into Vulkan draws.
  **Done** through the backend-neutral command/draw-list translation path; the
  corresponding command and solid-geometry tests cover points, lines, and path
  geometry.
- Blend state matching GL `SRC_ALPHA, ONE_MINUS_SRC_ALPHA` default. **Done**.
Gate: render a filled polygon + polyline offscreen; pixel-compare (fuzzy) against
the OpenGL output of the same scene. Covered by
`WhatsCanvasVulkanSolidGeometryTests` and the ADR-006 command-translation tests
on NVIDIA RTX 2080 Ti.

### M4 — Paint features on geometry · **Done; broader parity hardening ongoing**
Depends on: M3.
- Per-Paint anti-aliased fill/stroke (coverage feathering) is **done** through
  the analytic-AA coverage path; broader visual parity scenes remain hardening.
- Linear/radial multi-stop gradients are **done** through fragment evaluation,
  with UBO-backed stops and clamp/repeat/mirror/decal tile modes.
- Blend modes are **done** across the Canvas set, including the Porter-Duff
  modes plus Add/Multiply/Screen; Vulkan blend tests cover the mappings.
- Alpha and stroke geometry are **done** in the command translation path;
  additional stress scenes remain validation work.
Gate: fuzzy pixel-compare of an AA + gradient + blend scene vs OpenGL.
Covered by `WhatsCanvasVulkanPaintTests`, `WhatsCanvasVulkanAATests`,
`WhatsCanvasVulkanBlendModeTests`, and gradient command tests on NVIDIA RTX
2080 Ti.

### M5 — Images and text · **Done; broader Canvas parity hardening ongoing**
Depends on: M4.
- `createImageResourceRGBA` / `createImageResourceFromImageData` / `updateImageResourceRGBA`:
  `VkImage` + `VkImageView` + `VkSampler`, staging upload, optional mipmaps.
  **Done**, including optional mipmap generation and channel normalization.
- `DrawImage`: sampling modes (Linear/Nearest/Mipmap), tile modes
  (Clamp/Repeat/Mirror/Decal), tint, and color matrix. **Done** through the
  textured-quad and command translation paths.
- `DrawText`: vector text and glyph-atlas text are supported through the Vulkan
  command and sampled-texture paths; dirty-rect atlas updates are covered by
  `WhatsCanvasVulkanTextTests`.
Gate: image + text coverage is exercised by the texture, text, image-color,
gradient, mipmap, and command-translation tests on NVIDIA RTX 2080 Ti.

### M6 — Offscreen command replay (saveLayer) · **Done**
Depends on: M5.
- `renderCommandsToImageResource`: record a command list into an offscreen image
  and return it as a `SharedImageResource` for `saveLayer` composition.
  **Done** through the backend-neutral command translation path.
- Vulkan-native saveLayer mechanism: **Done** via `compositeLayer`, which samples
  an already-rendered offscreen layer and composites it over a background with a
  layer alpha (fragment push-constant).
Gate: a `saveLayer` scene composites correctly vs OpenGL. Covered by
`WhatsCanvasVulkanLayerTests`, `WhatsCanvasVulkanCommandTests`, and the render
target pool tests on NVIDIA RTX 2080 Ti.

### M7 — Anti-aliased path clipping · **Done; broader parity hardening ongoing**
Depends on: M6.
- `createClipMaskResource`: **Done** — returns a Vulkan clip-mask resource
  holding the analytic-AA path data.
- Coverage-mask draw: **Done** via `renderClippedSolid`, which samples a coverage
  mask (red channel) and modulates the fill alpha per fragment, giving
  path-shaped clipping with analytic-AA coverage and nested-clip intersection.
- Rectangular clip fast path uses the dynamic scissor state where applicable.
Gate: fuzzy pixel-compare of the clip-path scene vs OpenGL. Covered by
`WhatsCanvasVulkanClipTests` and `WhatsCanvasVulkanClipCommandTests`, including
solid, text, gradient, image, point, and line fills.

Vulkan covers text as vector geometry and can render glyph-atlas text quads
through the sampled texture pipeline. Dirty-rect atlas texture updates are
covered by `WhatsCanvasVulkanTextTests`; broader Canvas-level glyph-atlas text
parity, text shadows, clipped atlas text, and larger text scenes remain good
hardening targets.

### M8 — Windowed presentation + external images · **Done; platform hardening ongoing**
Depends on: M2 (swapchain can proceed in parallel after M2).
- GLFW Vulkan surface (`glfwCreateWindowSurface`) + swapchain + present queue.
  **Done** both in the standalone integration harness
  (`tests/integration/vulkan_present`) and in
  the Win32 Canvas `OutputTarget::ToWindow(...)` path.
- Frame loop: acquire/record/submit/present with proper synchronization + resize.
  *Partial*: single-frame present; a continuous loop + resize/recreate is a
  follow-up.
- `wrapExternalImageResource` for externally-provided images. **Done** with a
  64-bit `ImageResourceHandle`; `WhatsCanvasVulkanExternalImageTests` and
  `WhatsCanvasVulkanWrapExternalTests` cover round-tripping and wrapping.
Canvas-level Win32 presentation is now wired through `VulkanRenderDevice` via
`OutputTarget::ToWindow(...)` and `Canvas::present()`. The lower-level
`tests/integration/vulkan_present` remains a standalone swapchain validation aid.
Cross-platform surface support and broader resize/device-loss coverage remain
follow-ups. This is not a CTest gate because windowed present is environment
dependent; the Win32 path was verified manually on NVIDIA RTX 2080 Ti (3
swapchain images, B8G8R8A8_UNORM).

### M9 — Integration, selection, and CI · **Done**
Depends on: M3+ (progressively).
- Allow selecting the Vulkan backend at runtime/build (factory + demo wiring).
  **Done**: public `Canvas::isBackendAvailable(Backend::Vulkan)` /
  `Canvas::create(Backend::Vulkan, w, h)`
  create a Vulkan-backed Canvas that renders off-screen through the shared
  command layer; `Renderer` routes its main-target flush to
  `IRenderDevice::executeCommands` for devices that report
  `usesDeviceCommandExecution()`. Returns null / false when Vulkan is not
  compiled in, so callers fall back cleanly (`WhatsCanvasVulkanBackendSelectionTests`,
  label `unit;vulkan`).
- Add a Vulkan smoke gate to CI where a Vulkan-capable runner exists (or software
  Vulkan / lavapipe as a fallback). **Done**: the `vulkan` CI job builds the
  Vulkan-enabled configuration (hard gate — embedded SPIR-V + translation must
  compile/link) and runs `ctest -L vulkan` on Mesa lavapipe as a best-effort
  step.
- Documentation: keep README and the backend status page aligned with the
  optional, implemented Vulkan backend, its build switch, and platform limits.
  **Done**: README documents `Canvas::create(Backend::Vulkan, ...)` and the
  Vulkan CI gate.
Gate: `ctest -L vulkan` green; CI job green. **Met** (hardware-verified on an
NVIDIA GTX 1060; CI build gate + lavapipe best-effort run).

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

- **Shaders:** keep the checked-in embedded SPIR-V headers synchronized with
  their GLSL sources; shader regeneration remains a development-time step.
- **Validation layers:** use `VK_LAYER_KHRONOS_validation` for debug/manual
  validation of presentation and resource-lifetime changes; the normal Vulkan
  build/test gate must also remain usable on software Vulkan where available.
- **Coordinate/clip origin:** Vulkan clip space Y is inverted vs OpenGL; normalize
  so pixel output matches the OpenGL reference (readback origin already handled in
  M2).
- **Parity signal:** prefer the existing fuzzy-PPM / pixel-hash comparison
  infrastructure so Vulkan scenes are checked against OpenGL references directly.
- **Keep GL green:** every milestone must leave the OpenGL path and the default
  (Vulkan-off) build unchanged and passing.

## 7. Definition of done (parity)

- All 12 required `IRenderDevice` methods implemented for Vulkan.
- All five draw-command families render on Vulkan.
- A representative scene set (fills, AA, gradients, shadows, images, text, clip,
  saveLayer) matches OpenGL within the fuzzy-compare tolerance.
- `ctest -L vulkan` passes on hardware; CI covers it where a runner allows.
- README documents Vulkan as an optional, implemented backend, including the
  `WHATSCANVAS_ENABLE_VULKAN` build requirement and current platform limits.

# Vulkan Backend — Progress Summary

Branch: `feature/vulkan-backend` · Validated on: NVIDIA GeForce RTX 2080 Ti ·
Enable with: `cmake -S . -B build -DWHATSCANVAS_ENABLE_VULKAN=ON` (Vulkan SDK
required). See [vulkan-backend-roadmap.md](vulkan-backend-roadmap.md) and
[ADR-006](architecture/ADR-006-backend-neutral-command-layer.md).

## What works today

The Vulkan backend is compiled unconditionally as an inert stub and only performs
real Vulkan work when `WHATSCANVAS_ENABLE_VULKAN` is set. Every capability below
is covered by a real-hardware test under the CTest `vulkan` label (8 tests,
`ctest -L vulkan`), and the default (Vulkan-off) build stays green.

| Milestone | Capability | Test |
| --- | --- | --- |
| Bring-up | Instance, physical-device selection (discrete preferred), logical device + graphics queue | `WhatsCanvasVulkanDeviceTests` |
| M1 | Command pool, single-time submit + fence, memory/buffer helpers | (exercised by M2) |
| M2 | Offscreen render target (image + view + clear render pass + framebuffer); `readPixelsRGBA` via image→staging→host | `WhatsCanvasVulkanRenderTargetTests` |
| M3 | Solid geometry through a real graphics pipeline; triangles / lines / points; SPIR-V shaders | `WhatsCanvasVulkanSolidGeometryTests` |
| M4 | Per-vertex gradient interpolation; fixed-function blend modes (SrcOver/Src/Add/Multiply/Screen) | `WhatsCanvasVulkanPaintTests` |
| M5 | Sampled textures (create/upload/partial update); textured-quad draw | `WhatsCanvasVulkanTextureTests` |
| M6 | Offscreen-layer compositing with layer alpha (saveLayer mechanism) | `WhatsCanvasVulkanLayerTests` |
| M7 | Coverage-mask path clipping | `WhatsCanvasVulkanClipTests` |
| ADR-006 | Backend-neutral `DrawList` + Vulkan translator (solid + textured + clip primitives) | `WhatsCanvasVulkanDrawListTests` |

## `IRenderDevice` parity

8 of 11 methods are implemented on Vulkan; 1 is a native mechanism with a
documented gap; 2 remain.

- Implemented: `initializeBackend`, `finalizeBackend`, `createRenderTarget`,
  `readPixelsRGBA`, `createImageResourceRGBA`, `createImageResourceFromImageData`,
  `updateImageResourceRGBA`, `createClipMaskResource`, `resourceStats`.
- Partial / native mechanism: `renderCommandsToImageResource` — Vulkan-native
  saveLayer via `compositeLayer`, but generic GL-`Command` replay is blocked (see
  below).
- Pending: `wrapExternalImageResource` — the `ImageResourceHandle` is 32-bit and
  cannot carry a 64-bit `VkImage`; needs an interface change (M8).

## Known gaps and why

- **Generic command replay** (`renderCommandsToImageResource`): the WhatsCanvas
  `Command` objects call OpenGL directly, so Vulkan cannot replay them. This is
  the coupling recorded in ADR-006; the fix is the backend-neutral command layer.
- **Windowed presentation (M8 swapchain)**: a standalone windowed present example
  (`examples/vulkan_present`) creates a GLFW surface + swapchain and presents a
  cleared frame; verified on NVIDIA RTX 2080 Ti. It is standalone because
  presentation needs surface extensions that `VulkanRenderDevice`'s headless
  instance does not enable. A continuous frame loop, resize handling, and
  integrating present into `VulkanRenderDevice` are follow-ups. Not a CTest gate
  (windowed present is environment dependent).
- **Textured/clip `DrawList` primitives (ADR-006 follow-up)**: solid, textured, and
  clip-fill primitives now land (`executeDrawList` records them in one render
  pass; clip-fill modulates the fill alpha by a coverage-mask texture's red
  channel). A teardown crash during development was root-caused with
  AddressSanitizer to a test lifetime bug (a `DrawList` holding a texture ref must
  be released before `finalizeBackend`), not a rendering bug.
- **Command translation (ADR-006)**: `executeCommands` reads a real WhatsCanvas
  `Command` stream and translates path fills/strokes, vertex-color (baked
  gradient) paths, points (sized), lines (width), and images (`DrawImage`
  dest-rect + UVs + alpha) to Vulkan draws (canvas->NDC via the same ortho the GL
  path uses), without touching the OpenGL command execution.
  `renderCommandsToImageResource` renders a command stream into an offscreen
  target and returns it as an owned sampled texture. Images also honor tint and a
  4x4 color matrix (fragment push constants). Fragment-evaluated **gradients**
  (linear/radial, up to 8 stops, clamp/repeat/mirror/decal tile modes via a UBO)
  translate ``DrawPathData`` shader gradients, matching the OpenGL gradient
  shader. Verified by ``WhatsCanvasVulkanCommandTests``,
  ``WhatsCanvasVulkanImageColorTests``, and ``WhatsCanvasVulkanGradientTests``,
  ASan-clean. **Text** is translated as vector triangle geometry: WhatsCanvas
  tessellates glyph outlines into local-space triangles (there is no glyph
  atlas), so text becomes a solid-color or shader-gradient fill using the same
  pipelines as paths, with the gradient evaluated in raw local space to match the
  OpenGL text shader (``WhatsCanvasVulkanTextTests``). Analytic-AA edge coverage
  feathers solid fills to match OpenGL (``WhatsCanvasVulkanAATests``). All 14
  Canvas **blend modes** (SrcOver/Src/Dst/Clear/SrcIn/DstIn/SrcOut/DstOut/
  SrcAtop/DstAtop/Xor/Add/Multiply/Screen) mirror ``glBlendFuncSeparate`` exactly
  (``WhatsCanvasVulkanBlendModeTests``). **Gaussian drop shadows** translate: the
  white silhouette is rendered offscreen, separable-Gaussian-blurred (CPU,
  identical kernel math to the GL passes), and composited as a tinted textured
  quad in stream order (``WhatsCanvasVulkanShadowTests``; both path silhouettes
  -- shapes + vector text -- and bitmap/glyph-atlas image silhouettes).
  **Clipped fills** translate: the clip paths are rasterized (with their
  analytic-AA coverage) into a coverage mask (nested clips intersect). Solid path
  fills, vector text, points and lines are drawn through the M7 clip pipeline
  (sampling the mask at each fragment's screen position). Clipped **gradient**
  and **image** fills are rendered in isolation and clipped on the CPU (alpha
  multiplied by the coverage) then composited -- mirroring the GL clip-mask
  fragment path (``WhatsCanvasVulkanClipCommandTests`` covers fills, text,
  points, gradient, and image).
- **Analytic-AA feathering / multi-stop fragment gradients**: the mechanisms are
  in place (coverage mask, vertex-color gradients); true AA feathering and
  texel-buffer multi-stop gradients are refinements.
- **Mipmapped image sampling**: ``createImageResourceFromImageData`` generates a
  full mip chain (blit) when requested, and ``DrawImageSampling::MipmapLinear``
  selects a trilinear sampler, matching the OpenGL mipmap path
  (``WhatsCanvasVulkanMipmapTests``).

## Next steps

1. Execute ADR-006 in reviewable slices (freeze primitive set → OpenGL translator
   validated by pixel-hash gates → Vulkan translator → `renderCommandsToImageResource`).
2. Root-cause the textured `DrawList` teardown crash (ASan) and re-land textured/
   clip primitives.
3. M8 windowed swapchain present as a dedicated windowed example + smoke.

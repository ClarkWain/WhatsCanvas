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
  4x4 color matrix (fragment push constants). Verified by
  `WhatsCanvasVulkanCommandTests` and `WhatsCanvasVulkanImageColorTests`,
  ASan-clean. Fragment shader gradients, clip, and text translation are
  follow-ups.
- **Analytic-AA feathering / multi-stop fragment gradients**: the mechanisms are
  in place (coverage mask, vertex-color gradients); true AA feathering and
  texel-buffer multi-stop gradients are refinements.

## Next steps

1. Execute ADR-006 in reviewable slices (freeze primitive set → OpenGL translator
   validated by pixel-hash gates → Vulkan translator → `renderCommandsToImageResource`).
2. Root-cause the textured `DrawList` teardown crash (ASan) and re-land textured/
   clip primitives.
3. M8 windowed swapchain present as a dedicated windowed example + smoke.

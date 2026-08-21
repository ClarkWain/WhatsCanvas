# Vulkan Backend — Progress Summary

Status snapshot: July 2026 · Validated on: NVIDIA GeForce RTX 2080 Ti and
NVIDIA GeForce GTX 1060 3GB ·
Enable with: `cmake -S . -B build -DWHATSCANVAS_ENABLE_VULKAN=ON` (Vulkan SDK
required). Vulkan is compiled into the OpenGL package target and selected at
runtime with `Canvas::Backend::Vulkan`. See [vulkan-backend-roadmap.md](vulkan-backend-roadmap.md) and
[ADR-006](architecture/ADR-006-backend-neutral-command-layer.md).

## What works today

The Vulkan source is compiled unconditionally, but it becomes a real backend
only when `WHATSCANVAS_ENABLE_VULKAN` is set and a Vulkan SDK is found. Without
that option it remains an inert factory entry. The current CMake configuration
registers 19 core Vulkan tests plus backend-selection and wrap-external coverage
(21 tests under the `vulkan` label; run `ctest -L vulkan`). The default
(Vulkan-off) build stays green.

| Milestone | Capability | Test |
| --- | --- | --- |
| Bring-up | Instance, physical-device selection (discrete preferred), logical device + graphics queue | `WhatsCanvasVulkanDeviceTests` |
| M1 | Command pool, single-time submit + fence, memory/buffer helpers | (exercised by M2) |
| M2 | Offscreen render target (image + view + clear render pass + framebuffer); `readPixelsRGBA` via image→staging→host | `WhatsCanvasVulkanRenderTargetTests` |
| M3 | Solid geometry through a real graphics pipeline; triangles / lines / points; SPIR-V shaders | `WhatsCanvasVulkanSolidGeometryTests` |
| M4 | Analytic-AA coverage, fragment-evaluated multi-stop gradients, and all 14 Canvas blend modes | `WhatsCanvasVulkanPaintTests`, `WhatsCanvasVulkanAATests`, `WhatsCanvasVulkanBlendModeTests` |
| M5 | Sampled textures (create/upload/partial update), mipmaps, sampling/tile modes, tint, color matrix, and textured-quad draw | `WhatsCanvasVulkanTextureTests`, `WhatsCanvasVulkanMipmapTests`, `WhatsCanvasVulkanImageColorTests` |
| M6 | Offscreen-layer compositing with layer alpha (saveLayer mechanism) | `WhatsCanvasVulkanLayerTests` |
| M7 | Analytic-AA coverage-mask path clipping, nested clip intersection, and rectangular scissor fast path | `WhatsCanvasVulkanClipTests`, `WhatsCanvasVulkanClipCommandTests` |
| ADR-006 | Backend-neutral `DrawList` + Vulkan translator (solid, textured, gradient, shadow, and clip primitives) | `WhatsCanvasVulkanDrawListTests`, `WhatsCanvasVulkanCommandTests` |
| Text / glyph atlas | Vector text geometry, shader-gradient text, glyph-atlas textured quads, and dirty-rect atlas texture updates | `WhatsCanvasVulkanTextTests` |
| Image filters | Image/backdrop blur, Clamp/Decal edges, color treatment, grain, adaptive downsampling, and Software pixel parity | `WhatsCanvasVulkanImageFilterTests` |
| OpenGL offscreen snapshots | `renderCommandsToImageResource` uses the shared `CommandDrawListEncoder` for layer/snapshot replay | `WhatsCanvasRenderTargetPoolTests` |

## `IRenderDevice` parity

All 12 required methods are implemented on Vulkan.

- Implemented: `initializeBackend`, `finalizeBackend`, `createRenderTarget`,
  `readPixelsRGBA`, `createImageResourceRGBA`, `createImageResourceFromImageData`,
  `updateImageResourceRGBA`, `createClipMaskResource`, `resourceStats`,
  `renderCommandsToImageResource`, `filterImageResource`,
  `wrapExternalImageResource`.
- `renderCommandsToImageResource` renders a `Command` stream into an offscreen
  target and returns it as an owned sampled texture through a GPU-local image
  copy (via the backend-neutral command translation).
- `filterImageResource` runs separable RGBA Gaussian blur in Vulkan fragment
  pipelines. Large kernels use a 2x blur target and a full-resolution restore
  pass; saturation, brightness, contrast, and stable grain run in the final
  pass without a CPU readback.
- `wrapExternalImageResource` wraps a foreign `VkImage` (carried by the now
  64-bit `ImageResourceHandle`) in a non-owning texture resource (owns only its
  view + sampler); the borrowed image is assumed RGBA8 in `SHADER_READ_ONLY`.
  `VulkanRenderDevice::nativeImageHandle` returns an owned texture's `VkImage`
  as a handle for round-tripping (``WhatsCanvasVulkanExternalImageTests``).

## Known gaps and why

- **Shared command layer still has room to grow**: Vulkan can replay real command
  streams, and OpenGL uses the shared command encoder for offscreen snapshots.
  The remaining architecture work is moving more regular OpenGL flush paths onto
  the same primitive stream without regressing the production renderer.
- **Windowed presentation (M8 swapchain)**: Canvas-level
  `OutputTarget::ToWindow(...)` + `Canvas::present()` is implemented for Win32
  Vulkan and exercised by `examples/present/vulkan_main.cpp`. The lower-level
  `tests/integration/vulkan_present` remains a standalone swapchain validation
  path.
  Cross-platform surfaces, broader resize/device-loss coverage, and a CTest
  gate remain follow-ups because windowed presentation is environment
  dependent.
- **DrawList lifetime and scope**: solid, textured, gradient, shadow, and
  clip-fill primitives are implemented (`executeDrawList` records them in one
  render pass; clip-fill modulates the fill alpha by a coverage-mask texture's
  red channel). A teardown crash during development was root-caused with
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
  ASan-clean. **Text** commands are translated as vector triangle geometry:
  WhatsCanvas tessellates glyph outlines into local-space triangles, while
  glyph-atlas text is handled through sampled image commands. Both paths become
  solid-color or shader-gradient fills using the Vulkan text/image pipelines,
  with the gradient evaluated in raw local space to match the OpenGL text shader
  (``WhatsCanvasVulkanTextTests``). Analytic-AA edge coverage
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
- **Analytic-AA feathering / multi-stop fragment gradients**: the coverage path
  and fragment-evaluated multi-stop gradients are implemented. Remaining work is
  broader representative-scene parity and device-specific hardening, not the
  basic rendering mechanism.
- **Mipmapped image sampling**: ``createImageResourceFromImageData`` generates a
  full mip chain (blit) when requested, and ``DrawImageSampling::MipmapLinear``
  selects a trilinear sampler, matching the OpenGL mipmap path
  (``WhatsCanvasVulkanMipmapTests``).
- **Glyph atlas text path needs broader scenes**: Vulkan can render glyph-atlas
  text quads through the sampled texture pipeline and validates dirty-rect atlas
  texture updates, but text shadows, clipped atlas text, and larger text
  pixel-parity scenes still need coverage.
- **Not the default backend**: normal builds still default to OpenGL or OpenGLES.
- **Cross-platform Canvas presentation is incomplete**: current Vulkan Canvas
  window presentation is wired for Win32. Other native surface types and
  broader resize/device-loss behavior remain future work.
- **Larger Canvas validation scenes remain**: the visual parity smoke now covers
  the core P0 Vulkan paths, but it should still grow into larger representative
  Canvas scenes and more text / image-effect combinations.
- **Native platform backends remain separate work**: Metal is still reserved, and
  DirectWrite/CoreText are text-backend adapter slots rather than render backends.

## Next steps

1. Expand representative Canvas-level validation scenes for Vulkan/OpenGL parity.
2. Move the remaining arbitrary clipped textured-image fallback onto a
   dual-texture Vulkan mask pipeline.
3. Continue migrating OpenGL execution paths to the shared command encoder in
   small, testable slices.
4. Expand the Win32 Canvas present path into a portable surface-aware path with
   broader resize and device-loss validation.

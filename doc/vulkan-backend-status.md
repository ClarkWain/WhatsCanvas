# Vulkan Backend Status

Branch: `fix/vulkan-review-hardening`

Enable with:

```bash
cmake -S . -B build-vulkan -DWHATSCANVAS_ENABLE_VULKAN=ON
```

The Vulkan backend is an **experimental selectable backend**. It is compiled as
an inert stub by default and performs real Vulkan work only when the build is
configured with `WHATSCANVAS_ENABLE_VULKAN=ON` and a Vulkan SDK is found.

## Current Capability

| Area | Status | Validation |
| --- | --- | --- |
| Device bring-up | Instance, physical-device selection, logical device, graphics queue, command pool | `WhatsCanvasVulkanDeviceTests` |
| Render target / readback | Offscreen image, view, render pass, framebuffer, image-to-staging `readPixelsRGBA` | `WhatsCanvasVulkanRenderTargetTests` |
| Solid geometry | Triangles, points, lines, path triangle command translation | `WhatsCanvasVulkanSolidGeometryTests`, `WhatsCanvasVulkanCommandTests` |
| Paint / blend | Per-vertex color, shader gradients, SrcOver / Src / Add / Multiply / Screen and Porter-Duff subset | `WhatsCanvasVulkanPaintTests`, `WhatsCanvasVulkanBlendModeTests`, `WhatsCanvasVulkanGradientTests` |
| Textures | RGBA upload, image-data upload, partial update, sampled draw, tint, color matrix | `WhatsCanvasVulkanTextureTests`, `WhatsCanvasVulkanImageColorTests` |
| External images | Non-owning sampled `ExternalImageDescriptor::vulkanImage(...)` wrapping | `WhatsCanvasVulkanTextureTests` |
| Layers | Native layer composite with layer alpha; command replay can render to sampled texture | `WhatsCanvasVulkanLayerTests` |
| Clip | Coverage-mask clip mechanism and command scissor translation aligned with OpenGL | `WhatsCanvasVulkanClipTests`, `WhatsCanvasBackendVisualParityTests` |
| Text commands | Vector text geometry translation, solid and shader-gradient fills | `WhatsCanvasVulkanTextTests`, `WhatsCanvasBackendVisualParityTests` |
| Present | Standalone GLFW Vulkan surface + swapchain + one-frame present example | `examples/vulkan_present` manual smoke |
| Visual parity | OpenGL and Vulkan render the same stable command stream and compare RGBA pixels | `WhatsCanvasBackendVisualParityTests` |

Local hardware validation on the current review branch included `ctest` passing
40/40 tests with Vulkan enabled, and the backend visual parity smoke reported
zero pixel difference on the tested stable scene.

## IRenderDevice Parity

| Method | Vulkan status |
| --- | --- |
| `initializeBackend` / `finalizeBackend` | Implemented |
| `readPixelsRGBA` | Implemented |
| `createRenderTarget` | Implemented |
| `createImageResourceRGBA` | Implemented |
| `createImageResourceFromImageData` | Implemented |
| `updateImageResourceRGBA` | Implemented |
| `wrapExternalImageResource` | Implemented for non-owning sampled Vulkan images |
| `createClipMaskResource` | Implemented as Vulkan clip-mask resource |
| `resourceStats` | Implemented for render targets and textures |
| `renderCommandsToImageResource` | Implemented through Vulkan command translation and texture snapshot |

## Remaining Gaps

- **Not the default backend**: normal builds still default to OpenGL or OpenGLES.
- **Canvas swapchain present is not integrated**: `examples/vulkan_present` proves
  real windowed present, but Canvas content is not yet rendered directly into a
  Vulkan swapchain.
- **Glyph atlas text path is not fully mirrored**: Vulkan text command support
  currently covers vector text geometry; the OpenGL glyph-atlas text rendering
  path still needs broader backend parity.
- **Parity scene coverage is still small**: current visual parity covers solid
  fills, alpha blend, shader gradient, scissor, and text geometry. It should grow
  to cover textures, saveLayer, clip masks, radial gradients, image filters, and
  larger Canvas validation scenes.
- **Native platform backends remain separate work**: Metal is still reserved, and
  DirectWrite/CoreText are text-backend adapter slots rather than render backends.

## Recommended Next Steps

1. Expand `WhatsCanvasBackendVisualParityTests` to include texture sampling,
   saveLayer, radial gradients, image color matrix, and clip masks.
2. Add a Vulkan Canvas-present path that renders real Canvas content into a
   swapchain, with resize/recreate handling.
3. Continue extracting the backend-neutral command layer so OpenGL, Vulkan, and
   future backends consume the same primitive stream.
4. Update README and release notes whenever a Vulkan capability moves from
   experimental helper coverage into Canvas-level parity.

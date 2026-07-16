# Direct3D Support Design Review

Status: July 13, 2026

Scope: this note reviews the current design shape for future Direct3D support in WhatsCanvas. It is intentionally about architectural readiness, not about implementation completeness.

## Executive Summary

WhatsCanvas is not ready to add a Direct3D backend cleanly yet.

The overall presentation abstraction is moving in a usable direction, especially the separation between window handles, swapchains, and wrap-external targets. However, three design seams should be resolved before any Direct3D implementation begins:

1. backend identity is inconsistent;
2. the public wrap-external API is not actually future-proof for D3D;
3. command translation does not yet have a single canonical path for non-GL backends.

## What Already Looks Sound

- `NativeSurface` plus `ISwapchain` is the right abstraction level for future D3D window presentation.
- `IRenderDevice` already separates offscreen rendering, presentation, and wrap-external targets.
- The Vulkan work proves that WhatsCanvas can support a non-GL backend without changing the high-level Canvas API.

## Main Issues

### 1. Backend Identity Is Still Unresolved

- The public API already exposes `Canvas::Backend::Direct3D`.
- The internal render-device factory does not model a Direct3D backend type yet.
- Tests currently lock in the expectation that Direct3D is unavailable.
- The windowed presentation design document uses `Backend::D3D12` in an example, which does not match the current public enum.

Why this matters:

The project still needs to decide whether "Direct3D" means D3D11, D3D12, or a family-level selector. That choice affects device creation, shader strategy, external interop, and test coverage.

### 2. The Public Interop Target API Will Still Need Changes

- Public `OutputTarget` currently supports host-owned OpenGL framebuffers and Vulkan images only.
- Internal `BackendRenderTarget` already reserves a `D3DTexture` kind.
- `Canvas::setOutputTarget()` only maps the GL and Vulkan cases.
- For D3D12 in particular, a descriptor shaped like `void* + format + width/height` is likely too weak because resource-state and ownership expectations usually need to be explicit.

Why this matters:

The current claim that future D3D support can land with zero public API changes is not true yet. The D3D wrap-external contract still needs to be defined.

### 3. There Is No Single Source of Truth for Command Translation

- Command objects still expose `execute(RenderContext&)`, which is a GL-style execution path.
- Device backends use a separate `usesDeviceCommandExecution()` flow.
- OpenGL offscreen replay already uses the shared draw-list encoder.
- Vulkan main-path execution translates the real command stream separately.
- The shared encoder still rejects several clipped command cases.

Why this matters:

A future D3D backend would otherwise have to either duplicate Vulkan's translation logic or depend on an intermediate representation that still has known semantic gaps.

## Recommended Order Before Any D3D Implementation

1. Choose the backend identity: D3D11, D3D12, or a split public/backend model.
2. Define the public wrap-external contract for D3D and decide whether D3D11 and D3D12 need different descriptors.
3. Promote one canonical backend-neutral command IR or translation path that both Vulkan and future D3D can share.
4. Only then start device bring-up, swapchain, and shader work.

## Short Recommendation

Do not start coding a Direct3D render device yet.

First stabilize the backend model, the external-target contract, and the command-translation seam. Once those three decisions are settled, the remaining work becomes implementation-heavy rather than architecture-heavy.

## Evidence Files

- `include/wsc/Canvas.h`
- `include/wsc/Surface.h`
- `src/canvas/Canvas.cpp`
- `src/render/IRenderDevice.h`
- `src/render/RenderDeviceFactory.h`
- `src/render/RenderDeviceFactory.cpp`
- `src/render/Surface.h`
- `src/render/CommandDrawListEncoder.cpp`
- `src/render/OpenGLRenderDevice.cpp`
- `src/render/vulkan/VulkanRenderDevice.cpp`
- `doc/windowed-presentation-design.md`
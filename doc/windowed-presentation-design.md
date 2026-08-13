# Windowed Presentation & Backend Selection — Design Discussion

Status: **Proposal / Discussion.** Core direction agreed; multiple slices landed.
**Implemented & validated:** the backend-neutral scaffolding (§3–§6), a unified
public `Canvas::setOutputTarget` (`Offscreen` / `OffscreenTexture` / `ToWindow` /
`GLFramebuffer` / `VulkanImageTarget`) + `present` / `resizeOutput`,
**software present on Windows (GDI)** and **Linux/X11**, **OpenGL host-owned
present (WGL; guarded GLX)**, **Vulkan windowed present** (present-ready
instance/device + swapchain, blitting the rendered image into the acquired
swapchain image; validated clean under the Khronos validation layer), and
**wrap-external into a host GL framebuffer or VkImage**. **Not yet:** Metal/D3D
swapchains and mobile surface lifecycle. Remaining API sketches are marked
"sketch".

This is the source of truth for the design rationale and remaining gaps around
on-screen presentation across backends. The current consumer-facing contract is
documented in [`GETTING_STARTED_AS_LIBRARY.md`](GETTING_STARTED_AS_LIBRARY.md).
It complements
[vulkan-backend-roadmap.md](vulkan-backend-roadmap.md) (which covers offscreen
rendering parity, already complete).

> Reader note: this is a design discussion as well as a status record. Any
> identifier in a design diagram or block marked **sketch**—for example `createWindowed`,
> `setBackend`, or `wrapBackendRenderTarget`—is not necessarily a current
> public API. For the current consumer-facing API, use
> [`GETTING_STARTED_AS_LIBRARY.md`](GETTING_STARTED_AS_LIBRARY.md):
> `setOutputTarget(...)`, `beginFrame()`, `endFrame()`, and `present()`.

## 1. Current state and remaining problem

WhatsCanvas now has a unified output-target API, but window presentation is
still platform- and backend-specific:

- **OpenGL** supports the unified `ToWindow` path where the platform swapchain
  adapter is available (WGL; guarded GLX on Linux). The common embedding path
  remains a host-owned current GL context plus `glfwSwapBuffers`/equivalent.
- **Vulkan** supports Canvas-level `ToWindow` + `present()` on Win32. The
  lower-level `examples/vulkan_present` remains a standalone swapchain test.
- **Software (CPU)** supports window blitting on Windows GDI and Linux/X11;
  otherwise it produces a CPU RGBA buffer for the host to display.
- **Metal / D3D** do not exist (factory returns `nullptr`).
- There is **no `setMode` / runtime backend switch**. The backend is fixed at
  `Canvas` construction, and `Canvas` is non-copyable.

Two independent asks motivated this design:

1. A way for **Vulkan (and others) to display in a window**, not just offscreen.
2. Ideally a **single, backend-neutral** way to pick GL / Vulkan / CPU / Metal.

## 2. Goals / Non-goals

**Goals**
- On-screen presentation for GPU backends without WhatsCanvas owning the OS
  window or the main loop (preserve the "embeddable renderer" positioning).
- A backend-neutral surface abstraction that future D3D / Metal slot into with
  **zero changes to public API**.
- Keep existing offscreen / GL users working unchanged.

**Non-goals**
- WhatsCanvas will **not** become a windowing framework (no bundled SDL, no
  owned event loop). That is the LÖVE model and conflicts with embeddability.
- Cheap per-frame backend hot-swapping (see §8 — it is inherently a rebuild).

## 3. Key decision: a two-layer design

Modeled on **Skia** (which itself ships a low-level "wrap an external target"
core plus an optional `sk_app` window helper), not on LÖVE (which owns the
window via SDL).

```
┌───────────────────────────────────────────────────────────────┐
│ Convenience layer (optional):  NativeSurface + ISwapchain       │
│   Canvas::createWindowed(NativeSurface, w, h) → present()       │
│   Library owns the swapchain; user opens a window, nothing more │
├───────────────────────────────────────────────────────────────┤
│ Core layer (source of truth):  wrap-external backend target     │
│   Canvas::wrapBackendRenderTarget(VulkanImageTarget{...})       │
│   Host owns instance/device/swapchain/present; library just     │
│   draws into the target it is handed                            │
└───────────────────────────────────────────────────────────────┘
```

- **Core layer** = Skia-style: maximum flexibility, embeddable, backend-neutral,
  library stays light. The convenience layer is built *on top of* it; the core
  never depends on the convenience layer.
- **Convenience layer** = the "I just want a window" path: the library builds the
  swapchain internally so the user only opens an OS window.

This aligns with WhatsCanvas's existing `wrapExternalTexture` /
`wrapExternalImageResource` / render-target-mode machinery — wrapping an external
backend render target is the same lineage.

## 4. Core layer — wrap-external (Skia-style)

The host owns the graphics stack and hands WhatsCanvas the per-frame target.

```cpp
// ⚠️ sketch
VkImage img = myEngine.acquireSwapchainImage();
canvas->wrapBackendRenderTarget(wsc::VulkanImageTarget{ img, format, w, h });
canvas->beginFrame();
/* draw */
canvas->endFrame();
myEngine.presentSwapchainImage(img);   // present is the host's job
```

Per backend the wrapped descriptor differs (`VulkanImageTarget{VkImage,…}`,
`GLFramebufferTarget{fbo,…}`, `MetalTextureTarget{id<MTLTexture>,…}`,
`D3DTarget{…}`), but the flow is identical. This is the most decoupled option and
the one that lets WhatsCanvas drop into existing engines / Qt / Vulkan apps with
no ownership conflict.

## 5. Convenience layer — NativeSurface + ISwapchain

The library takes a **neutral OS window handle** (never a backend-specific
surface) and builds the swapchain internally.

```cpp
// ⚠️ sketch
struct NativeSurface {
    enum class Platform { Win32, Xlib, Xcb, Wayland, Cocoa, Android };
    Platform platform;
    void* window;   // HWND / NSView* / xcb_window / ANativeWindow* / CAMetalLayer*
    void* display;  // HINSTANCE / Display* / wl_display* (when needed)
};

struct SwapchainConfig { bool vsync = true; int imageCount = 3; /* HDR/format later */ };

auto canvas = wsc::Canvas::createWindowed(
    wsc::NativeSurface::fromGlfw(window), 1280, 720,
    /*prefer=*/{ Backend::Vulkan, Backend::Metal, Backend::D3D12,
                 Backend::OpenGL, Backend::Software });

while (running) {
    canvas->beginFrame();
    /* draw */
    canvas->endFrame();
    canvas->present();          // resize: canvas->resize(w, h)
}
```

Device-layer abstraction each backend implements:

```cpp
// ⚠️ sketch
class ISwapchain {
public:
    virtual AcquiredImage acquire() = 0;    // this frame's render target
    virtual void present() = 0;
    virtual void resize(int w, int h) = 0;
    virtual ~ISwapchain() = default;
};

class IRenderDevice {  // new optional capability
    virtual std::unique_ptr<ISwapchain>
        createSwapchain(const NativeSurface&, const SwapchainConfig&) { return nullptr; }
    virtual bool supportsPresentation() const { return false; }
};
```

Rationale for a **neutral window handle** instead of a backend surface: surfaces
(`VkSurfaceKHR`, `IDXGISwapChain`, `CAMetalLayer`) are API-private and cannot be
shared across backends; the *only* input common to all backends is the OS window
itself. Convenience helpers `NativeSurface::fromGlfw/fromSDL/fromHWND` stay
header-only so the core library takes no hard window-library dependency.

## 6. Frame model — `present()` is a new output step, not a replacement

`beginFrame` / `endFrame` stay unchanged. `present()` is a **new step
peer to `readPixelsRGBA`** — both answer "where does this frame's result go":

| Step | Role | Offscreen | Windowed |
|---|---|---|---|
| `beginFrame()` | start a frame | ✅ | ✅ |
| `Renderer::flush()` *(internal)* | submit/execute commands | ✅ | ✅ |
| `endFrame()` | finalize, stats | ✅ | ✅ |
| **output** | where the result goes | `readPixelsRGBA()` | **`present()`** |

`Canvas::endFrame()` invokes the internal submission step; there is no public
`Canvas::flush()` method. Existing offscreen / GL users are unaffected;
windowed users add one `present()` after `endFrame()`.
Merging present into endFrame was rejected: it breaks the offscreen/windowed
frame symmetry and backward compatibility.

## 7. Per-backend mapping

Presentation is needed **only when rendering on-screen**; offscreen never calls
`present()`. On-screen, who does the present differs:

| Backend | On-screen present is… | Library does it, or host? |
|---|---|---|
| Vulkan | `vkQueuePresentKHR` | **library** |
| D3D11/12 | `IDXGISwapChain::Present()` | **library** |
| Metal | `presentDrawable` | **library** |
| Software | blit CPU buffer to window (`StretchDIBits`/`XPutImage`) | **library** |
| OpenGL / GLES | `swapBuffers` (WGL/GLX/EGL) | usually **host** (e.g. GLFW) |

Two abstraction leaks to accept:
- **OpenGL is the odd one out** — it is *host-owned* (context creation is tied to
  the window). Its `ISwapchain::present()` is a thin shell that delegates to the
  host swap (or is a no-op), rather than the library owning present.
- **Software** has no native present — it needs a CPU→window blit path per
  platform. Both differences hide behind `ISwapchain`; the public API stays
  uniform.

## 8. Runtime `setMode` is a rebuild, not a toggle

GPU resources (glyph atlas, images, render targets, tessellation/stroke caches)
are **device-owned and not portable across GL↔Vulkan↔…**. A `setBackend()` would
therefore tear down the device and re-upload every resource:

```cpp
// ⚠️ sketch — expensive, not per-frame
canvas->setBackend(Backend::OpenGL);  // destroy old device+resources → build new → re-upload
```

Realistic `setMode` = "recreate the renderer with a new device", best expressed as
create-time selection (`createWindowed(..., prefer={...})`) rather than a live
toggle.

## 9. Desktop vs mobile

Mobile has no desktop GL and no D3D; backend choice narrows, and WhatsCanvas
already has an **OpenGL ES backend** and [iOS build notes](IOS_BUILD_NOTES.md).

**Android**
- Backends: OpenGL ES (EGL) or Vulkan (`VK_KHR_android_surface`).
- Native handle: `ANativeWindow*` (from Java `Surface`/`SurfaceView`).
- Present: `eglSwapBuffers` (GLES) or swapchain present (Vulkan).
- **Hard part:** backgrounding **destroys the surface** (`surfaceDestroyed`); it
  must be recreated on resume → swapchain (and possibly resources) rebuilt.

**iOS**
- Backends: Metal (primary), OpenGL ES (deprecated), or Vulkan via MoltenVK.
- Native "surface": `CAMetalLayer` on a `UIView`.
- Present: `nextDrawable` → `presentDrawable` (Metal); EAGL for GLES.
- **Hard part:** layer size / `contentsScale` changes; app lifecycle.

These confirm the `NativeSurface` design ("platform enum + `void*` handles" must
hold `ANativeWindow*` / `CAMetalLayer*`, not just `HWND`). The real mobile
challenge is **surface lifecycle (loss/recreation)** — the same class of problem
as the existing `releaseResources()` / `initializeContext()` GL-context-loss
recovery, and the present layer must expose a hook for it.

## 10. Prior art

- **Skia** — does not own the window/present; core wraps a host-built backend
  render target (`SkSurfaces::WrapBackendRenderTarget` on a `GrDirectContext` for
  GL/Vulkan/Metal/D3D/Dawn); host builds the swapchain and presents. A separate
  `sk_app` tools layer wraps window+swapchain (not core). **This two-layer split
  is exactly the model adopted here.**
- **LÖVE (love2d)** — a framework that *owns* the window and main loop via SDL;
  present is entirely hidden. Rejected: it breaks embeddability.

WhatsCanvas is positioned closest to Skia (embeddable), hence the Skia-style core
plus an optional convenience layer.

## 11. Historical change inventory

This inventory records the original implementation plan. The completed slices
are summarized in §1 and §13; the remaining work is primarily platform and
backend expansion.

- **A. Instance** — Vulkan: build a *present-ready* instance (surface extensions)
  behind a windowed mode flag; keep the headless path for offscreen.
- **B. Surface** — new; needs an OS window handle. Core layer: host supplies the
  target; convenience layer: `NativeSurface` → backend builds the surface.
- **C. Device** — Vulkan: enable `VK_KHR_swapchain`, select a present-capable
  queue family, add present to device scoring/availability.
- **D. Swapchain** — new module: query caps/formats/present modes, create,
  and **recreate** on resize / `VK_ERROR_OUT_OF_DATE_KHR`.
- **E. Present loop** — acquire → submit → present with semaphores + in-flight
  fences; route `Renderer::flush()`'s device-command path to render into the
  acquired image and present instead of read back.
- **F. Abstraction** — `IRenderDevice::createSwapchain` + `ISwapchain`;
  `Canvas::wrapBackendRenderTarget` (core) and `Canvas::createWindowed` /
  `present()` / `resize()` (convenience).
- **G. Dependencies/platform** — keep the core free of window-library deps;
  `NativeSurface::from*` helpers are header-only; per-platform blit for Software.

## 12. Forward-compatibility check (D3D / Metal)

- ⚠️ A future backend must implement its full render-device/resource and command
  translation surface in addition to `createSwapchain` / `ISwapchain`; this is a
  design goal, not a claim that presentation alone is sufficient.
- ✅ The current public output model is intended to remain stable: user code can
  use `setOutputTarget(...)` and `present()` once a backend supports the target.
- ⚠️ `NativeSurface` must reserve enough platform fields (window/display/layer).
- ⚠️ Backend differences (vsync/present mode/color space/HDR) abstracted in a
  neutral `SwapchainConfig`; unmapped features degrade gracefully.

## 13. Suggested implementation order

1. ~~**Vulkan `ISwapchain`**~~ / **Software `ISwapchain`** — done first instead,
   since it needs no Vulkan SDK and validates the whole seam end-to-end
   (`SoftwareSwapchain`, GDI, Windows). Backend-neutral scaffolding + the public
   `Canvas` present API + `examples/software_present` also landed.
2. ~~**Vulkan `ISwapchain`**~~ **done** — the instance/device are made
   present-ready (surface + `VK_KHR_swapchain` extensions, present-capable
   graphics queue) and present blits the render device's offscreen image
   (`readbackImage`) into the acquired swapchain image. Validated on hardware
   via `examples/vulkan_canvas_present` (clean under the Khronos validation
   layer). Reuses the entire existing offscreen renderer.
3. ~~**Vulkan `wrapBackendRenderTarget`**~~ **done** — renders into a host-owned
   `VkImage` allocated on the canvas's Vulkan device (exposed via
   `Canvas::vulkanDevice()` etc.). `executeCommands` redirects into the external
   target. Verified via `tests/VulkanWrapExternalTests` (validation-clean).
   Importing a *foreign* host device is a further step.
4. ~~**OpenGL** host-owned thin-shell `ISwapchain`~~ **done** (WGL swap; GLX
   guarded/unverified) and **wrap-external into a host GL framebuffer** done.
5. Future: **D3D / Metal** against the same interface; mobile surface-lifecycle
   handling; verify the Linux X11/GLX paths.

## 14. Open questions and resolved decisions

- **Resolved:** swapchain acquisition remains an internal `ISwapchain` detail;
  the current `Canvas::present()` delegates to it after `endFrame()`.
- **Resolved:** `present()` is exposed on `Canvas`, not on a new public
  `WindowTarget` object.
- Color-space / sRGB handling across swapchain formats vs the existing
  `setGammaCorrect` model.
- Mobile surface-loss callback surface area (reuse `releaseResources` semantics?).

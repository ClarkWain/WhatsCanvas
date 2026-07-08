#pragma once

#include <memory>

// Backend-neutral presentation abstraction (design: windowed-presentation).
//
// This is the internal seam that lets each render backend present to an
// on-screen window without WhatsCanvas owning the OS window or the main loop.
// The public convenience layer (Canvas::createWindowed / present) and the
// Skia-style wrap-external core will be built on top of these types.
//
// NOTE: This is scaffolding. No backend implements a real swapchain yet; the
// default IRenderDevice hooks report "not supported" so existing offscreen/GL
// behavior is unchanged.

/// A neutral operating-system window handle. The only input common to every
/// graphics backend is the OS window itself; each backend builds its own
/// surface/swapchain from this. Convenience adapters (fromGlfw, fromHWND, ...)
/// will populate it without the core taking a window-library dependency.
struct NativeSurface
{
    enum class Platform
    {
        Win32,
        Xlib,
        Xcb,
        Wayland,
        Cocoa,
        Android,
    };

    Platform platform{Platform::Win32};
    void *window = nullptr;  ///< HWND / NSView* / xcb_window / ANativeWindow* / CAMetalLayer*
    void *display = nullptr; ///< HINSTANCE / Display* / wl_display* (when the platform needs it)
};

/// Neutral swapchain preferences. Backends map these to their own concepts and
/// gracefully degrade features they cannot honor.
struct SwapchainConfig
{
    bool vsync = true;
    int imageCount = 3;
};

/// The render target for the current frame, as handed back by a swapchain. The
/// handle is backend-specific (e.g. a VkImage, a GL framebuffer id, or an
/// id<MTLTexture>); `valid` is false when acquisition failed (e.g. the surface
/// is out of date and needs a resize/recreate).
struct AcquiredImage
{
    void *handle = nullptr;
    int width = 0;
    int height = 0;
    bool valid = false;
};

/// Backend-neutral on-screen presentation target. Each backend provides a
/// concrete implementation (Vulkan swapchain, GL host-owned shell, Metal
/// CAMetalLayer, software blit). Owned by the layer that drives the frame.
class ISwapchain
{
public:
    virtual ~ISwapchain() = default;

    /// Acquire this frame's render target. May return an invalid image when the
    /// surface is out of date; the caller should then resize() and retry.
    virtual AcquiredImage acquire() = 0;

    /// Present the most recently rendered frame to the window. Returns false
    /// when the surface became out of date (caller should resize()).
    virtual bool present() = 0;

    /// Rebuild for a new drawable size (window resize, or surface out of date).
    virtual void resize(int width, int height) = 0;
};

#pragma once

#include <memory>

#include "render/Surface.h"

// On-screen presentation for the OpenGL family. GL is host-owned: the host
// creates the window and GL context and makes it current; WhatsCanvas renders
// into the default framebuffer. The GL swapchain is a thin shell whose
// present() performs the platform buffer swap (WGL/GLX), so GL can use the same
// Canvas::attachPresentSurface/present() API as other backends.

namespace wsc::gl {

/// True when GL on-screen presentation (platform buffer swap) is available.
bool glPresentSupported();

/// Create a GL swapchain that swaps the buffers of the window in `surface`.
/// Returns nullptr on unsupported platforms or without a usable window handle.
std::unique_ptr<ISwapchain> makeGLSwapchain(const NativeSurface &surface, const SwapchainConfig &config);

} // namespace wsc::gl

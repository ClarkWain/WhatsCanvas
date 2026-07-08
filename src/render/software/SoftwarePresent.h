#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "render/Surface.h"

namespace wsc::software {

/// Fetches the latest rendered frame as top-left-origin RGBA8 bytes.
/// Returns false if no frame is available. Out-params carry the dimensions.
using PixelSource = std::function<bool(std::vector<unsigned char> &pixels, int &width, int &height)>;

/// True when on-screen software presentation is available on this platform
/// (currently Windows/GDI only).
bool softwarePresentSupported();

/// Blit top-left-origin RGBA8 pixels onto a device context, scaling to the
/// destination size. `hdc` is an opaque HDC (cast internally). Handles the
/// RGBA→BGRA channel order and top-down orientation GDI expects. Returns false
/// on unsupported platforms or invalid input. Exposed for testing.
bool blitRgbaTopDownToHdc(void *hdc, const unsigned char *rgba, int srcWidth, int srcHeight,
                          int dstWidth, int dstHeight);

/// Create a software on-screen swapchain that presents `source`'s frames to the
/// window in `surface`. Returns nullptr on unsupported platforms or when the
/// surface has no usable window handle.
std::unique_ptr<ISwapchain> makeSoftwareSwapchain(const NativeSurface &surface, const SwapchainConfig &config,
                                                  PixelSource source);

} // namespace wsc::software

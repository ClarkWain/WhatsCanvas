#pragma once

#include "text/ITextBackend.h"

#include <memory>
#include <string>

namespace wsc::text {

/// Raster mode for DirectWrite text output.
enum class DirectWriteRasterMode
{
    /// Default. Grayscale alpha coverage — safe for transparent surfaces,
    /// transforms, animation, and cross-backend consistency.
    Grayscale,
    /// ClearType RGB subpixel — only safe for axis-aligned text over a known
    /// opaque background. Must not be used when alpha compositing could cause
    /// colored fringes.
    ClearType
};

struct DirectWriteBackendOptions
{
    DirectWriteRasterMode rasterMode = DirectWriteRasterMode::Grayscale;
    /// When true, the backend reports available and constructs a real
    /// IDWriteFactory. When false, construction fails (returns nullptr).
    bool enableSystemFontFallback = true;
};

/// Query whether a real DirectWrite backend can be constructed on this platform.
bool isDirectWriteAvailable();

/// Create a DirectWrite text backend. Returns nullptr if DirectWrite is not
/// available on this platform.
std::unique_ptr<ITextBackend> createDirectWriteTextBackend(const DirectWriteBackendOptions &options = {});

} // namespace wsc::text

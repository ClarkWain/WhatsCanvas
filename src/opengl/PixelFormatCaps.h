#pragma once

#include <glad/glad.h>

/// Query and cache OpenGL pixel format support capabilities.
/// Used to determine if specific formats (HDR, float, sRGB, etc.)
/// are available before creating render targets or textures.
class PixelFormatCaps
{
public:
    /// Usage flags that can be combined for capability queries.
    enum Usage : int
    {
        SAMPLE      = 1 << 0,  ///< Can be sampled as a texture.
        RENDERTARGET = 1 << 1, ///< Can be used as a render target.
        LINEAR      = 1 << 2,  ///< Supports linear filtering.
        MSAA        = 1 << 3,  ///< Supports multisampling.
    };

    /// Internal format to query.
    enum Format : int
    {
        RGBA8,
        SRGB8_ALPHA8,
        RGBA16F,
        RGBA32F,
        R32F,
        DEPTH24_STENCIL8,
        DEPTH32F,
    };

    /// Initialize capability queries. Must be called after GL context creation.
    static void initialize();
    /// Clear context-specific capability results.
    static void reset();

    /// Check if a format supports the requested usage flags.
    static bool isSupported(Format format, int usageFlags);

    /// Get the GL internal format enum for a Format.
    static GLint toGLInternalFormat(Format format);

private:
    static bool initialized_;
    static bool caps_[7][4]; // [Format][Usage bit index]
};

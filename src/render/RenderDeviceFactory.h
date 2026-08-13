#pragma once

#include <memory>
#include <string>

class IRenderDevice;

/// Identifies the available rendering backends.
enum class RenderBackendType
{
    OpenGL,   ///< Desktop OpenGL.
    OpenGLES, ///< OpenGL ES.
    Vulkan,  ///< Optional Vulkan backend.
    Metal    ///< Metal on Apple platforms.
};

/// Returns a human-readable name for the backend type.
inline const char *renderBackendTypeName(RenderBackendType type)
{
    switch (type) {
    case RenderBackendType::OpenGL:   return "OpenGL";
    case RenderBackendType::OpenGLES: return "OpenGLES";
    case RenderBackendType::Vulkan:   return "Vulkan";
    case RenderBackendType::Metal:    return "Metal";
    }
    return "Unknown";
}

/// Factory for creating render device backends.
/// Creates render device backends. OpenGL and OpenGLES share the current
/// GL-family render device implementation; Vulkan is optional and Metal is
/// still reserved for a future implementation.
class RenderDeviceFactory
{
public:
    /// Create the best available render device for the current platform.
    /// Returns OpenGLES for OpenGLES builds, otherwise desktop OpenGL.
    static std::unique_ptr<IRenderDevice> createBestAvailable();

    /// Create a specific render device by type.
    /// Returns nullptr if the requested backend is not available.
    static std::unique_ptr<IRenderDevice> create(RenderBackendType type);

    /// Get the type of the currently active backend, if any.
    static RenderBackendType activeBackendType();

    /// Check if a specific backend type is supported on this platform.
    static bool isBackendSupported(RenderBackendType type);

private:
    RenderDeviceFactory() = delete;
};

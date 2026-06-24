#pragma once

#include <memory>
#include <string>

class IRenderDevice;

/// Identifies the available rendering backends.
enum class RenderBackendType
{
    OpenGL,  ///< Desktop/ES OpenGL (currently the only fully implemented backend).
    Vulkan,  ///< Vulkan (future)
    Metal    ///< Metal on Apple platforms (future)
};

/// Returns a human-readable name for the backend type.
inline const char *renderBackendTypeName(RenderBackendType type)
{
    switch (type) {
    case RenderBackendType::OpenGL: return "OpenGL";
    case RenderBackendType::Vulkan: return "Vulkan";
    case RenderBackendType::Metal:  return "Metal";
    }
    return "Unknown";
}

/// Factory for creating render device backends.
/// Creates render device backends. Non-OpenGL backend enum values are reserved
/// for future implementations and are never selected implicitly.
class RenderDeviceFactory
{
public:
    /// Create the best available render device for the current platform.
    /// Currently returns OpenGL, the only implemented backend.
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

#include "RenderDeviceFactory.h"

#include "OpenGLRenderDevice.h"

#include <iostream>

// Track the currently active backend type.
namespace {
RenderBackendType g_activeBackend = RenderBackendType::OpenGL;
}

std::unique_ptr<IRenderDevice> RenderDeviceFactory::createBestAvailable()
{
    // Try backends in platform-appropriate priority order.
#if defined(__APPLE__)
    // Apple: Metal > Vulkan > OpenGL
    constexpr RenderBackendType order[] = {
        RenderBackendType::Metal,
        RenderBackendType::Vulkan,
        RenderBackendType::OpenGL
    };
#else
    // Windows/Linux: Vulkan > OpenGL
    constexpr RenderBackendType order[] = {
        RenderBackendType::Vulkan,
        RenderBackendType::OpenGL
    };
#endif

    for (auto type : order) {
        auto device = create(type);
        if (device) {
            g_activeBackend = type;
            std::cout << "[RenderDeviceFactory] Using backend: "
                      << renderBackendTypeName(type) << std::endl;
            return device;
        }
    }

    std::cerr << "[RenderDeviceFactory] No render backend available!" << std::endl;
    return nullptr;
}

std::unique_ptr<IRenderDevice> RenderDeviceFactory::create(RenderBackendType type)
{
    switch (type) {
    case RenderBackendType::OpenGL:
        return std::make_unique<OpenGLRenderDevice>();

    case RenderBackendType::Vulkan:
        // TODO: Implement VulkanRenderDevice
        std::cout << "[RenderDeviceFactory] Vulkan backend not yet implemented." << std::endl;
        return nullptr;

    case RenderBackendType::Metal:
        // TODO: Implement MetalRenderDevice
        std::cout << "[RenderDeviceFactory] Metal backend not yet implemented." << std::endl;
        return nullptr;
    }

    return nullptr;
}

RenderBackendType RenderDeviceFactory::activeBackendType()
{
    return g_activeBackend;
}

bool RenderDeviceFactory::isBackendSupported(RenderBackendType type)
{
    switch (type) {
    case RenderBackendType::OpenGL:
        return true;  // Always supported (GLAD loader handles the rest)

    case RenderBackendType::Vulkan:
        // TODO: Check for Vulkan availability at runtime
        return false;

    case RenderBackendType::Metal:
#if defined(__APPLE__)
        // TODO: Check for Metal availability
        return false;
#else
        return false;
#endif
    }

    return false;
}

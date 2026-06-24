#include "RenderDeviceFactory.h"

#include "OpenGLRenderDevice.h"

#include <iostream>

// Track the currently active backend type.
namespace {
RenderBackendType g_activeBackend = RenderBackendType::OpenGL;
}

std::unique_ptr<IRenderDevice> RenderDeviceFactory::createBestAvailable()
{
    auto device = create(RenderBackendType::OpenGL);
    if (device) {
        g_activeBackend = RenderBackendType::OpenGL;
        return device;
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
        return nullptr;

    case RenderBackendType::Metal:
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
        return false;

    case RenderBackendType::Metal:
        return false;
    }

    return false;
}

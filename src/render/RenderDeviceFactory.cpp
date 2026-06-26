#include "RenderDeviceFactory.h"

#include "OpenGLRenderDevice.h"

#include <iostream>

// Track the currently active backend type.
namespace {
RenderBackendType defaultBackendType()
{
#if defined(WHATSCANVAS_OPENGL_ES)
    return RenderBackendType::OpenGLES;
#else
    return RenderBackendType::OpenGL;
#endif
}

RenderBackendType g_activeBackend = defaultBackendType();
}

std::unique_ptr<IRenderDevice> RenderDeviceFactory::createBestAvailable()
{
    const RenderBackendType backend = defaultBackendType();
    auto device = create(backend);
    if (device) {
        g_activeBackend = backend;
        return device;
    }

    std::cerr << "[RenderDeviceFactory] No render backend available!" << std::endl;
    return nullptr;
}

std::unique_ptr<IRenderDevice> RenderDeviceFactory::create(RenderBackendType type)
{
    switch (type) {
    case RenderBackendType::OpenGL:
    case RenderBackendType::OpenGLES:
        g_activeBackend = type;
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
    case RenderBackendType::OpenGLES:
        return true;  // Supported by the GL-family device when the target is built for it.

    case RenderBackendType::Vulkan:
        return false;

    case RenderBackendType::Metal:
        return false;
    }

    return false;
}

// Non-Apple inert stub for the Metal render backend.
//
// On Apple hosts CMake selects the sibling `.mm` translation unit instead. This
// file only exists so the render device factory can reference MetalRenderDevice
// unconditionally, and the class links on Linux/Windows without any Metal
// framework dependency.

#include "MetalRenderDevice.h"

#include <string>
#include <vector>

struct MetalRenderDevice::MetalContext
{
};

bool MetalRenderDevice::isAvailable() { return false; }

MetalRenderDevice::MetalRenderDevice()
    : context_(nullptr)
{
}

MetalRenderDevice::~MetalRenderDevice() = default;

void MetalRenderDevice::initializeBackend() {}
void MetalRenderDevice::finalizeBackend() {}
bool MetalRenderDevice::readPixelsRGBA(int, int, std::vector<unsigned char> &) const { return false; }
std::unique_ptr<IRenderTarget> MetalRenderDevice::createRenderTarget(int, int) const { return nullptr; }
SharedClipMaskResource MetalRenderDevice::createClipMaskResource(const ClipMaskPath &) const { return {}; }
SharedImageResource MetalRenderDevice::createImageResourceRGBA(int, int,
                                                               const std::vector<unsigned char> &) const { return {}; }
SharedImageResource MetalRenderDevice::createImageResourceAlpha8(int, int,
                                                                 const std::vector<unsigned char> &) const { return {}; }
SharedImageResource MetalRenderDevice::createImageResourceFromImageData(int, int, int, const unsigned char *,
                                                                        bool) const { return {}; }
bool MetalRenderDevice::updateImageResourceRGBA(const SharedImageResource &, int, int, int, int,
                                                const unsigned char *, bool) const { return false; }
bool MetalRenderDevice::updateImageResourceAlpha8(const SharedImageResource &, int, int, int, int,
                                                  const unsigned char *) const { return false; }
SharedImageResource MetalRenderDevice::wrapExternalImageResource(ImageResourceHandle) const { return {}; }
ImageResourceHandle MetalRenderDevice::nativeImageHandle(const SharedImageResource &) const { return {}; }
RenderResourceStats MetalRenderDevice::resourceStats() const { return {}; }
SharedImageResource MetalRenderDevice::renderCommandsToImageResource(
    const std::vector<std::unique_ptr<Command>> &, const OffscreenRenderRequest &) const { return {}; }
SharedImageResource MetalRenderDevice::filterImageResource(
    const SharedImageResource &, int, int, const wsc::ImageFilter &, FilterExecutionStats *) const { return {}; }
bool MetalRenderDevice::executeDrawList(const std::unique_ptr<IRenderTarget> &,
                                        const wsc::DrawList &) const { return false; }
bool MetalRenderDevice::executeCommands(const std::unique_ptr<IRenderTarget> &,
                                        const std::vector<std::unique_ptr<Command>> &,
                                        const OffscreenRenderRequest &) const { return false; }
bool MetalRenderDevice::isDeviceReady() const { return false; }
const std::string &MetalRenderDevice::selectedDeviceName() const
{
    static const std::string kEmpty;
    return kEmpty;
}

std::uintptr_t MetalRenderDevice::nativeHandle(int) const { return 0; }

bool MetalRenderDevice::supportsPresentation() const { return false; }
std::unique_ptr<ISwapchain> MetalRenderDevice::createSwapchain(const NativeSurface &, const SwapchainConfig &)
{
    return nullptr;
}

bool MetalRenderDevice::beginGpuFrameTiming() { return false; }
void MetalRenderDevice::endGpuFrameTiming() {}
void MetalRenderDevice::setGpuFrameTimingEnabled(bool) {}
bool MetalRenderDevice::lastGpuFrameTimeNs(std::uint64_t &nanoseconds) const
{
    nanoseconds = 0;
    return false;
}

SharedImageResource MetalRenderDevice::rasterizeClipMask(const ClipMaskState &, int, int) const
{
    return {};
}

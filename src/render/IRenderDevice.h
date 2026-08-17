#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "RenderTypes.h"
#include "Surface.h"
#include "wsc/ImageFilter.h"

class Command;
class IRenderTarget;
struct OffscreenRenderRequest;
struct ClipMaskPath;

class IRenderDevice
{
public:
    virtual ~IRenderDevice() = default;

    virtual void initializeBackend() = 0;
    virtual void finalizeBackend() = 0;
    virtual bool readPixelsRGBA(int width, int height, std::vector<unsigned char> &pixels) const = 0;
    virtual std::unique_ptr<IRenderTarget> createRenderTarget(int width, int height) const = 0;
    virtual SharedClipMaskResource createClipMaskResource(const ClipMaskPath &maskPath) const = 0;
    virtual SharedImageResource createImageResourceRGBA(int width, int height,
                                                        const std::vector<unsigned char> &pixels) const = 0;
    virtual SharedImageResource createImageResourceAlpha8(
        int /*width*/, int /*height*/,
        const std::vector<unsigned char> & /*pixels*/) const
    {
        return {};
    }
    virtual SharedImageResource createImageResourceFromImageData(int width, int height, int channels,
                                                                 const unsigned char *pixels,
                                                                 bool generateMipmaps) const = 0;
    virtual bool updateImageResourceRGBA(const SharedImageResource &imageResource, int x, int y, int width, int height,
                                         const unsigned char *pixels, bool regenerateMipmaps) const = 0;
    virtual bool updateImageResourceAlpha8(
        const SharedImageResource & /*imageResource*/,
        int /*x*/, int /*y*/, int /*width*/, int /*height*/,
        const unsigned char * /*pixels*/) const
    {
        return false;
    }
    virtual SharedImageResource wrapExternalImageResource(ImageResourceHandle handle) const = 0;
    virtual RenderResourceStats resourceStats() const = 0;
    virtual SharedImageResource renderCommandsToImageResource(const std::vector<std::unique_ptr<Command>> &commands,
                                                              const OffscreenRenderRequest &request) const = 0;
    virtual SharedImageResource filterImageResource(const SharedImageResource &source,
                                                    int width, int height,
                                                    const wsc::ImageFilter &filter,
                                                    FilterExecutionStats *executionStats = nullptr) const
    {
        (void)source;
        (void)width;
        (void)height;
        (void)filter;
        if (executionStats != nullptr) {
            *executionStats = {};
        }
        return {};
    }

    /// Whether this device drives its main-target frame by rendering a recorded
    /// command stream through executeCommands() (e.g. Vulkan) rather than the
    /// OpenGL-style per-command execute() path. When true, Renderer flushes the
    /// frame via executeCommands() into a device render target and reads it back
    /// with readPixelsRGBA().
    virtual bool usesDeviceCommandExecution() const { return false; }

    /// Render a command stream into the given render target, leaving it ready
    /// for readPixelsRGBA(). Only meaningful when usesDeviceCommandExecution()
    /// is true; the default returns false (unsupported).
    virtual bool executeCommands(const std::unique_ptr<IRenderTarget> & /*target*/,
                                 const std::vector<std::unique_ptr<Command>> & /*commands*/,
                                 const OffscreenRenderRequest & /*request*/) const { return false; }
    virtual std::size_t lastExecutionDrawCallCount() const { return 0; }
    virtual std::size_t lastExecutionMergedBatchCount() const { return 0; }
    virtual std::size_t lastCompiledPacketCount() const { return 0; }
    virtual std::size_t lastCompiledVertexBytes() const { return 0; }
    virtual std::size_t lastCompiledIndexBytes() const { return 0; }
    virtual std::uint64_t lastFrameCompileCpuTimeNs() const { return 0; }

    /// Begin/end a non-blocking backend GPU timer around one frame. Results may
    /// arrive one or more frames later; unsupported backends return false.
    virtual bool beginGpuFrameTiming() { return false; }
    virtual void endGpuFrameTiming() {}
    virtual void setGpuFrameTimingEnabled(bool /*enabled*/) {}
    virtual bool lastGpuFrameTimeNs(std::uint64_t &nanoseconds) const
    {
        nanoseconds = 0;
        return false;
    }

    /// Whether this backend can present to an on-screen window (build a
    /// swapchain from a NativeSurface). Default false: the device is
    /// offscreen-only. See doc/windowed-presentation-design.md.
    virtual bool supportsPresentation() const { return false; }

    /// Create an on-screen presentation target for the given OS window. Returns
    /// nullptr when presentation is unsupported (the default) or setup failed.
    /// OpenGL and the Win32 Vulkan path provide concrete adapters; other
    /// platform/backend combinations may still return nullptr.
    virtual std::unique_ptr<ISwapchain> createSwapchain(const NativeSurface & /*surface*/,
                                                        const SwapchainConfig & /*config*/) { return nullptr; }

    /// Draw subsequent frames into a host-owned backend render target.
    /// Returns false when external-target wrapping is unsupported (the default).
    virtual bool wrapBackendRenderTarget(const BackendRenderTarget & /*target*/) { return false; }

    /// Raw native handle accessor for advanced interop. Meaning of `which` is
    /// backend-specific; returns 0 by default.
    virtual std::uintptr_t nativeHandle(int /*which*/) const { return 0; }
};

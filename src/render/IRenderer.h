#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "render/FrameStats.h"
#include "render/RenderTypes.h"
#include "render/Surface.h"
#include "wsc/ImageFilter.h"

class Command;

struct OffscreenRenderRequest
{
    int canvasWidth = 0;
    int canvasHeight = 0;
    int targetWidth = 0;
    int targetHeight = 0;
    int viewportX = 0;
    int viewportY = 0;
    int scissorOffsetX = 0;
    int scissorOffsetY = 0;
};

class IRenderer
{
public:
    virtual ~IRenderer() = default;

    virtual void initializeBackend() = 0;
    virtual void finalizeBackend() = 0;
    virtual void setViewport(int width, int height) = 0;
    virtual void submit(std::unique_ptr<Command> &&command) = 0;
    virtual size_t commandCount() const = 0;
    virtual std::vector<std::unique_ptr<Command>> takeCommandsFrom(size_t index) = 0;
    virtual void appendCommands(std::vector<std::unique_ptr<Command>> &&commands) = 0;
    virtual bool readPixelsRGBA(std::vector<unsigned char> &pixels) const = 0;
    virtual SharedClipMaskResource createClipMaskResource(const ClipMaskPath &maskPath) const = 0;
    virtual SharedImageResource createImageResourceRGBA(int width, int height, const std::vector<unsigned char> &pixels) const = 0;
    virtual SharedImageResource createImageResourceFromImageData(int width, int height, int channels,
                                                                 const unsigned char *pixels, bool generateMipmaps) const = 0;
    virtual bool updateImageResourceRGBA(const SharedImageResource &imageResource, int x, int y, int width, int height,
                                         const unsigned char *pixels, bool regenerateMipmaps) const = 0;
    virtual SharedImageResource wrapExternalImageResource(ImageResourceHandle handle) const = 0;
    virtual const FrameStats &frameStats() const = 0;
    virtual void resetFrameStats() = 0;
    virtual RenderResourceStats resourceStats() const = 0;
    virtual SharedImageResource renderCommandsToImageResource(const std::vector<std::unique_ptr<Command>> &commands,
                                                              const OffscreenRenderRequest &request) const = 0;
    /// Render a prefix of the currently queued frame without consuming it.
    /// Used by backdrop filters to snapshot content recorded before a layer.
    virtual SharedImageResource renderQueuedCommandsToImageResource(
        size_t commandEnd, const OffscreenRenderRequest &request) const
    {
        (void)commandEnd;
        (void)request;
        return {};
    }
    /// Apply a backend-native filter and return a new image resource.
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
    virtual void resetRenderState() = 0;
    virtual void clear() = 0;
    virtual void flush() = 0;

    /// Whether this renderer's backend can present to an on-screen window.
    /// Default false (offscreen-only). See doc/windowed-presentation-design.md.
    virtual bool supportsPresentation() const { return false; }

    /// Create an on-screen presentation target for the given OS window, or
    /// nullptr when unsupported / setup failed. OpenGL, Software, and the
    /// Win32 Vulkan path provide concrete adapters.
    virtual std::unique_ptr<ISwapchain> createSwapchain(const NativeSurface & /*surface*/,
                                                        const SwapchainConfig & /*config*/) { return nullptr; }

    /// Draw subsequent frames into a host-owned backend render target
    /// (Skia-style wrap-external). Returns false when unsupported; OpenGL and
    /// Vulkan support their corresponding external target kinds.
    virtual bool wrapBackendRenderTarget(const BackendRenderTarget & /*target*/) { return false; }

    /// Raw native handle accessor for advanced interop; backend-specific.
    virtual std::uintptr_t nativeHandle(int /*which*/) const { return 0; }
};

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "render/FrameStats.h"
#include "render/RenderTypes.h"
#include "render/Surface.h"
#include "wsc/ImageFilter.h"

class Command;
struct DrawImageBatchData;
struct DrawImageBatchQuad;

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
    // Safe only when the result is composited directly, without async filters.
    bool allowDirectTargetSampling = false;
};

class IRenderer
{
public:
    virtual ~IRenderer() = default;

    virtual void initializeBackend() = 0;
    virtual void finalizeBackend() = 0;
    /// Drop backend state after involuntary context/device loss. Implementations
    /// must not issue destruction calls against the lost backend context.
    virtual void abandonBackend() { finalizeBackend(); }
    virtual void setViewport(int width, int height) = 0;
    virtual void submit(std::unique_ptr<Command> &&command) = 0;
    virtual void recordCommandClone(
        std::size_t /*payloadBytes*/,
        bool /*pathCommand*/) {}
    virtual bool tryAppendImageBatch(
        DrawImageBatchData & /*batch*/)
    {
        return false;
    }
    // Returns the existing compatible batch's quad storage after reserving
    // room for an immediate append. This lets hot producers write directly
    // into renderer-owned staging instead of building and copying a temporary
    // vector. The pointer must not be retained across another renderer call.
    virtual std::vector<DrawImageBatchQuad> *tryGetImageBatchAppendTarget(
        const DrawImageBatchData & /*batch*/,
        std::size_t /*additionalQuadCount*/)
    {
        return nullptr;
    }
    virtual size_t commandCount() const = 0;
    virtual std::vector<std::unique_ptr<Command>> takeCommandsFrom(size_t index) = 0;
    /// Read-only peek used by cache prototypes (currently: pre-layer command
    /// fingerprinting for the backdrop cache PoC). Returns nullptr for indices
    /// that are out of range or for renderer implementations that do not
    /// expose a command queue.
    virtual const Command *commandAt(size_t /*index*/) const { return nullptr; }
    virtual void appendCommands(std::vector<std::unique_ptr<Command>> &&commands) = 0;
    virtual bool readPixelsRGBA(std::vector<unsigned char> &pixels) const = 0;
    virtual SharedClipMaskResource createClipMaskResource(const ClipMaskPath &maskPath) const = 0;
    virtual SharedImageResource createImageResourceRGBA(int width, int height, const std::vector<unsigned char> &pixels) const = 0;
    virtual SharedImageResource createImageResourceAlpha8(
        int /*width*/, int /*height*/,
        const std::vector<unsigned char> & /*pixels*/) const
    {
        return {};
    }
    virtual SharedImageResource createImageResourceFromImageData(int width, int height, int channels,
                                                                 const unsigned char *pixels, bool generateMipmaps) const = 0;
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
    /// Apply an ordered filter pipeline. The default implementation preserves
    /// node order and lowers generic nodes through backend image rendering.
    virtual SharedImageResource filterImageResource(
        const SharedImageResource &source,
        int width, int height,
        const wsc::ImageFilterChain &filters,
        FilterExecutionStats *executionStats = nullptr) const;
    virtual void resetRenderState() = 0;
    virtual void clear() = 0;
    virtual void flush() = 0;
    virtual void setGpuTimingEnabled(bool /*enabled*/) {}

    /// Whether this renderer's backend can present to an on-screen window.
    /// Default false (offscreen-only). See doc/internal/architecture/windowed-presentation.md.
    virtual bool supportsPresentation() const { return false; }

    /// Create an on-screen presentation target for the given OS window, or
    /// nullptr when unsupported / setup failed. OpenGL, Software, and the
    /// Win32 Vulkan path provide concrete adapters.
    virtual std::unique_ptr<ISwapchain> createSwapchain(const NativeSurface & /*surface*/,
                                                        const SwapchainConfig & /*config*/) { return nullptr; }

    /// Draw subsequent frames into a host-owned backend render target. Returns
    /// false when unsupported; OpenGL and Vulkan support their corresponding
    /// external target kinds.
    virtual bool wrapBackendRenderTarget(const BackendRenderTarget & /*target*/) { return false; }

    /// Raw native handle accessor for advanced interop; backend-specific.
    virtual std::uintptr_t nativeHandle(int /*which*/) const { return 0; }

protected:
    virtual void recordGenericFilterPass(
        int /*width*/, int /*height*/) const {}
};

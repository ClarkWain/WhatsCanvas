#pragma once

#include <memory>

#include "DrawList.h"
#include "IRenderDevice.h"
#include "RenderTargetPool.h"

struct OpenGLContextState;

class OpenGLRenderDevice : public IRenderDevice
{
public:
    OpenGLRenderDevice() = default;
    ~OpenGLRenderDevice() override;

    void initializeBackend() override;
    void finalizeBackend() override;
    void abandonBackend() override;
    bool readPixelsRGBA(int width, int height, std::vector<unsigned char> &pixels) const override;
    std::unique_ptr<IRenderTarget> createRenderTarget(int width, int height) const override;
    SharedClipMaskResource createClipMaskResource(const ClipMaskPath &maskPath) const override;
    SharedImageResource createImageResourceRGBA(int width, int height,
                                                const std::vector<unsigned char> &pixels) const override;
    SharedImageResource createImageResourceAlpha8(
        int width, int height,
        const std::vector<unsigned char> &pixels) const override;
    SharedImageResource createImageResourceFromImageData(int width, int height, int channels,
                                                         const unsigned char *pixels,
                                                         bool generateMipmaps) const override;
    bool updateImageResourceRGBA(const SharedImageResource &imageResource, int x, int y, int width, int height,
                                 const unsigned char *pixels, bool regenerateMipmaps) const override;
    bool updateImageResourceAlpha8(
        const SharedImageResource &imageResource,
        int x, int y, int width, int height,
        const unsigned char *pixels) const override;
    SharedImageResource wrapExternalImageResource(ImageResourceHandle handle) const override;
    RenderResourceStats resourceStats() const override;
    SharedImageResource renderCommandsToImageResource(const std::vector<std::unique_ptr<Command>> &commands,
                                                      const OffscreenRenderRequest &request) const override;
    SharedImageResource filterImageResource(const SharedImageResource &source,
                                            int width, int height,
                                            const wsc::ImageFilter &filter,
                                            FilterExecutionStats *executionStats = nullptr) const override;
    bool executeDrawList(const wsc::DrawList &drawList, int width, int height,
                         int scissorOffsetX = 0, int scissorOffsetY = 0) const;
    bool beginGpuFrameTiming() override;
    void endGpuFrameTiming() override;
    void setGpuFrameTimingEnabled(bool enabled) override
    {
        gpuTimingEnabled_ = enabled;
    }
    bool lastGpuFrameTimeNs(std::uint64_t &nanoseconds) const override;
    std::size_t lastCompiledPacketCount() const override
    {
        return lastCompiledPacketCount_;
    }
    std::size_t lastCompiledVertexBytes() const override
    {
        return lastCompiledVertexBytes_;
    }
    std::size_t lastCompiledIndexBytes() const override
    {
        return lastCompiledIndexBytes_;
    }
    std::uint64_t lastFrameCompileCpuTimeNs() const override
    {
        return lastFrameCompileCpuTimeNs_;
    }

    // Host-owned on-screen presentation (WGL/GLX buffer swap).
    bool supportsPresentation() const override;
    std::unique_ptr<ISwapchain> createSwapchain(const NativeSurface &surface,
                                                const SwapchainConfig &config) override;

    // Render into a host-provided GL framebuffer.
    bool wrapBackendRenderTarget(const BackendRenderTarget &target) override;

private:
    bool backendInitialized_ = false;
    bool hasWrappedFramebuffer_ = false;
    unsigned int wrappedFramebuffer_ = 0;
    unsigned int gpuTimerQueries_[3] = {};
    bool gpuTimerPending_[3] = {};
    std::uint64_t gpuTimerSequences_[3] = {};
    int activeGpuTimerQuery_ = -1;
    int nextGpuTimerQuery_ = 0;
    std::uint64_t nextGpuTimerSequence_ = 1;
    std::uint64_t lastGpuTimeNs_ = 0;
    std::uint64_t lastGpuTimeSequence_ = 0;
    bool lastGpuTimeAvailable_ = false;
    bool gpuTimingEnabled_ = false;
    mutable std::size_t lastCompiledPacketCount_ = 0;
    mutable std::size_t lastCompiledVertexBytes_ = 0;
    mutable std::size_t lastCompiledIndexBytes_ = 0;
    mutable std::uint64_t lastFrameCompileCpuTimeNs_ = 0;
    mutable std::unique_ptr<RenderTargetPool> renderTargetPool_;
    std::shared_ptr<OpenGLContextState> contextState_;
};

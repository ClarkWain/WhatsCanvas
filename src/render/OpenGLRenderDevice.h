#pragma once

#include <memory>

#include "DrawList.h"
#include "IRenderDevice.h"
#include "RenderTargetPool.h"

class OpenGLRenderDevice : public IRenderDevice
{
public:
    OpenGLRenderDevice() = default;
    ~OpenGLRenderDevice() override;

    void initializeBackend() override;
    void finalizeBackend() override;
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

    // Host-owned on-screen presentation (WGL/GLX buffer swap).
    bool supportsPresentation() const override;
    std::unique_ptr<ISwapchain> createSwapchain(const NativeSurface &surface,
                                                const SwapchainConfig &config) override;

    // Skia-style wrap-external: render into a host-provided GL framebuffer.
    bool wrapBackendRenderTarget(const BackendRenderTarget &target) override;

private:
    bool backendInitialized_ = false;
    mutable std::unique_ptr<RenderTargetPool> renderTargetPool_;
};

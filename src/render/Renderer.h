#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "IRenderDevice.h"
#include "IRenderer.h"
#include "RenderContext.h"
#include "FrameStats.h"

// Forward declaration for backend type enum.
enum class RenderBackendType;

class Renderer : public IRenderer
{
public:
    static void initialize();
    static void finalize();

public:
    Renderer();
    explicit Renderer(std::unique_ptr<IRenderDevice> device);
    ~Renderer() override;

    void initializeBackend() override;
    void finalizeBackend() override;

    void setViewport(int width, int height) override;
    void submit(std::unique_ptr<Command> &&command) override;
    size_t commandCount() const override;
    std::vector<std::unique_ptr<Command>> takeCommandsFrom(size_t index) override;
    void appendCommands(std::vector<std::unique_ptr<Command>> &&commands) override;

    bool readPixelsRGBA(std::vector<unsigned char> &pixels) const override;
    SharedClipMaskResource createClipMaskResource(const ClipMaskPath &maskPath) const override;
    SharedImageResource createImageResourceRGBA(int width, int height, const std::vector<unsigned char> &pixels) const override;
    SharedImageResource createImageResourceFromImageData(int width, int height, int channels,
                                                         const unsigned char *pixels, bool generateMipmaps) const override;
    bool updateImageResourceRGBA(const SharedImageResource &imageResource, int x, int y, int width, int height,
                                 const unsigned char *pixels, bool regenerateMipmaps) const override;
    SharedImageResource wrapExternalImageResource(ImageResourceHandle handle) const override;
    const FrameStats &frameStats() const override { return stats_; }
    void resetFrameStats() override { stats_.reset(); }
    RenderResourceStats resourceStats() const override;
    SharedImageResource renderCommandsToImageResource(const std::vector<std::unique_ptr<Command>> &commands,
                                                      const OffscreenRenderRequest &request) const override;
    SharedImageResource renderQueuedCommandsToImageResource(
        size_t commandEnd, const OffscreenRenderRequest &request) const override;
    SharedImageResource filterImageResource(const SharedImageResource &source,
                                            int width, int height,
                                            const wsc::ImageFilter &filter) const override;
    void resetRenderState() override;
    void clear() override;
    void flush() override;

    // Presentation: forwarded to the underlying render device (GL/Vulkan).
    bool supportsPresentation() const override;
    std::unique_ptr<ISwapchain> createSwapchain(const NativeSurface &surface,
                                                const SwapchainConfig &config) override;
    bool wrapBackendRenderTarget(const BackendRenderTarget &target) override;
    std::uintptr_t nativeHandle(int which) const override;

private:
    // Renders the recorded frame through the device's command-execution path
    // (used by devices such as Vulkan that render a command stream into a device
    // render target). Returns true when the device handled the flush.
    bool flushViaDeviceCommands();

    std::vector<std::unique_ptr<Command>> commands_;
    std::unique_ptr<IRenderDevice> device_;
    RenderContext context_;
    bool backendInitialized_ = false;
    mutable FrameStats stats_;

    // Main render target for devices that render command streams into a target
    // (usesDeviceCommandExecution()); unused by the OpenGL execute() path.
    std::unique_ptr<IRenderTarget> mainTarget_;
    int mainTargetWidth_ = 0;
    int mainTargetHeight_ = 0;
};

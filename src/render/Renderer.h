#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "IRenderDevice.h"
#include "IRenderer.h"
#if !defined(WHATSCANVAS_METAL_ONLY)
#include "RenderContext.h"
#endif
#include "FrameStats.h"
#include "command/DrawData.h"

// Forward declaration for backend type enum.
enum class RenderBackendType;
class SpriteBatch;

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
    void abandonBackend() override;

    void setViewport(int width, int height) override;
    void submit(std::unique_ptr<Command> &&command) override;
    void recordCommandClone(
        std::size_t payloadBytes,
        bool pathCommand) override;
    bool tryAppendImageBatch(
        DrawImageBatchData &batch) override;
    std::vector<DrawImageBatchQuad> *tryGetImageBatchAppendTarget(
        const DrawImageBatchData &batch,
        std::size_t additionalQuadCount) override;
    size_t commandCount() const override;
    std::vector<std::unique_ptr<Command>> takeCommandsFrom(size_t index) override;
    void appendCommands(std::vector<std::unique_ptr<Command>> &&commands) override;

    bool readPixelsRGBA(std::vector<unsigned char> &pixels) const override;
    SharedClipMaskResource createClipMaskResource(const ClipMaskPath &maskPath) const override;
    SharedImageResource createImageResourceRGBA(int width, int height, const std::vector<unsigned char> &pixels) const override;
    SharedImageResource createImageResourceAlpha8(
        int width, int height,
        const std::vector<unsigned char> &pixels) const override;
    SharedImageResource createImageResourceFromImageData(int width, int height, int channels,
                                                         const unsigned char *pixels, bool generateMipmaps) const override;
    bool updateImageResourceRGBA(const SharedImageResource &imageResource, int x, int y, int width, int height,
                                 const unsigned char *pixels, bool regenerateMipmaps) const override;
    bool updateImageResourceAlpha8(
        const SharedImageResource &imageResource,
        int x, int y, int width, int height,
        const unsigned char *pixels) const override;
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
                                            const wsc::ImageFilter &filter,
                                            FilterExecutionStats *executionStats = nullptr) const override;
    void resetRenderState() override;
    void clear() override;
    void flush() override;
    void setGpuTimingEnabled(bool enabled) override;

    // Presentation: forwarded to the underlying render device (GL/Vulkan).
    bool supportsPresentation() const override;
    std::unique_ptr<ISwapchain> createSwapchain(const NativeSurface &surface,
                                                const SwapchainConfig &config) override;
    bool wrapBackendRenderTarget(const BackendRenderTarget &target) override;
    std::uintptr_t nativeHandle(int which) const override;

private:
    std::size_t stagingCapacityBytes() const;
    void recordGenericFilterPass(
        int width, int height) const override;

    // Renders the recorded frame through the device's command-execution path
    // (used by devices such as Vulkan that render a command stream into a device
    // render target). Returns true when the device handled the flush.
    bool flushViaDeviceCommands();

    std::vector<std::unique_ptr<Command>> commands_;
    // Commands at or before this index belong to an already-observed recording
    // scope (for example, the parent of a saveLayer boundary).
    mutable std::size_t imageBatchAppendFloor_ = 0;
    std::unique_ptr<IRenderDevice> device_;
#if !defined(WHATSCANVAS_METAL_ONLY)
    RenderContext context_;
#endif
    int viewportWidth_ = 0;
    int viewportHeight_ = 0;
    bool backendInitialized_ = false;
    mutable FrameStats stats_;
#if !defined(WHATSCANVAS_METAL_ONLY)
    std::unique_ptr<SpriteBatch> spriteBatch_;
#endif

    struct PathBatchCache
    {
        DrawPathData packet;
        std::vector<std::uint64_t> topology;
    };

    // Keep one cache entry per merged packet. Dense frames exceed the 16-bit
    // index limit and produce multiple packets; a single slot makes those
    // packets evict each other every frame.
    std::vector<PathBatchCache> pathBatchCaches_;

    // Main render target for devices that render command streams into a target
    // (usesDeviceCommandExecution()); unused by the OpenGL execute() path.
    std::unique_ptr<IRenderTarget> mainTarget_;
    int mainTargetWidth_ = 0;
    int mainTargetHeight_ = 0;
};

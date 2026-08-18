#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "render/IRenderer.h"

namespace wsc::software {

/// A pure-CPU rasterizer backend (no GPU / no graphics context required).
///
/// It implements IRenderer directly and rasterizes the already-tessellated
/// DrawData carried by the recorded commands into an RGBA8 CPU framebuffer.
/// Because it needs no window, context or driver, it renders deterministically
/// and is fully headless-testable — useful for server-side rendering,
/// thumbnails, golden-image baselines and environments without a GPU.
///
/// Feature coverage: filled/stroked paths and geometry text (triangle lists
/// with optional per-vertex color and analytic-AA coverage), all 14 blend
/// modes, linear/radial gradients, points and lines, image sampling (tint,
/// color matrix, sampling quality and tile modes), scissor plus anti-aliased
/// path clipping, true separable-Gaussian shadows, saveLayer offscreen layers,
/// optional gamma-correct linear-space blending, and pixel readback.
class SoftwareRenderer final : public IRenderer
{
public:
    SoftwareRenderer(int width, int height);
    ~SoftwareRenderer() override = default;

    void initializeBackend() override;
    void finalizeBackend() override;
    void setViewport(int width, int height) override;
    void submit(std::unique_ptr<Command> &&command) override;
    void recordCommandClone(
        std::size_t payloadBytes,
        bool pathCommand) override;
    std::vector<DrawImageBatchQuad> *tryGetImageBatchAppendTarget(
        const DrawImageBatchData &batch,
        std::size_t additionalQuadCount) override;
    size_t commandCount() const override;
    std::vector<std::unique_ptr<Command>> takeCommandsFrom(size_t index) override;
    void appendCommands(std::vector<std::unique_ptr<Command>> &&commands) override;
    bool readPixelsRGBA(std::vector<unsigned char> &pixels) const override;
    SharedClipMaskResource createClipMaskResource(const ClipMaskPath &maskPath) const override;
    SharedImageResource createImageResourceRGBA(int width, int height,
                                                const std::vector<unsigned char> &pixels) const override;
    SharedImageResource createImageResourceFromImageData(int width, int height, int channels,
                                                         const unsigned char *pixels,
                                                         bool generateMipmaps) const override;
    bool updateImageResourceRGBA(const SharedImageResource &imageResource, int x, int y, int width, int height,
                                 const unsigned char *pixels, bool regenerateMipmaps) const override;
    SharedImageResource wrapExternalImageResource(ImageResourceHandle handle) const override;
    const FrameStats &frameStats() const override;
    void resetFrameStats() override;
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

    // On-screen presentation (Windows/GDI). Offscreen-only elsewhere.
    bool supportsPresentation() const override;
    std::unique_ptr<ISwapchain> createSwapchain(const NativeSurface &surface,
                                                const SwapchainConfig &config) override;

    int width() const { return width_; }
    int height() const { return height_; }

private:
    void recordGenericFilterPass(
        int width, int height) const override;
    void ensureFramebuffer();
    void clearFramebuffer();

    int width_ = 0;
    int height_ = 0;
    std::vector<std::uint8_t> framebuffer_; // RGBA8, row 0 = top (canvas y = 0)
    std::vector<std::unique_ptr<Command>> commands_;
    mutable std::size_t imageBatchAppendFloor_ = 0;
    mutable FrameStats stats_;
};

} // namespace wsc::software

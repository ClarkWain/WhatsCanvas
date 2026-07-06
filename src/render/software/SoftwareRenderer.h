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
/// Milestone 1 scope: filled/stroked paths and geometry text (triangle lists
/// with optional per-vertex color and analytic-AA coverage), SrcOver-family
/// blending, and pixel readback. Gradients, images, clip masks, offscreen
/// layers and blurred shadows are added in later milestones.
class SoftwareRenderer final : public IRenderer
{
public:
    SoftwareRenderer(int width, int height);
    ~SoftwareRenderer() override = default;

    void initializeBackend() override;
    void finalizeBackend() override;
    void setViewport(int width, int height) override;
    void submit(std::unique_ptr<Command> &&command) override;
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
    void resetRenderState() override;
    void clear() override;
    void flush() override;

    int width() const { return width_; }
    int height() const { return height_; }

private:
    void ensureFramebuffer();
    void clearFramebuffer();
    void executeCommand(const Command &command);

    int width_ = 0;
    int height_ = 0;
    std::vector<std::uint8_t> framebuffer_; // RGBA8, row 0 = top (canvas y = 0)
    std::vector<std::unique_ptr<Command>> commands_;
    FrameStats stats_;
};

} // namespace wsc::software

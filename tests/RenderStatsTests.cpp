#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "command/DrawCommand.h"
#include "render/IRenderDevice.h"
#include "render/IRenderTarget.h"
#include "render/Renderer.h"
#include "wsc/wsc.h"

namespace {

bool expect(bool condition, const std::string &message)
{
    if (condition) {
        return true;
    }

    std::cerr << "EXPECTATION FAILED: " << message << std::endl;
    return false;
}

class NoopCommand final : public Command
{
public:
    NoopCommand() : Command(Type::Text) {}

    void execute(RenderContext &) override {}
};

class FakeImageResource final : public ImageResource
{
public:
    bool isValid() const override { return true; }
    void bind(const RenderContext &) const override {}
    bool updateRGBA(int, int, int, int, const unsigned char *, bool) override { return true; }
};

class FakeRenderDevice final : public IRenderDevice
{
public:
    void initializeBackend() override {}
    void finalizeBackend() override {}
    bool readPixelsRGBA(int, int, std::vector<unsigned char> &pixels) const override
    {
        pixels.clear();
        return false;
    }
    std::unique_ptr<IRenderTarget> createRenderTarget(int, int) const override { return {}; }
    SharedClipMaskResource createClipMaskResource(const ClipMaskPath &) const override { return {}; }
    SharedImageResource createImageResourceRGBA(int, int, const std::vector<unsigned char> &) const override { return {}; }
    SharedImageResource createImageResourceFromImageData(int, int, int, const unsigned char *, bool) const override
    {
        return {};
    }
    bool updateImageResourceRGBA(const SharedImageResource &, int, int, int, int, const unsigned char *, bool) const override
    {
        return false;
    }
    SharedImageResource wrapExternalImageResource(ImageResourceHandle) const override { return {}; }
    RenderResourceStats resourceStats() const override { return {}; }
    SharedImageResource renderCommandsToImageResource(const std::vector<std::unique_ptr<Command>> &,
                                                      const OffscreenRenderRequest &) const override
    {
        return std::make_shared<FakeImageResource>();
    }
    SharedImageResource filterImageResource(const SharedImageResource &, int, int,
                                            const wsc::ImageFilter &,
                                            FilterExecutionStats *executionStats) const override
    {
        if (executionStats != nullptr) {
            executionStats->passCount = 3;
            executionStats->pixelPassCount = 5120;
            executionStats->downsampled = true;
        }
        return std::make_shared<FakeImageResource>();
    }
};

bool testDefaultStatsAreReadable()
{
    auto canvasOwner = wsc::Canvas::create(wsc::Canvas::Backend::OpenGL, 0, 0);
    wsc::Canvas &canvas = *canvasOwner;
    const wsc::Canvas::RenderStats stats = canvas.getRenderStats();
    return expect(stats.commandCount == 0, "default command count should be zero")
        && expect(stats.drawCallCount == 0, "default draw call count should be zero")
        && expect(stats.mergedBatchCount == 0, "default merged batch count should be zero")
        && expect(stats.renderTargetSwitches == 0, "default render target switch count should be zero")
        && expect(stats.filterCount == 0, "default filter count should be zero")
        && expect(stats.filterPassCount == 0, "default filter pass count should be zero")
        && expect(stats.downsampledFilterCount == 0,
                  "default downsampled filter count should be zero")
        && expect(stats.filterInputPixelCount == 0,
                  "default filter input pixel count should be zero")
        && expect(stats.filterPixelPassCount == 0,
                  "default filter pixel-pass count should be zero")
        && expect(stats.imageTextureCount == 0, "default image texture count should be zero")
        && expect(stats.glyphAtlasTextureCount == 0, "default glyph atlas count should be zero")
        && expect(stats.renderTargetCount == 0, "default render target count should be zero");
}

bool testOffscreenStatsCountCommandsAndDraws()
{
    Renderer renderer(std::make_unique<FakeRenderDevice>());

    std::vector<std::unique_ptr<Command>> commands;
    commands.push_back(std::make_unique<NoopCommand>());
    commands.push_back(std::make_unique<NoopCommand>());

    OffscreenRenderRequest request;
    request.canvasWidth = 64;
    request.canvasHeight = 64;
    request.targetWidth = 32;
    request.targetHeight = 32;

    const SharedImageResource image = renderer.renderCommandsToImageResource(commands, request);
    const FrameStats &stats = renderer.frameStats();
    return expect(image && image->isValid(), "fake offscreen render should return a valid image")
        && expect(stats.commandCount == 2, "offscreen command count should include rendered commands")
        && expect(stats.drawCallCount == 2, "offscreen draw call count should include rendered commands")
        && expect(stats.mergedBatchCount == 0, "offscreen render should not report renderer-side merges")
        && expect(stats.renderTargetSwitches == 1, "offscreen render should count render target switch");
}

bool testFilterStatsAccumulateAndReset()
{
    Renderer renderer(std::make_unique<FakeRenderDevice>());
    const auto source = std::make_shared<FakeImageResource>();
    const SharedImageResource filtered = renderer.filterImageResource(
        source, 64, 40, wsc::ImageFilter::blur(32.0f));
    const FrameStats &stats = renderer.frameStats();
    bool ok = expect(filtered && filtered->isValid(),
                     "fake filter should return a valid image")
        && expect(stats.filterCount == 1, "successful filter should increment filter count")
        && expect(stats.filterPassCount == 3, "filter passes should be accumulated")
        && expect(stats.downsampledFilterCount == 1,
                  "downsampled filters should be counted")
        && expect(stats.filterInputPixelCount == 2560,
                  "filter input pixels should use the submitted dimensions")
        && expect(stats.filterPixelPassCount == 5120,
                  "backend pixel-pass work should be accumulated");
    renderer.resetFrameStats();
    ok = expect(renderer.frameStats().filterCount == 0
                    && renderer.frameStats().filterPixelPassCount == 0,
                "resetFrameStats should clear filter diagnostics") && ok;
    return ok;
}

} // namespace

int main()
{
    bool ok = true;
    ok = testDefaultStatsAreReadable() && ok;
    ok = testOffscreenStatsCountCommandsAndDraws() && ok;
    ok = testFilterStatsAccumulateAndReset() && ok;
    return ok ? 0 : 1;
}

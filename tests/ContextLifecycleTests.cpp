#include "wsc/wsc.h"

#include "command/DrawCommand.h"
#include "render/IRenderer.h"

#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

bool expect(bool condition, const std::string &message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << std::endl;
        return false;
    }
    return true;
}

bool near(float lhs, float rhs, float epsilon = 0.0001f)
{
    return std::abs(lhs - rhs) <= epsilon;
}

class FakeImageResource final : public ImageResource
{
public:
    bool isValid() const override { return true; }
    void bind(const RenderContext &) const override {}
    bool updateRGBA(int, int, int, int, const unsigned char *, bool) override { return true; }
};

class FakeRenderer final : public IRenderer
{
public:
    void initializeBackend() override
    {
        ++initializeCount;
        initialized = true;
    }

    void finalizeBackend() override
    {
        ++finalizeCount;
        initialized = false;
    }

    void setViewport(int width, int height) override
    {
        ++viewportCount;
        viewportWidth = width;
        viewportHeight = height;
    }

    void submit(std::unique_ptr<Command> &&command) override
    {
        commands.push_back(std::move(command));
    }

    size_t commandCount() const override { return commands.size(); }

    std::vector<std::unique_ptr<Command>> takeCommandsFrom(size_t index) override
    {
        std::vector<std::unique_ptr<Command>> taken;
        if (index >= commands.size()) {
            return taken;
        }
        taken.reserve(commands.size() - index);
        for (size_t i = index; i < commands.size(); ++i) {
            taken.push_back(std::move(commands[i]));
        }
        commands.erase(commands.begin() + static_cast<std::ptrdiff_t>(index), commands.end());
        return taken;
    }

    void appendCommands(std::vector<std::unique_ptr<Command>> &&appended) override
    {
        for (auto &command : appended) {
            commands.push_back(std::move(command));
        }
    }

    bool readPixelsRGBA(std::vector<unsigned char> &) const override { return false; }
    SharedClipMaskResource createClipMaskResource(const ClipMaskPath &) const override { return {}; }
    SharedImageResource createImageResourceRGBA(int, int, const std::vector<unsigned char> &) const override
    {
        return {};
    }
    SharedImageResource createImageResourceFromImageData(int, int, int, const unsigned char *, bool) const override
    {
        return {};
    }
    bool updateImageResourceRGBA(const SharedImageResource &, int, int, int, int, const unsigned char *, bool) const override
    {
        return false;
    }
    SharedImageResource wrapExternalImageResource(ImageResourceHandle) const override { return {}; }
    const FrameStats &frameStats() const override { return stats; }
    void resetFrameStats() override { stats.reset(); }
    RenderResourceStats resourceStats() const override { return {}; }
    SharedImageResource renderCommandsToImageResource(const std::vector<std::unique_ptr<Command>> &,
                                                      const OffscreenRenderRequest &) const override
    {
        ++renderTargetRequests;
        stats.renderTargetSwitches += 1;
        return returnRenderTargetImage ? std::make_shared<FakeImageResource>() : SharedImageResource();
    }
    void resetRenderState() override {}

    void clear() override
    {
        ++clearCount;
        commands.clear();
    }

    void flush() override
    {
        stats.commandCount += commands.size();
        stats.drawCallCount += commands.size();
        commands.clear();
    }

    bool initialized = false;
    int initializeCount = 0;
    int finalizeCount = 0;
    int viewportCount = 0;
    int viewportWidth = 0;
    int viewportHeight = 0;
    int clearCount = 0;
    mutable int renderTargetRequests = 0;
    bool returnRenderTargetImage = false;
    std::vector<std::unique_ptr<Command>> commands;
    mutable FrameStats stats;
};

} // namespace

namespace wsc {

class CanvasLifecycleTestAccess
{
public:
    static std::unique_ptr<Canvas> create(std::unique_ptr<IRenderer> renderer)
    {
        return std::unique_ptr<Canvas>(new Canvas(std::move(renderer)));
    }
};

} // namespace wsc

namespace {

bool testExplicitInitializeAndFinalize()
{
    auto renderer = std::make_unique<FakeRenderer>();
    FakeRenderer *rawRenderer = renderer.get();
    std::unique_ptr<wsc::Canvas> canvas = wsc::CanvasLifecycleTestAccess::create(std::move(renderer));

    bool ok = expect(!canvas->isContextInitialized(), "new canvas context should be uninitialized");
    canvas->setSize(320, 180);
    ok = expect(rawRenderer->viewportCount == 0, "setSize before initialization should not touch renderer viewport") && ok;

    ok = expect(canvas->initializeContext(), "initializeContext should succeed with a renderer") && ok;
    ok = expect(canvas->isContextInitialized(), "initializeContext should mark context initialized") && ok;
    ok = expect(rawRenderer->initialized, "renderer should be initialized") && ok;
    ok = expect(rawRenderer->initializeCount == 1, "renderer should initialize once") && ok;
    ok = expect(rawRenderer->viewportCount == 1 && rawRenderer->viewportWidth == 320
                && rawRenderer->viewportHeight == 180, "initializeContext should apply existing size") && ok;

    ok = expect(canvas->initializeContext(), "initializeContext should be idempotent") && ok;
    ok = expect(rawRenderer->initializeCount == 1, "second initializeContext should not reinitialize renderer") && ok;

    canvas->setSize(640, 360);
    ok = expect(rawRenderer->viewportCount == 2 && rawRenderer->viewportWidth == 640
                && rawRenderer->viewportHeight == 360, "setSize after initialization should update viewport") && ok;

    canvas->finalizeContext();
    ok = expect(!canvas->isContextInitialized(), "finalizeContext should clear initialized flag") && ok;
    ok = expect(!rawRenderer->initialized, "renderer should be finalized") && ok;
    ok = expect(rawRenderer->finalizeCount == 1, "renderer should finalize once") && ok;

    canvas->finalizeContext();
    ok = expect(rawRenderer->finalizeCount == 1, "second finalizeContext should be idempotent") && ok;

    return ok;
}

bool testReleaseResourcesClearsQueuedWork()
{
    auto renderer = std::make_unique<FakeRenderer>();
    FakeRenderer *rawRenderer = renderer.get();
    std::unique_ptr<wsc::Canvas> canvas = wsc::CanvasLifecycleTestAccess::create(std::move(renderer));

    bool ok = expect(canvas->initializeContext(), "initializeContext should succeed");
    canvas->setSize(200, 100);

    wsc::Paint paint;
    paint.setColor(wsc::Color::RED);
    canvas->drawRect(wsc::RectF(0.0f, 0.0f, 50.0f, 40.0f), paint);
    ok = expect(rawRenderer->commandCount() > 0, "drawRect should queue renderer work") && ok;

    canvas->flush();
    ok = expect(canvas->getRenderStats().commandCount > 0, "flush should update command stats") && ok;

    canvas->drawRect(wsc::RectF(10.0f, 10.0f, 20.0f, 20.0f), paint);
    ok = expect(rawRenderer->commandCount() > 0, "second drawRect should queue renderer work") && ok;
    canvas->releaseResources();
    ok = expect(canvas->isContextInitialized(), "releaseResources should keep context initialized") && ok;
    ok = expect(rawRenderer->commandCount() == 0, "releaseResources should clear queued commands") && ok;
    ok = expect(canvas->getRenderStats().commandCount == 0, "releaseResources should reset frame stats") && ok;
    ok = expect(rawRenderer->clearCount >= 1, "releaseResources should call renderer clear") && ok;

    return ok;
}

bool testCanvasGraphicsStateColorAndBlendAffectPathCommands()
{
    auto renderer = std::make_unique<FakeRenderer>();
    FakeRenderer *rawRenderer = renderer.get();
    std::unique_ptr<wsc::Canvas> canvas = wsc::CanvasLifecycleTestAccess::create(std::move(renderer));

    bool ok = expect(canvas->initializeContext(), "initializeContext should succeed");
    canvas->setSize(200, 100);

    canvas->setColor(wsc::Color(128, 255, 255, 128));
    canvas->setBlendMode(wsc::Paint::BlendMode::ADD);
    const int saved = canvas->save();

    canvas->setColor(wsc::Color(255, 128, 255, 64));
    canvas->setBlendMode(wsc::Paint::BlendMode::MULTIPLY);

    wsc::Paint paint;
    paint.setColor(wsc::Color(100, 200, 50, 255));
    canvas->drawRect(wsc::RectF(0.0f, 0.0f, 40.0f, 30.0f), paint);

    ok = expect(rawRenderer->commandCount() == 1, "stateful drawRect should queue one path command") && ok;
    const auto *command = dynamic_cast<DrawPathCommand *>(rawRenderer->commands.front().get());
    ok = expect(command != nullptr, "stateful drawRect should queue DrawPathCommand") && ok;
    if (command != nullptr) {
        const DrawPathData &data = command->data();
        ok = expect(near(data.color[0], 100.0f / 255.0f), "state color should preserve red channel") && ok;
        ok = expect(near(data.color[1], 100.0f / 255.0f), "state color should tint green channel") && ok;
        ok = expect(near(data.color[2], 50.0f / 255.0f), "state color should preserve blue channel") && ok;
        ok = expect(near(data.color[3], 64.0f / 255.0f), "state color should multiply alpha") && ok;
        ok = expect(data.blendMode == DrawBlendMode::Multiply, "state blend mode should affect path command") && ok;
    }

    canvas->restoreToCount(saved);
    ok = expect(canvas->getBlendMode() == wsc::Paint::BlendMode::ADD, "restore should recover blend mode") && ok;
    ok = expect(canvas->getColor().getR() == 128, "restore should recover color") && ok;

    return ok;
}

bool testResizeInvalidatesRenderTargetTexture()
{
    auto renderer = std::make_unique<FakeRenderer>();
    FakeRenderer *rawRenderer = renderer.get();
    rawRenderer->returnRenderTargetImage = true;
    std::unique_ptr<wsc::Canvas> canvas = wsc::CanvasLifecycleTestAccess::create(std::move(renderer));

    bool ok = expect(canvas->initializeContext(), "initializeContext should succeed");
    canvas->setSize(128, 64);
    canvas->setRenderTargetMode(true);

    wsc::Paint paint;
    paint.setColor(wsc::Color::WHITE);
    canvas->drawRect(wsc::RectF(0.0f, 0.0f, 32.0f, 24.0f), paint);
    canvas->flush();

    ok = expect(canvas->isTextureValid(), "render target should be valid after offscreen flush") && ok;
    canvas->setSize(256, 128);
    ok = expect(!canvas->isTextureValid(), "resize should invalidate the old render target texture") && ok;
    ok = expect(rawRenderer->viewportWidth == 256 && rawRenderer->viewportHeight == 128,
                "resize should still update renderer viewport") && ok;

    return ok;
}

bool testBoxShadowQueuesWork()
{
    auto renderer = std::make_unique<FakeRenderer>();
    FakeRenderer *rawRenderer = renderer.get();
    std::unique_ptr<wsc::Canvas> canvas = wsc::CanvasLifecycleTestAccess::create(std::move(renderer));

    bool ok = expect(canvas->initializeContext(), "initializeContext should succeed");
    canvas->setSize(200, 100);
    canvas->drawBoxShadow(wsc::RectF(20.0f, 20.0f, 80.0f, 40.0f), 10.0f, 4.0f, 12.0f,
                          3.0f, 5.0f, wsc::Color(0, 0, 0, 128));
    ok = expect(rawRenderer->commandCount() > 0, "drawBoxShadow should queue shadow commands") && ok;

    const std::size_t queued = rawRenderer->commandCount();
    canvas->drawBoxShadow(wsc::RectF(20.0f, 20.0f, 80.0f, 40.0f), 10.0f, 4.0f, 12.0f,
                          3.0f, 5.0f, wsc::Color(0, 0, 0, 0));
    ok = expect(rawRenderer->commandCount() == queued, "transparent drawBoxShadow should be a no-op") && ok;

    return ok;
}

bool testTextShadowQueuesWork()
{
    auto renderer = std::make_unique<FakeRenderer>();
    FakeRenderer *rawRenderer = renderer.get();
    std::unique_ptr<wsc::Canvas> canvas = wsc::CanvasLifecycleTestAccess::create(std::move(renderer));

    bool ok = expect(canvas->initializeContext(), "initializeContext should succeed");
    canvas->setSize(200, 100);

    wsc::Paint textPaint;
    textPaint.setTextSize(16.0f);
    textPaint.setColor(wsc::Color::WHITE);
    canvas->drawText("shadow", 20.0f, 24.0f, textPaint);
    ok = expect(rawRenderer->commandCount() == 1, "plain geometry text should queue one command") && ok;

    rawRenderer->clear();
    textPaint.setShadowLayer(8.0f, 2.0f, 3.0f, wsc::Color(0, 0, 0, 128));
    canvas->drawText("shadow", 20.0f, 24.0f, textPaint);
    ok = expect(rawRenderer->commandCount() > 1, "shadowed text should queue shadow and text commands") && ok;

    rawRenderer->clear();
    textPaint.setShadowLayer(8.0f, 2.0f, 3.0f, wsc::Color(0, 0, 0, 0));
    canvas->drawText("shadow", 20.0f, 24.0f, textPaint);
    ok = expect(rawRenderer->commandCount() == 1, "transparent text shadow should not add commands") && ok;

    return ok;
}

bool testTextStrokeQueuesWork()
{
    auto renderer = std::make_unique<FakeRenderer>();
    FakeRenderer *rawRenderer = renderer.get();
    std::unique_ptr<wsc::Canvas> canvas = wsc::CanvasLifecycleTestAccess::create(std::move(renderer));

    bool ok = expect(canvas->initializeContext(), "initializeContext should succeed");
    canvas->setSize(200, 100);

    wsc::Paint textPaint;
    textPaint.setTextSize(16.0f);
    textPaint.setColor(wsc::Color::WHITE);
    textPaint.setStrokeColor(wsc::Color(20, 30, 40, 255));
    textPaint.setStrokeWidth(4.0f);

    textPaint.setStyle(wsc::Paint::Style::STROKE);
    canvas->drawText("stroke", 20.0f, 24.0f, textPaint);
    const std::size_t strokeCommands = rawRenderer->commandCount();
    ok = expect(strokeCommands > 1, "stroke text should queue multiple offset commands") && ok;

    rawRenderer->clear();
    textPaint.setStyle(wsc::Paint::Style::FILL_AND_STROKE);
    canvas->drawText("stroke", 20.0f, 24.0f, textPaint);
    ok = expect(rawRenderer->commandCount() == strokeCommands + 1,
                "fill-and-stroke text should queue stroke commands plus fill") && ok;

    rawRenderer->clear();
    textPaint.setStyle(wsc::Paint::Style::STROKE);
    textPaint.setStrokeColor(wsc::Color(20, 30, 40, 0));
    canvas->drawText("stroke", 20.0f, 24.0f, textPaint);
    ok = expect(rawRenderer->commandCount() == 0, "transparent stroke-only text should not queue commands") && ok;

    return ok;
}

bool testGradientQueuesShaderDescriptor()
{
    auto renderer = std::make_unique<FakeRenderer>();
    FakeRenderer *rawRenderer = renderer.get();
    std::unique_ptr<wsc::Canvas> canvas = wsc::CanvasLifecycleTestAccess::create(std::move(renderer));

    bool ok = expect(canvas->initializeContext(), "initializeContext should succeed");
    canvas->setSize(200, 100);

    wsc::Paint linear;
    linear.setLinearGradient(0.0f, 0.0f, 100.0f, 0.0f,
                             {
                                 wsc::Paint::ColorStop(0.0f, wsc::Color::RED),
                                 wsc::Paint::ColorStop(0.5f, wsc::Color::GREEN),
                                 wsc::Paint::ColorStop(1.0f, wsc::Color::BLUE),
                             });
    linear.setShaderTileMode(wsc::Paint::ShaderTileMode::MIRROR);
    canvas->drawRect(wsc::RectF(0.0f, 0.0f, 80.0f, 40.0f), linear);
    ok = expect(rawRenderer->commandCount() == 1, "linear gradient rect should queue one path command") && ok;
    const auto *linearCommand = dynamic_cast<DrawPathCommand *>(rawRenderer->commands.front().get());
    ok = expect(linearCommand != nullptr, "linear gradient should queue DrawPathCommand") && ok;
    if (linearCommand != nullptr) {
        const DrawPathData &data = linearCommand->data();
        ok = expect(data.gradientType == DrawGradientType::Linear, "linear gradient should use shader descriptor") && ok;
        ok = expect(data.gradientTileMode == DrawGradientTileMode::Mirror, "gradient tile mode should be forwarded") && ok;
        ok = expect(data.gradientStopCount == 3, "gradient stops should be forwarded") && ok;
        ok = expect(data.colors.empty(), "shader gradient should not require per-vertex colors") && ok;
    }

    rawRenderer->clear();
    wsc::Paint radial;
    radial.setRadialGradient(40.0f, 30.0f, 24.0f, wsc::Color::WHITE, wsc::Color::BLACK);
    canvas->drawCircle(40.0f, 30.0f, 24.0f, radial);
    ok = expect(rawRenderer->commandCount() == 1, "radial gradient circle should queue one path command") && ok;
    const auto *radialCommand = dynamic_cast<DrawPathCommand *>(rawRenderer->commands.front().get());
    ok = expect(radialCommand != nullptr, "radial gradient should queue DrawPathCommand") && ok;
    if (radialCommand != nullptr) {
        const DrawPathData &data = radialCommand->data();
        ok = expect(data.gradientType == DrawGradientType::Radial, "radial gradient should use shader descriptor") && ok;
        ok = expect(data.gradientStopCount == 2, "radial gradient stops should be forwarded") && ok;
    }

    return ok;
}

bool testAsyncReadbackRejectsInvalidState()
{
    auto renderer = std::make_unique<FakeRenderer>();
    std::unique_ptr<wsc::Canvas> canvas = wsc::CanvasLifecycleTestAccess::create(std::move(renderer));

    bool ok = expect(!canvas->readPixelsRGBAAsync({}), "async readback should reject empty callbacks");
    canvas->setSize(16, 16);
    ok = expect(!canvas->readPixelsRGBAAsync([](std::vector<unsigned char>, int, int) {}),
                "async readback should require initialized context") && ok;
    ok = expect(!canvas->hasPendingReadPixelsRGBAAsync(), "failed async readback should not be pending") && ok;
    ok = expect(canvas->pollReadPixelsRGBAAsync(), "poll without pending readback should complete") && ok;
    return ok;
}

bool testContextRecreation()
{
    auto renderer = std::make_unique<FakeRenderer>();
    FakeRenderer *rawRenderer = renderer.get();
    std::unique_ptr<wsc::Canvas> canvas = wsc::CanvasLifecycleTestAccess::create(std::move(renderer));

    bool ok = expect(canvas->initializeContext(), "first initializeContext should succeed");
    canvas->finalizeContext();
    ok = expect(!canvas->isContextInitialized(), "context should be finalized before recreation") && ok;

    ok = expect(canvas->initializeContext(), "initializeContext should succeed after finalize") && ok;
    ok = expect(canvas->isContextInitialized(), "context should be initialized after recreation") && ok;
    ok = expect(rawRenderer->initializeCount == 2, "renderer should initialize again after finalize") && ok;
    ok = expect(rawRenderer->finalizeCount == 1, "renderer should have one explicit finalize") && ok;

    return ok;
}

} // namespace

int main()
{
    bool ok = true;
    ok = testExplicitInitializeAndFinalize() && ok;
    ok = testReleaseResourcesClearsQueuedWork() && ok;
    ok = testCanvasGraphicsStateColorAndBlendAffectPathCommands() && ok;
    ok = testResizeInvalidatesRenderTargetTexture() && ok;
    ok = testBoxShadowQueuesWork() && ok;
    ok = testTextShadowQueuesWork() && ok;
    ok = testTextStrokeQueuesWork() && ok;
    ok = testGradientQueuesShaderDescriptor() && ok;
    ok = testAsyncReadbackRejectsInvalidState() && ok;
    ok = testContextRecreation() && ok;
    return ok ? 0 : 1;
}

#include "wsc/wsc.h"

#include "command/DrawCommand.h"
#include "render/IRenderer.h"

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
        return {};
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
    ok = testContextRecreation() && ok;
    return ok ? 0 : 1;
}

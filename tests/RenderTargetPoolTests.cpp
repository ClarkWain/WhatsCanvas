#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "command/DrawCommand.h"
#include "render/IRenderDevice.h"
#include "render/IRenderTarget.h"
#include "render/RenderTargetPool.h"

namespace {

bool expect(bool condition, const std::string &message)
{
    if (condition) {
        return true;
    }

    std::cerr << "EXPECTATION FAILED: " << message << std::endl;
    return false;
}

class FakeImageResource final : public ImageResource
{
public:
    bool isValid() const override { return true; }
    void bind(const RenderContext &) const override {}
    bool updateRGBA(int, int, int, int, const unsigned char *, bool) override { return true; }
};

class FakeRenderTarget final : public IRenderTarget
{
public:
    FakeRenderTarget(int width, int height, int identifier)
        : width_(width),
          height_(height),
          identifier_(identifier)
    {
    }

    bool isValid() const override { return true; }
    int width() const override { return width_; }
    int height() const override { return height_; }
    bool begin(const OffscreenRenderRequest &) override { return true; }
    void activate() override { activated_ = true; }
    bool isActivated() const override { return activated_; }
    void end() override { activated_ = false; }
    SharedImageResource getImageResource() const override { return std::make_shared<FakeImageResource>(); }
    int identifier() const { return identifier_; }

private:
    int width_ = 0;
    int height_ = 0;
    int identifier_ = 0;
    bool activated_ = false;
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
    std::unique_ptr<IRenderTarget> createRenderTarget(int width, int height) const override
    {
        ++createdCount;
        return std::make_unique<FakeRenderTarget>(width, height, createdCount);
    }
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
        return {};
    }

    mutable int createdCount = 0;
};

bool testPoolReusesMatchingSize()
{
    FakeRenderDevice device;
    RenderTargetPool pool(&device);

    std::unique_ptr<IRenderTarget> first = pool.acquire(64, 32);
    const auto *firstTarget = dynamic_cast<const FakeRenderTarget *>(first.get());
    const int firstIdentifier = firstTarget == nullptr ? 0 : firstTarget->identifier();
    pool.release(std::move(first));

    std::unique_ptr<IRenderTarget> second = pool.acquire(64, 32);
    const auto *secondTarget = dynamic_cast<const FakeRenderTarget *>(second.get());
    const int secondIdentifier = secondTarget == nullptr ? 0 : secondTarget->identifier();

    return expect(device.createdCount == 1, "pool should create one target for matching dimensions")
        && expect(firstIdentifier != 0, "first fake target should be inspectable")
        && expect(secondIdentifier == firstIdentifier, "pool should return the same target for matching dimensions");
}

bool testPoolExpiresIdleTargets()
{
    FakeRenderDevice device;
    RenderTargetPool pool(&device);

    pool.release(pool.acquire(16, 16));
    pool.expire(0);

    return expect(pool.pooledCount() == 0, "pool should remove targets past the idle limit");
}

} // namespace

int main()
{
    const bool ok = testPoolReusesMatchingSize()
        && testPoolExpiresIdleTargets();
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

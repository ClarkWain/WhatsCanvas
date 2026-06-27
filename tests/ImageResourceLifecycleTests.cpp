#include "wsc/wsc.h"

#include "command/DrawCommand.h"
#include "render/IRenderer.h"

#include <cstdint>
#include <fstream>
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

class FakeImageResource final : public ImageResource
{
public:
    explicit FakeImageResource(std::uint32_t handle, bool valid = true)
        : handle_(handle), valid_(valid)
    {
    }

    bool isValid() const override { return valid_; }
    void bind(const RenderContext &) const override {}

    bool updateRGBA(int x, int y, int width, int height, const unsigned char *pixels,
                    bool regenerateMipmaps) override
    {
        if (!valid_ || pixels == nullptr || x < 0 || y < 0 || width <= 0 || height <= 0) {
            return false;
        }

        ++updateCount;
        lastX = x;
        lastY = y;
        lastWidth = width;
        lastHeight = height;
        lastRegenerateMipmaps = regenerateMipmaps;
        return true;
    }

    std::uint32_t handle() const { return handle_; }

    int updateCount = 0;
    int lastX = 0;
    int lastY = 0;
    int lastWidth = 0;
    int lastHeight = 0;
    bool lastRegenerateMipmaps = false;

private:
    std::uint32_t handle_ = 0;
    bool valid_ = true;
};

class FakeRenderer final : public IRenderer
{
public:
    void initializeBackend() override {}
    void finalizeBackend() override {}
    void setViewport(int, int) override {}
    void submit(std::unique_ptr<Command> &&) override {}
    size_t commandCount() const override { return 0; }
    std::vector<std::unique_ptr<Command>> takeCommandsFrom(size_t) override { return {}; }
    void appendCommands(std::vector<std::unique_ptr<Command>> &&) override {}
    bool readPixelsRGBA(std::vector<unsigned char> &) const override { return false; }
    SharedClipMaskResource createClipMaskResource(const ClipMaskPath &) const override { return {}; }

    SharedImageResource createImageResourceRGBA(int width, int height,
                                                const std::vector<unsigned char> &pixels) const override
    {
        return createImageResourceFromImageData(width, height, 4, pixels.data(), false);
    }

    SharedImageResource createImageResourceFromImageData(int width, int height, int channels,
                                                         const unsigned char *pixels,
                                                         bool generateMipmaps) const override
    {
        if (width <= 0 || height <= 0 || channels <= 0 || pixels == nullptr) {
            return {};
        }

        ++createdCount;
        lastCreatedWidth = width;
        lastCreatedHeight = height;
        lastCreatedChannels = channels;
        lastCreateMipmaps = generateMipmaps;
        auto resource = std::make_shared<FakeImageResource>(nextHandle_++);
        lastResource = resource;
        return resource;
    }

    bool updateImageResourceRGBA(const SharedImageResource &imageResource, int x, int y, int width, int height,
                                 const unsigned char *pixels, bool regenerateMipmaps) const override
    {
        ++rendererUpdateRequests;
        return imageResource && imageResource->updateRGBA(x, y, width, height, pixels, regenerateMipmaps);
    }

    SharedImageResource wrapExternalImageResource(ImageResourceHandle handle) const override
    {
        if (!handle.isValid()) {
            return {};
        }

        ++wrappedCount;
        auto resource = std::make_shared<FakeImageResource>(handle.value);
        lastResource = resource;
        return resource;
    }

    const FrameStats &frameStats() const override { return frameStats_; }
    void resetFrameStats() override {}
    RenderResourceStats resourceStats() const override { return {}; }
    SharedImageResource renderCommandsToImageResource(const std::vector<std::unique_ptr<Command>> &,
                                                      const OffscreenRenderRequest &) const override
    {
        return {};
    }
    void resetRenderState() override {}
    void clear() override {}
    void flush() override {}

    mutable int createdCount = 0;
    mutable int wrappedCount = 0;
    mutable int rendererUpdateRequests = 0;
    mutable int lastCreatedWidth = 0;
    mutable int lastCreatedHeight = 0;
    mutable int lastCreatedChannels = 0;
    mutable bool lastCreateMipmaps = false;
    mutable std::shared_ptr<FakeImageResource> lastResource;

private:
    mutable std::uint32_t nextHandle_ = 1;
    FrameStats frameStats_;
};

std::vector<unsigned char> rgbaPixels(int width, int height, unsigned char seed)
{
    std::vector<unsigned char> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);
    for (size_t i = 0; i < pixels.size(); ++i) {
        pixels[i] = static_cast<unsigned char>(seed + static_cast<unsigned char>(i % 29u));
    }
    return pixels;
}

const unsigned char kOneByOnePng[] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
    0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4, 0x89, 0x00, 0x00, 0x00,
    0x0D, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8, 0xCF, 0xC0, 0xF0,
    0x1F, 0x00, 0x05, 0x00, 0x01, 0xFF, 0x89, 0x99, 0x3D, 0x1D, 0x00, 0x00,
    0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82
};

} // namespace

namespace wsc {

class ImageLifecycleTestAccess
{
public:
    static bool loadRGBA(Image &image, IRenderer &renderer, const unsigned char *pixels, int width, int height,
                         bool generateMipmaps)
    {
        return image.loadRGBA(renderer, pixels, width, height, generateMipmaps);
    }

    static bool loadEncodedMemory(Image &image, IRenderer &renderer, const unsigned char *data, int size,
                                  bool generateMipmaps)
    {
        return image.loadEncodedMemory(renderer, data, size, generateMipmaps);
    }

    static bool loadFile(Image &image, IRenderer &renderer, const char *path)
    {
        return image.load(renderer, path);
    }

    static bool replaceRGBA(Image &image, IRenderer &renderer, const unsigned char *pixels, int width, int height,
                            bool generateMipmaps)
    {
        return image.replaceRGBA(renderer, pixels, width, height, generateMipmaps);
    }

    static bool updateRGBA(Image &image, IRenderer &renderer, const unsigned char *pixels, int x, int y, int width,
                           int height, bool regenerateMipmaps)
    {
        return image.updateRGBA(renderer, pixels, x, y, width, height, regenerateMipmaps);
    }

    static bool wrapExternalTexture(Image &image, IRenderer &renderer, std::uint32_t textureId, int width, int height,
                                    bool mipmapsGenerated)
    {
        return image.wrapExternalTexture(renderer, textureId, width, height, mipmapsGenerated);
    }

    static void reset(Image &image)
    {
        image.reset();
    }
};

} // namespace wsc

namespace {

bool testDefaultResetAndMove()
{
    wsc::Image image;
    bool ok = expect(image.getWidth() == 0 && image.getHeight() == 0, "default image should have zero size");
    ok = expect(!image.isTextureValid(), "default image should not be texture-valid") && ok;
    ok = expect(!image.hasMipmaps(), "default image should not report mipmaps") && ok;

    FakeRenderer renderer;
    const std::vector<unsigned char> pixels = rgbaPixels(2, 2, 10);
    ok = expect(wsc::ImageLifecycleTestAccess::loadRGBA(image, renderer, pixels.data(), 2, 2, true),
                "loadRGBA should accept valid pixels") && ok;

    wsc::Image moved(std::move(image));
    ok = expect(!image.isTextureValid(), "moved-from image should be invalid") && ok;
    ok = expect(moved.isTextureValid(), "moved-to image should keep texture validity") && ok;
    ok = expect(moved.getWidth() == 2 && moved.getHeight() == 2, "moved-to image should keep size") && ok;
    ok = expect(moved.hasMipmaps(), "moved-to image should keep mipmap state") && ok;

    wsc::Image assigned;
    assigned = std::move(moved);
    ok = expect(!moved.isTextureValid(), "move-assigned source should be invalid") && ok;
    ok = expect(assigned.isTextureValid(), "move-assigned target should keep texture validity") && ok;

    wsc::ImageLifecycleTestAccess::reset(assigned);
    ok = expect(!assigned.isTextureValid(), "reset image should be invalid") && ok;
    ok = expect(assigned.getWidth() == 0 && assigned.getHeight() == 0, "reset image should clear size") && ok;
    ok = expect(!assigned.hasMipmaps(), "reset image should clear mipmap state") && ok;

    return ok;
}

bool testLoadRejectsInvalidInputs()
{
    FakeRenderer renderer;
    wsc::Image image;
    const std::vector<unsigned char> pixels = rgbaPixels(2, 2, 22);
    bool ok = expect(wsc::ImageLifecycleTestAccess::loadRGBA(image, renderer, pixels.data(), 2, 2, false),
                     "initial load should succeed");

    ok = expect(!wsc::ImageLifecycleTestAccess::loadRGBA(image, renderer, nullptr, 2, 2, false),
                "loadRGBA should reject null pixels") && ok;
    ok = expect(!image.isTextureValid(), "failed loadRGBA should reset existing image") && ok;

    ok = expect(!wsc::ImageLifecycleTestAccess::loadRGBA(image, renderer, pixels.data(), 0, 2, false),
                "loadRGBA should reject non-positive width") && ok;
    ok = expect(!wsc::ImageLifecycleTestAccess::loadRGBA(image, renderer, pixels.data(), 2, -1, false),
                "loadRGBA should reject non-positive height") && ok;

    return ok;
}

bool testEncodedAndFileDecodeLifecycle()
{
    FakeRenderer renderer;
    wsc::Image image;
    bool ok = expect(wsc::ImageLifecycleTestAccess::loadEncodedMemory(
                         image, renderer, kOneByOnePng, static_cast<int>(sizeof(kOneByOnePng)), true),
                     "loadEncodedMemory should decode a valid PNG");
    ok = expect(image.isTextureValid(), "decoded memory image should be texture-valid") && ok;
    ok = expect(image.getWidth() == 1 && image.getHeight() == 1, "decoded memory image should store dimensions") && ok;
    ok = expect(image.hasMipmaps(), "decoded memory image should store mipmap state") && ok;
    ok = expect(renderer.lastCreatedWidth == 1 && renderer.lastCreatedHeight == 1,
                "decoded memory image should create a 1x1 resource") && ok;
    ok = expect(renderer.lastCreatedChannels == 4, "decoded PNG should report RGBA channels") && ok;

    const std::string filePath = "wsc_image_lifecycle_test_1x1.png";
    {
        std::ofstream file(filePath, std::ios::binary);
        file.write(reinterpret_cast<const char *>(kOneByOnePng), static_cast<std::streamsize>(sizeof(kOneByOnePng)));
    }

    wsc::Image fileImage;
    ok = expect(wsc::ImageLifecycleTestAccess::loadFile(fileImage, renderer, filePath.c_str()),
                "loadImage file path should decode a valid PNG") && ok;
    ok = expect(fileImage.isTextureValid(), "decoded file image should be texture-valid") && ok;
    ok = expect(fileImage.getWidth() == 1 && fileImage.getHeight() == 1, "decoded file image should store dimensions") && ok;
    ok = expect(fileImage.hasMipmaps(), "file decode should generate mipmaps") && ok;

    std::remove(filePath.c_str());

    ok = expect(!wsc::ImageLifecycleTestAccess::loadEncodedMemory(image, renderer, nullptr, 0, false),
                "loadEncodedMemory should reject empty data") && ok;
    ok = expect(!image.isTextureValid(), "failed encoded-memory decode should reset image") && ok;
    return ok;
}

bool testReplaceAndUpdateLifecycle()
{
    FakeRenderer renderer;
    wsc::Image image;
    const std::vector<unsigned char> pixels = rgbaPixels(2, 2, 40);
    bool ok = expect(wsc::ImageLifecycleTestAccess::loadRGBA(image, renderer, pixels.data(), 2, 2, false),
                     "initial load should succeed");
    ok = expect(renderer.createdCount == 1, "initial load should create one resource") && ok;

    const std::vector<unsigned char> replacement = rgbaPixels(2, 2, 70);
    ok = expect(wsc::ImageLifecycleTestAccess::replaceRGBA(image, renderer, replacement.data(), 2, 2, false),
                "same-size replace should update existing resource") && ok;
    ok = expect(renderer.createdCount == 1, "same-size replace should not allocate a new resource") && ok;
    ok = expect(renderer.rendererUpdateRequests == 1, "same-size replace should issue one update") && ok;
    ok = expect(renderer.lastResource && renderer.lastResource->lastWidth == 2 && renderer.lastResource->lastHeight == 2,
                "same-size replace should update the full image") && ok;

    const std::vector<unsigned char> larger = rgbaPixels(4, 1, 90);
    ok = expect(wsc::ImageLifecycleTestAccess::replaceRGBA(image, renderer, larger.data(), 4, 1, true),
                "different-size replace should reload resource") && ok;
    ok = expect(renderer.createdCount == 2, "different-size replace should allocate a new resource") && ok;
    ok = expect(image.getWidth() == 4 && image.getHeight() == 1, "different-size replace should update dimensions") && ok;
    ok = expect(image.hasMipmaps(), "different-size replace should set mipmap state") && ok;

    ok = expect(!wsc::ImageLifecycleTestAccess::replaceRGBA(image, renderer, nullptr, 4, 1, false),
                "replace should reject null pixels") && ok;
    ok = expect(image.isTextureValid() && image.getWidth() == 4 && image.getHeight() == 1,
                "failed replace should preserve existing resource") && ok;

    const std::vector<unsigned char> subrect = rgbaPixels(1, 1, 120);
    ok = expect(wsc::ImageLifecycleTestAccess::updateRGBA(image, renderer, subrect.data(), 2, 0, 1, 1, true),
                "valid sub-rect update should succeed") && ok;
    ok = expect(renderer.lastResource && renderer.lastResource->lastX == 2 && renderer.lastResource->lastY == 0,
                "sub-rect update should preserve update offset") && ok;
    ok = expect(image.hasMipmaps(), "sub-rect update should preserve or enable mipmaps") && ok;

    const int previousUpdates = renderer.rendererUpdateRequests;
    ok = expect(!wsc::ImageLifecycleTestAccess::updateRGBA(image, renderer, subrect.data(), 4, 0, 1, 1, true),
                "out-of-bounds update should fail") && ok;
    ok = expect(renderer.rendererUpdateRequests == previousUpdates,
                "out-of-bounds update should not reach renderer") && ok;

    return ok;
}

bool testExternalTextureLifecycle()
{
    FakeRenderer renderer;
    wsc::Image image;
    bool ok = expect(wsc::ImageLifecycleTestAccess::wrapExternalTexture(image, renderer, 42, 16, 8, true),
                     "valid external texture should wrap");
    ok = expect(image.isTextureValid(), "wrapped external texture should be valid") && ok;
    ok = expect(image.getWidth() == 16 && image.getHeight() == 8, "wrapped external texture should store size") && ok;
    ok = expect(image.hasMipmaps(), "wrapped external texture should store mipmap state") && ok;
    ok = expect(renderer.wrappedCount == 1, "valid external texture should call renderer wrap") && ok;
    ok = expect(renderer.lastResource && renderer.lastResource->handle() == 42,
                "wrapped resource should preserve external handle") && ok;

    ok = expect(!wsc::ImageLifecycleTestAccess::wrapExternalTexture(image, renderer, 0, 16, 8, false),
                "zero external texture handle should fail") && ok;
    ok = expect(!image.isTextureValid(), "failed external wrap should reset image") && ok;

    ok = expect(!wsc::ImageLifecycleTestAccess::wrapExternalTexture(image, renderer, 7, -1, 8, false),
                "invalid external texture size should fail") && ok;

    return ok;
}

} // namespace

int main()
{
    bool ok = true;
    ok = testDefaultResetAndMove() && ok;
    ok = testLoadRejectsInvalidInputs() && ok;
    ok = testEncodedAndFileDecodeLifecycle() && ok;
    ok = testReplaceAndUpdateLifecycle() && ok;
    ok = testExternalTextureLifecycle() && ok;
    return ok ? 0 : 1;
}

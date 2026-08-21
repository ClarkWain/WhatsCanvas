#include <wsc/CanvasStats.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <vector>

#include "render/IRenderTarget.h"
#include "render/RenderTypes.h"
#include "render/vulkan/VulkanRenderDevice.h"
#include "wsc/ImageFilter.h"
#include "wsc/wsc.h"

namespace {

bool expect(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << "[VulkanImageFilterTests] FAIL: " << message << std::endl;
    }
    return condition;
}

const unsigned char *pixelAt(const std::vector<unsigned char> &pixels,
                             int width, int x, int y)
{
    return pixels.data()
        + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width)
           + static_cast<std::size_t>(x)) * 4u;
}

std::vector<unsigned char> makeSplitImage(int width, int height)
{
    std::vector<unsigned char> pixels(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            unsigned char *pixel = pixels.data()
                + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width)
                   + static_cast<std::size_t>(x)) * 4u;
            const unsigned char value = x < width / 2 ? 0 : 255;
            pixel[0] = value;
            pixel[1] = value;
            pixel[2] = value;
            pixel[3] = 255;
        }
    }
    return pixels;
}

std::vector<unsigned char> makeTransparentEdgeImage(int width, int height)
{
    std::vector<unsigned char> pixels(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            unsigned char *pixel = pixels.data()
                + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width)
                   + static_cast<std::size_t>(x)) * 4u;
            const bool opaqueBlue = x >= width / 2;
            pixel[0] = opaqueBlue ? 0 : 255;
            pixel[1] = 0;
            pixel[2] = opaqueBlue ? 255 : 0;
            pixel[3] = opaqueBlue ? 255 : 0;
        }
    }
    return pixels;
}

std::vector<unsigned char> makeVerticalStripes(int width, int height)
{
    std::vector<unsigned char> pixels(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const unsigned char value = ((x / 8) % 2) == 0 ? 0 : 255;
            unsigned char *pixel = pixels.data()
                + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width)
                   + static_cast<std::size_t>(x)) * 4u;
            pixel[0] = value;
            pixel[1] = value;
            pixel[2] = value;
            pixel[3] = 255;
        }
    }
    return pixels;
}

std::vector<unsigned char> makeOpaqueRectImage(int width, int height)
{
    std::vector<unsigned char> pixels(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
    for (int y = 8; y < height - 8; ++y) {
        for (int x = 12; x < width - 12; ++x) {
            unsigned char *pixel = pixels.data()
                + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width)
                   + static_cast<std::size_t>(x)) * 4u;
            pixel[0] = 255;
            pixel[1] = 255;
            pixel[2] = 255;
            pixel[3] = 255;
        }
    }
    return pixels;
}

bool readTexture(VulkanRenderDevice &device, const SharedImageResource &texture,
                 int width, int height, std::vector<unsigned char> &pixels)
{
    auto target = device.createRenderTarget(width, height);
    return target && target->isValid()
        && device.renderTexturedQuad(target, texture)
        && device.readPixelsRGBA(width, height, pixels);
}

bool renderBackdropScene(wsc::Canvas::Backend backend, int width, int height,
                         std::vector<unsigned char> &pixels,
                         wsc::Canvas::RenderStats &stats)
{
    auto canvas = wsc::Canvas::create(backend, width, height);
    if (!canvas) {
        return false;
    }
    canvas->initializeContext();
    canvas->beginFrame();

    wsc::Paint black;
    black.setColor(wsc::Color(0, 0, 0, 255));
    black.setAntiAlias(false);
    wsc::Paint white;
    white.setColor(wsc::Color(255, 255, 255, 255));
    white.setAntiAlias(false);
    canvas->drawRect(wsc::RectF(0.0f, 0.0f, width / 2.0f,
                                static_cast<float>(height)), black);
    canvas->drawRect(wsc::RectF(width / 2.0f, 0.0f, width / 2.0f,
                                static_cast<float>(height)), white);

    wsc::LayerOptions options;
    options.setBackdropFilter(wsc::ImageFilter::blur(6.0f));
    wsc::Paint composite;
    composite.setColor(wsc::Color(255, 255, 255, 255));
    canvas->saveLayer(wsc::RectF(24.0f, 8.0f, 48.0f, 48.0f),
                      composite, options);
    canvas->restore();
    canvas->endFrame();
    stats = canvas->getRenderStats();
    return canvas->readPixelsRGBA(pixels);
}

bool renderImageFilterLayerScene(wsc::Canvas::Backend backend, int width, int height,
                                 std::vector<unsigned char> &pixels)
{
    auto canvas = wsc::Canvas::create(backend, width, height);
    if (!canvas) {
        return false;
    }
    canvas->initializeContext();
    canvas->beginFrame();

    wsc::Paint composite;
    composite.setColor(wsc::Color(255, 255, 255, 255));
    wsc::LayerOptions options;
    options.setImageFilter(wsc::ImageFilter::blur(3.0f));
    canvas->saveLayer(
        wsc::RectF(0.0f, 0.0f, static_cast<float>(width),
                   static_cast<float>(height)),
        composite, options);

    wsc::Paint top;
    top.setColor(wsc::Color(240, 32, 24, 255));
    top.setAntiAlias(false);
    wsc::Paint bottom = top;
    bottom.setColor(wsc::Color(24, 48, 240, 255));
    canvas->drawRect(
        wsc::RectF(0.0f, 0.0f, static_cast<float>(width),
                   static_cast<float>(height / 2)),
        top);
    canvas->drawRect(
        wsc::RectF(0.0f, static_cast<float>(height / 2),
                   static_cast<float>(width), static_cast<float>(height / 2)),
        bottom);
    canvas->restore();
    canvas->endFrame();
    return canvas->readPixelsRGBA(pixels);
}

bool renderComposableFilterScene(
    wsc::Canvas::Backend backend, int width, int height,
    std::vector<unsigned char> &pixels)
{
    auto canvas = wsc::Canvas::create(backend, width, height);
    if (!canvas || !canvas->initializeContext()) {
        return false;
    }
    const std::array<float, 20> redToGreen = {
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
    };
    wsc::ImageFilterChain filters;
    filters.appendColorMatrix(redToGreen)
        .appendOffset(8.0f, 6.0f);
    wsc::LayerOptions options;
    options.setImageFilter(filters);
    wsc::Paint black;
    black.setColor(wsc::Color(0, 0, 0, 255));
    black.setAntiAlias(false);
    wsc::Paint red = black;
    red.setColor(wsc::Color(255, 0, 0, 255));
    wsc::Paint composite;
    composite.setColor(wsc::Color::WHITE);

    canvas->beginFrame();
    canvas->drawRect(
        wsc::RectF(0.0f, 0.0f, static_cast<float>(width),
                   static_cast<float>(height)),
        black);
    canvas->saveLayer(
        wsc::RectF(0.0f, 0.0f, static_cast<float>(width),
                   static_cast<float>(height)),
        composite, options);
    canvas->drawRect(wsc::RectF(16.0f, 16.0f, 16.0f, 16.0f), red);
    canvas->restore();
    canvas->endFrame();
    return canvas->readPixelsRGBA(pixels);
}

bool renderTranslucentFilterLayerScene(
    wsc::Canvas::Backend backend, int width, int height,
    std::vector<unsigned char> &pixels)
{
    auto canvas = wsc::Canvas::create(backend, width, height);
    if (!canvas) {
        return false;
    }
    canvas->initializeContext();
    canvas->beginFrame();

    wsc::Paint composite;
    composite.setColor(wsc::Color(255, 255, 255, 255));
    wsc::LayerOptions options;
    options.setImageFilter(wsc::ImageFilter::blur(2.0f));
    canvas->saveLayer(
        wsc::RectF(0.0f, 0.0f, static_cast<float>(width),
                   static_cast<float>(height)),
        composite, options);
    wsc::Paint red;
    red.setColor(wsc::Color(255, 0, 0, 128));
    red.setAntiAlias(false);
    canvas->drawRect(wsc::RectF(12.0f, 12.0f, 40.0f, 40.0f), red);
    canvas->restore();
    canvas->endFrame();
    return canvas->readPixelsRGBA(pixels);
}

bool renderInnerShadowLayerScene(wsc::Canvas::Backend backend, int width, int height,
                                 std::vector<unsigned char> &pixels,
                                 wsc::Canvas::RenderStats &stats)
{
    auto canvas = wsc::Canvas::create(backend, width, height);
    if (!canvas) {
        return false;
    }
    canvas->initializeContext();
    canvas->beginFrame();

    wsc::Paint background;
    background.setColor(wsc::Color(18, 30, 54, 255));
    background.setAntiAlias(false);
    canvas->drawRect(
        wsc::RectF(0.0f, 0.0f, static_cast<float>(width),
                   static_cast<float>(height)),
        background);

    wsc::LayerOptions options;
    options.setImageFilter(wsc::ImageFilter::innerShadow(
        6.0f, 4.0f, 4.0f, 3.0f, wsc::Color(0, 0, 0, 220)));
    wsc::Paint composite;
    composite.setColor(wsc::Color(255, 255, 255, 255));
    canvas->saveLayer(wsc::RectF(16.0f, 12.0f, 64.0f, 48.0f),
                      composite, options);
    wsc::Paint surface;
    surface.setColor(wsc::Color(240, 244, 252, 255));
    surface.setAntiAlias(false);
    canvas->drawRect(wsc::RectF(16.0f, 12.0f, 64.0f, 48.0f), surface);
    canvas->restore();
    canvas->endFrame();
    stats = canvas->getRenderStats();
    return canvas->readPixelsRGBA(pixels);
}

bool renderTranslucentInnerShadowScene(wsc::Canvas::Backend backend,
                                       int width, int height,
                                       std::vector<unsigned char> &pixels)
{
    auto canvas = wsc::Canvas::create(backend, width, height);
    if (!canvas) {
        return false;
    }
    canvas->initializeContext();
    canvas->beginFrame();

    wsc::LayerOptions options;
    options.setImageFilter(wsc::ImageFilter::innerShadow(
        7.0f, -3.5f, 2.25f, wsc::Color(20, 120, 240, 180)));
    wsc::Paint composite;
    composite.setColor(wsc::Color(255, 255, 255, 255));
    canvas->saveLayer(wsc::RectF(8.0f, 8.0f, 48.0f, 48.0f),
                      composite, options);
    wsc::Paint surface;
    surface.setColor(wsc::Color(220, 60, 30, 128));
    surface.setAntiAlias(false);
    canvas->drawRect(wsc::RectF(8.0f, 8.0f, 48.0f, 48.0f), surface);
    canvas->restore();
    canvas->endFrame();
    return canvas->readPixelsRGBA(pixels);
}

bool renderHalfPixelFullBleedInnerShadow(wsc::Canvas::Backend backend,
                                         int width, int height,
                                         std::vector<unsigned char> &pixels)
{
    auto canvas = wsc::Canvas::create(backend, width, height);
    if (!canvas) {
        return false;
    }
    canvas->initializeContext();
    canvas->beginFrame();

    wsc::LayerOptions options;
    options.setImageFilter(wsc::ImageFilter::innerShadow(
        0.0f, -0.5f, 0.0f, wsc::Color(0, 0, 0, 255)));
    wsc::Paint paint;
    paint.setColor(wsc::Color(255, 255, 255, 255));
    paint.setAntiAlias(false);
    canvas->saveLayer(
        wsc::RectF(0.0f, 0.0f, static_cast<float>(width),
                   static_cast<float>(height)),
        paint, options);
    canvas->drawRect(
        wsc::RectF(0.0f, 0.0f, static_cast<float>(width),
                   static_cast<float>(height)),
        paint);
    canvas->restore();
    canvas->endFrame();
    return canvas->readPixelsRGBA(pixels);
}

bool renderCroppedClipScene(wsc::Canvas::Backend backend, int width, int height,
                            std::vector<unsigned char> &pixels)
{
    auto canvas = wsc::Canvas::create(backend, width, height);
    if (!canvas) {
        return false;
    }
    canvas->initializeContext();

    std::vector<unsigned char> imagePixels(16u * 16u * 4u, 255);
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            unsigned char *pixel =
                imagePixels.data() + (static_cast<std::size_t>(y) * 16u + x) * 4u;
            pixel[0] = x < 8 ? 255 : 0;
            pixel[1] = y < 8 ? 220 : 32;
            pixel[2] = x < 8 ? 16 : 255;
        }
    }
    wsc::Image image;
    if (!canvas->loadImageFromRGBA(image, imagePixels, 16, 16)) {
        return false;
    }

    canvas->beginFrame();
    wsc::Paint background;
    background.setColor(wsc::Color(8, 10, 14, 255));
    background.setAntiAlias(false);
    canvas->drawRect(
        wsc::RectF(0.0f, 0.0f, static_cast<float>(width),
                   static_cast<float>(height)),
        background);

    wsc::Paint composite;
    composite.setColor(wsc::Color(255, 255, 255, 255));
    canvas->saveLayer(wsc::RectF(16.0f, 8.0f, 64.0f, 56.0f), composite);
    wsc::Path clip;
    clip.addRoundRect(wsc::RectF(20.0f, 12.0f, 56.0f, 48.0f), 8.0f);
    canvas->clipPath(clip);

    wsc::Paint gradient;
    gradient.setLinearGradient(
        20.0f, 0.0f, 48.0f, 0.0f,
        {wsc::Paint::ColorStop(0.0f, wsc::Color(255, 32, 16, 255)),
         wsc::Paint::ColorStop(1.0f, wsc::Color(16, 240, 64, 255))});
    gradient.setAntiAlias(false);
    canvas->drawRect(wsc::RectF(16.0f, 8.0f, 32.0f, 56.0f), gradient);

    wsc::Paint imagePaint;
    imagePaint.setColor(wsc::Color(255, 255, 255, 255));
    imagePaint.setAntiAlias(false);
    canvas->drawImage(
        image, wsc::RectF(48.0f, 8.0f, 32.0f, 56.0f), imagePaint);
    canvas->restore();
    canvas->endFrame();
    return canvas->readPixelsRGBA(pixels);
}

} // namespace

int main()
{
    if (!VulkanRenderDevice::isAvailable()) {
        std::cerr << "[VulkanImageFilterTests] FAIL: Vulkan support was not compiled in."
                  << std::endl;
        return 1;
    }

    VulkanRenderDevice device;
    device.initializeBackend();
    if (!expect(device.isDeviceReady(), "Vulkan device was not created")) {
        return 1;
    }

    bool ok = true;
    {
        constexpr int width = 64;
        constexpr int height = 32;
        auto source = device.createImageResourceRGBA(
            width, height, makeSplitImage(width, height));
        FilterExecutionStats stats;
        auto filtered = device.filterImageResource(
            source, width, height, wsc::ImageFilter::blur(6.0f), &stats);
        ok = expect(filtered && filtered->isValid(), "clamp blur returned no texture") && ok;
        ok = expect(device.nativeImageHandle(filtered).isValid(),
                    "filtered result is not a device-local Vulkan image") && ok;
        ok = expect(stats.passCount == 2 && !stats.downsampled,
                    "small blur should report two full-resolution passes") && ok;
        ok = expect(stats.pixelPassCount
                        == static_cast<std::size_t>(width * height * 2),
                    "small blur pixel-pass count is wrong") && ok;

        std::vector<unsigned char> pixels;
        ok = expect(readTexture(device, filtered, width, height, pixels),
                    "clamp blur readback failed") && ok;
        if (!pixels.empty()) {
            const auto *blackSide = pixelAt(pixels, width, 29, height / 2);
            const auto *whiteSide = pixelAt(pixels, width, 34, height / 2);
            const auto *farBlack = pixelAt(pixels, width, 8, height / 2);
            const auto *farWhite = pixelAt(pixels, width, 55, height / 2);
            ok = expect(blackSide[0] > 0 && blackSide[0] < 128,
                        "blur did not spread white into the black side") && ok;
            ok = expect(whiteSide[0] > 128 && whiteSide[0] < 255,
                        "blur did not spread black into the white side") && ok;
            ok = expect(farBlack[0] == 0 && farWhite[0] == 255,
                        "blur changed pixels outside its kernel reach") && ok;
        }
    }

    {
        constexpr int width = 64;
        constexpr int height = 48;
        std::vector<unsigned char> vulkanPixels;
        std::vector<unsigned char> softwarePixels;
        ok = expect(renderHalfPixelFullBleedInnerShadow(
                        wsc::Canvas::Backend::Vulkan, width, height,
                        vulkanPixels),
                    "Vulkan half-pixel full-bleed inner shadow failed") && ok;
        ok = expect(renderHalfPixelFullBleedInnerShadow(
                        wsc::Canvas::Backend::Software, width, height,
                        softwarePixels),
                    "Software half-pixel full-bleed inner shadow failed") && ok;
        if (!vulkanPixels.empty()) {
            const auto *transition =
                pixelAt(vulkanPixels, width, width - 1, height / 2);
            const auto *center = pixelAt(vulkanPixels, width, width / 2, height / 2);
            ok = expect(transition[0] >= 126 && transition[0] <= 129,
                        "Vulkan Decal bilinear sampling missed its half-pixel edge") && ok;
            ok = expect(center[0] == 255,
                        "Vulkan half-pixel inner shadow changed the center") && ok;
        }
        if (vulkanPixels.size() == softwarePixels.size() && !vulkanPixels.empty()) {
            int maxDifference = 0;
            for (std::size_t i = 0; i < vulkanPixels.size(); ++i) {
                maxDifference = std::max(
                    maxDifference,
                    std::abs(static_cast<int>(vulkanPixels[i])
                             - static_cast<int>(softwarePixels[i])));
            }
            ok = expect(maxDifference <= 1,
                        "half-pixel full-bleed Vulkan output drifted from Software") && ok;
        } else {
            ok = expect(false,
                        "half-pixel full-bleed parity image sizes differ") && ok;
        }
    }

    {
        constexpr int width = 160;
        constexpr int height = 128;
        const std::vector<unsigned char> opaqueWhite(
            static_cast<std::size_t>(width * height * 4), 255);
        auto source = device.createImageResourceRGBA(width, height, opaqueWhite);
        FilterExecutionStats stats;
        auto filtered = device.filterImageResource(
            source, width, height,
            wsc::ImageFilter::innerShadow(
                32.0f, 10.0f, 8.0f, wsc::Color(0, 0, 0, 220)),
            &stats);
        std::vector<unsigned char> pixels;
        ok = expect(readTexture(device, filtered, width, height, pixels),
                    "downsampled full-bleed inner-shadow readback failed") && ok;
        ok = expect(stats.passCount == 3 && stats.downsampled,
                    "large inner shadow should report downsampling and three passes") && ok;
        ok = expect(stats.pixelPassCount == 30720,
                    "downsampled inner-shadow pixel-pass count is wrong") && ok;
        if (!pixels.empty()) {
            const auto *corner = pixelAt(pixels, width, 0, 0);
            const auto *center = pixelAt(pixels, width, width / 2, height / 2);
            ok = expect(corner[0] < 160,
                        "Decal sampling should shade a full-bleed Vulkan edge") && ok;
            ok = expect(center[0] > 245,
                        "large inner shadow should preserve its center") && ok;
        }
    }

    {
        constexpr int width = 32;
        constexpr int height = 32;
        const std::vector<unsigned char> opaqueWhite(
            static_cast<std::size_t>(width * height * 4), 255);
        auto source = device.createImageResourceRGBA(width, height, opaqueWhite);
        auto filtered = device.filterImageResource(
            source, width, height,
            wsc::ImageFilter::blur(6.0f, wsc::ImageFilter::TileMode::Decal));
        std::vector<unsigned char> pixels;
        ok = expect(readTexture(device, filtered, width, height, pixels),
                    "decal blur readback failed") && ok;
        if (!pixels.empty()) {
            const auto *corner = pixelAt(pixels, width, 0, 0);
            const auto *center = pixelAt(pixels, width, width / 2, height / 2);
            ok = expect(corner[3] < 245,
                        "decal blur should fade alpha at the image boundary") && ok;
            ok = expect(center[3] >= 254,
                        "decal blur should preserve opaque interior alpha") && ok;
        }
    }

    {
        constexpr int width = 32;
        constexpr int height = 16;
        std::vector<unsigned char> red(
            static_cast<std::size_t>(width * height * 4), 255);
        for (std::size_t i = 0; i < red.size(); i += 4) {
            red[i + 1] = 0;
            red[i + 2] = 0;
        }
        auto source = device.createImageResourceRGBA(width, height, red);
        wsc::ImageFilter grayscale = wsc::ImageFilter::blur(1.0f);
        grayscale.setColorAdjustment(0.0f);
        auto filtered =
            device.filterImageResource(source, width, height, grayscale);
        std::vector<unsigned char> pixels;
        ok = expect(readTexture(device, filtered, width, height, pixels),
                    "color-adjusted blur readback failed") && ok;
        if (!pixels.empty()) {
            const auto *center = pixelAt(pixels, width, width / 2, height / 2);
            const int minChannel = std::min({center[0], center[1], center[2]});
            const int maxChannel = std::max({center[0], center[1], center[2]});
            ok = expect(maxChannel - minChannel <= 1,
                        "zero saturation did not produce grayscale") && ok;
            ok = expect(center[3] == 255,
                        "color adjustment changed opaque alpha") && ok;
        }
    }

    {
        constexpr int width = 160;
        constexpr int height = 128;
        auto source = device.createImageResourceRGBA(
            width, height, makeSplitImage(width, height));
        FilterExecutionStats stats;
        auto filtered = device.filterImageResource(
            source, width, height, wsc::ImageFilter::blur(32.0f), &stats);
        ok = expect(filtered && filtered->isValid(),
                    "downsampled blur returned no texture") && ok;
        ok = expect(stats.passCount == 3 && stats.downsampled,
                    "large blur should report downsample plus restore") && ok;
        ok = expect(stats.pixelPassCount == 30720,
                    "downsampled blur pixel-pass count is wrong") && ok;
        std::vector<unsigned char> pixels;
        ok = expect(readTexture(device, filtered, width, height, pixels),
                    "downsampled blur readback failed") && ok;
        if (!pixels.empty()) {
            const auto *blackSide = pixelAt(pixels, width, 68, height / 2);
            const auto *whiteSide = pixelAt(pixels, width, 92, height / 2);
            ok = expect(blackSide[0] > 0 && blackSide[0] < 128,
                        "downsampled blur did not soften the black side") && ok;
            ok = expect(whiteSide[0] > 128 && whiteSide[0] < 255,
                        "downsampled blur did not soften the white side") && ok;
        }
    }

    {
        constexpr int width = 160;
        constexpr int height = 128;
        auto source = device.createImageResourceRGBA(
            width, height, makeTransparentEdgeImage(width, height));
        auto filtered = device.filterImageResource(
            source, width, height, wsc::ImageFilter::blur(32.0f));
        std::vector<unsigned char> pixels;
        ok = expect(readTexture(device, filtered, width, height, pixels),
                    "transparent-edge blur readback failed") && ok;
        if (!pixels.empty()) {
            const auto *edge = pixelAt(pixels, width, 72, height / 2);
            ok = expect(edge[3] > 0 && edge[2] > 0,
                        "opaque blue did not spread into transparent pixels") && ok;
            ok = expect(edge[0] <= 2,
                        "transparent red RGB contaminated the blurred blue edge") && ok;
        }
    }

    {
        constexpr int width = 160;
        constexpr int height = 128;
        auto source = device.createImageResourceRGBA(
            width, height, makeVerticalStripes(width, height));
        FilterExecutionStats stats;
        auto filtered = device.filterImageResource(
            source, width, height, wsc::ImageFilter::blur(0.0f, 32.0f), &stats);
        std::vector<unsigned char> pixels;
        ok = expect(readTexture(device, filtered, width, height, pixels),
                    "anisotropic blur readback failed") && ok;
        ok = expect(stats.downsampled && stats.pixelPassCount == 40960,
                    "anisotropic blur should downsample only its vertical axis") && ok;
        if (!pixels.empty()) {
            const auto *blackStripe = pixelAt(pixels, width, 4, height / 2);
            const auto *whiteStripe = pixelAt(pixels, width, 12, height / 2);
            ok = expect(blackStripe[0] < 8 && whiteStripe[0] > 247,
                        "vertical-only blur lost horizontal stripe detail") && ok;
        }
    }

    {
        constexpr int width = 64;
        constexpr int height = 48;
        auto source = device.createImageResourceRGBA(
            width, height, makeOpaqueRectImage(width, height));
        FilterExecutionStats stats;
        auto filtered = device.filterImageResource(
            source, width, height,
            wsc::ImageFilter::innerShadow(
                6.0f, 4.0f, 4.0f, 3.0f, wsc::Color(0, 0, 0, 220)),
            &stats);
        ok = expect(filtered && filtered->isValid(),
                    "inner shadow returned no texture") && ok;
        ok = expect(stats.passCount == 3 && !stats.downsampled,
                    "small inner shadow should report three full-resolution passes") && ok;
        std::vector<unsigned char> pixels;
        ok = expect(readTexture(device, filtered, width, height, pixels),
                    "inner-shadow readback failed") && ok;
        if (!pixels.empty()) {
            const auto *outside = pixelAt(pixels, width, 4, 4);
            const auto *topLeft = pixelAt(pixels, width, 13, 9);
            const auto *center = pixelAt(pixels, width, 32, 24);
            const auto *bottomRight = pixelAt(pixels, width, 50, 38);
            ok = expect(outside[3] == 0,
                        "inner shadow leaked outside the source silhouette") && ok;
            ok = expect(topLeft[0] < 180 && topLeft[3] == 255,
                        "inner shadow did not shade the offset-facing edge") && ok;
            ok = expect(center[0] > 245 && center[3] == 255,
                        "inner shadow changed the opaque center") && ok;
            ok = expect(bottomRight[0] > topLeft[0],
                        "inner-shadow offset direction is reversed") && ok;
        }
    }

    const std::string deviceName = device.selectedDeviceName();
    device.finalizeBackend();

    {
        constexpr int width = 96;
        constexpr int height = 64;
        std::vector<unsigned char> vulkanPixels;
        wsc::Canvas::RenderStats vulkanStats;
        ok = expect(renderBackdropScene(wsc::Canvas::Backend::Vulkan,
                                        width, height, vulkanPixels, vulkanStats),
                    "Vulkan Canvas backdrop readback failed") && ok;
        if (!vulkanPixels.empty()) {
            const auto *outsideBlack = pixelAt(vulkanPixels, width, 8, height / 2);
            const auto *insideBlack = pixelAt(vulkanPixels, width, 46, height / 2);
            const auto *insideWhite = pixelAt(vulkanPixels, width, 50, height / 2);
            const auto *outsideWhite = pixelAt(vulkanPixels, width, 87, height / 2);
            ok = expect(outsideBlack[0] == 0 && outsideWhite[0] == 255,
                        "Canvas backdrop changed pixels outside the layer") && ok;
            ok = expect(insideBlack[0] > 0 && insideBlack[0] < 128,
                        "Canvas backdrop did not blur into its black side") && ok;
            ok = expect(insideWhite[0] > 128 && insideWhite[0] < 255,
                        "Canvas backdrop did not blur into its white side") && ok;
        }
        ok = expect(vulkanStats.filterCount == 1 && vulkanStats.filterPassCount == 2,
                    "Canvas did not expose Vulkan filter execution stats") && ok;

        std::vector<unsigned char> softwarePixels;
        wsc::Canvas::RenderStats softwareStats;
        ok = expect(renderBackdropScene(wsc::Canvas::Backend::Software,
                                        width, height, softwarePixels, softwareStats),
                    "Software Canvas parity scene failed") && ok;
        if (softwarePixels.size() == vulkanPixels.size() && !vulkanPixels.empty()) {
            int maxDifference = 0;
            std::uint64_t totalDifference = 0;
            std::size_t comparedChannels = 0;
            for (int y = 8; y < 56; ++y) {
                for (int x = 24; x < 72; ++x) {
                    const std::size_t index =
                        (static_cast<std::size_t>(y) * width + x) * 4u;
                    for (int channel = 0; channel < 4; ++channel) {
                        const int difference = std::abs(
                            static_cast<int>(vulkanPixels[index + channel])
                            - static_cast<int>(softwarePixels[index + channel]));
                        maxDifference = std::max(maxDifference, difference);
                        totalDifference += static_cast<std::uint64_t>(difference);
                        ++comparedChannels;
                    }
                }
            }
            const double meanDifference = comparedChannels > 0
                ? static_cast<double>(totalDifference) / comparedChannels : 0.0;
            if (maxDifference > 3 || meanDifference > 0.5) {
                std::cerr << "[VulkanImageFilterTests] parity max_difference="
                          << maxDifference << " mean_difference=" << meanDifference
                          << std::endl;
                std::cerr << "[VulkanImageFilterTests] transition Vulkan/Software:";
                for (int x = 40; x <= 56; ++x) {
                    const std::size_t index =
                        (static_cast<std::size_t>(height / 2) * width + x) * 4u;
                    std::cerr << " " << x << "="
                              << static_cast<int>(vulkanPixels[index]) << "/"
                              << static_cast<int>(softwarePixels[index]);
                }
                std::cerr << std::endl;
            }
            ok = expect(maxDifference <= 3 && meanDifference <= 0.5,
                        "Vulkan backdrop pixels drifted from the Software reference") && ok;
        } else {
            ok = expect(false, "Vulkan/Software parity image sizes differ") && ok;
        }
    }

    {
        constexpr int width = 64;
        constexpr int height = 64;
        std::vector<unsigned char> vulkanPixels;
        std::vector<unsigned char> softwarePixels;
        ok = expect(renderTranslucentInnerShadowScene(
                        wsc::Canvas::Backend::Vulkan, width, height,
                        vulkanPixels),
                    "Vulkan translucent inner-shadow scene failed") && ok;
        ok = expect(renderTranslucentInnerShadowScene(
                        wsc::Canvas::Backend::Software, width, height,
                        softwarePixels),
                    "Software translucent inner-shadow scene failed") && ok;
        if (!vulkanPixels.empty()) {
            const auto *edge = pixelAt(vulkanPixels, width, 54, 9);
            const auto *center = pixelAt(vulkanPixels, width, 32, 32);
            const auto *outside = pixelAt(vulkanPixels, width, 4, 32);
            if (!(center[0] >= 105 && center[0] <= 115
                  && center[1] >= 27 && center[1] <= 33
                  && center[2] >= 12 && center[2] <= 18)) {
                std::cerr << "[VulkanImageFilterTests] translucent inner-shadow "
                             "center="
                          << static_cast<int>(center[0]) << ","
                          << static_cast<int>(center[1]) << ","
                          << static_cast<int>(center[2]) << ","
                          << static_cast<int>(center[3]) << std::endl;
            }
            ok = expect(center[3] >= 126 && center[3] <= 130,
                        "inner shadow changed fractional source alpha") && ok;
            ok = expect(center[0] >= 105 && center[0] <= 115
                            && center[1] >= 27 && center[1] <= 33
                            && center[2] >= 12 && center[2] <= 18,
                        "inner shadow changed premultiplied translucent center color") && ok;
            ok = expect(edge[2] > center[2] && edge[0] < center[0],
                        "colored inner shadow did not tint the translucent edge") && ok;
            ok = expect(outside[3] == 0 && outside[0] == 0
                            && outside[1] == 0 && outside[2] == 0,
                        "translucent inner shadow contaminated transparent pixels") && ok;
        }
        if (vulkanPixels.size() == softwarePixels.size() && !vulkanPixels.empty()) {
            int maxDifference = 0;
            std::uint64_t totalDifference = 0;
            for (std::size_t i = 0; i < vulkanPixels.size(); ++i) {
                const int difference = std::abs(
                    static_cast<int>(vulkanPixels[i])
                    - static_cast<int>(softwarePixels[i]));
                maxDifference = std::max(maxDifference, difference);
                totalDifference += static_cast<std::uint64_t>(difference);
            }
            const double meanDifference =
                static_cast<double>(totalDifference) / vulkanPixels.size();
            ok = expect(maxDifference <= 4 && meanDifference <= 0.75,
                        "translucent inner shadow drifted from Software") && ok;
        } else {
            ok = expect(false,
                        "translucent inner-shadow parity image sizes differ") && ok;
        }
    }

    {
        constexpr int width = 64;
        constexpr int height = 64;
        std::vector<unsigned char> vulkanPixels;
        std::vector<unsigned char> softwarePixels;
        ok = expect(renderImageFilterLayerScene(
                        wsc::Canvas::Backend::Vulkan, width, height, vulkanPixels),
                    "Vulkan public image-filter layer scene failed") && ok;
        ok = expect(renderImageFilterLayerScene(
                        wsc::Canvas::Backend::Software, width, height, softwarePixels),
                    "Software public image-filter layer scene failed") && ok;
        if (!vulkanPixels.empty()) {
            const auto *top = pixelAt(vulkanPixels, width, width / 2, 12);
            const auto *bottom = pixelAt(vulkanPixels, width, width / 2, 52);
            ok = expect(top[0] > top[2] * 3,
                        "filtered saveLayer flipped the red top half") && ok;
            ok = expect(bottom[2] > bottom[0] * 3,
                        "filtered saveLayer flipped the blue bottom half") && ok;
        }
        ok = expect(vulkanPixels.size() == softwarePixels.size(),
                    "public image-filter parity image sizes differ") && ok;
    }

    {
        constexpr int width = 64;
        constexpr int height = 64;
        std::vector<unsigned char> vulkanPixels;
        std::vector<unsigned char> softwarePixels;
        ok = expect(renderComposableFilterScene(
                        wsc::Canvas::Backend::Vulkan,
                        width, height, vulkanPixels),
                    "Vulkan composable filter scene failed") && ok;
        ok = expect(renderComposableFilterScene(
                        wsc::Canvas::Backend::Software,
                        width, height, softwarePixels),
                    "Software composable filter scene failed") && ok;
        if (!vulkanPixels.empty()) {
            const auto *shifted =
                pixelAt(vulkanPixels, width, 28, 26);
            const auto *oldPosition =
                pixelAt(vulkanPixels, width, 18, 18);
            ok = expect(shifted[0] < 4 && shifted[1] > 251
                            && shifted[2] < 4,
                        "Vulkan should execute color matrix before offset") && ok;
            ok = expect(oldPosition[0] < 4 && oldPosition[1] < 4
                            && oldPosition[2] < 4,
                        "Vulkan offset should expose the background") && ok;
        }
        ok = expect(vulkanPixels == softwarePixels,
                    "composable filter output should exactly match Software") && ok;
    }

    {
        constexpr int width = 64;
        constexpr int height = 64;
        std::vector<unsigned char> vulkanPixels;
        std::vector<unsigned char> softwarePixels;
        ok = expect(renderTranslucentFilterLayerScene(
                        wsc::Canvas::Backend::Vulkan, width, height, vulkanPixels),
                    "Vulkan translucent image-filter layer failed") && ok;
        ok = expect(renderTranslucentFilterLayerScene(
                        wsc::Canvas::Backend::Software, width, height, softwarePixels),
                    "Software translucent image-filter layer failed") && ok;
        if (!vulkanPixels.empty()) {
            const auto *center = pixelAt(vulkanPixels, width, 32, 32);
            ok = expect(center[0] >= 118 && center[0] <= 138,
                        "premultiplied layer RGB was multiplied by alpha twice") && ok;
            ok = expect(center[3] >= 118 && center[3] <= 138,
                        "translucent filtered layer changed center alpha") && ok;
            ok = expect(center[1] <= 2 && center[2] <= 2,
                        "translucent red layer gained unrelated color") && ok;
        }
        if (vulkanPixels.size() == softwarePixels.size() && !vulkanPixels.empty()) {
            const auto *vkCenter = pixelAt(vulkanPixels, width, 32, 32);
            const auto *swCenter = pixelAt(softwarePixels, width, 32, 32);
            ok = expect(std::abs(static_cast<int>(vkCenter[0])
                                 - static_cast<int>(swCenter[0])) <= 3
                            && std::abs(static_cast<int>(vkCenter[3])
                                       - static_cast<int>(swCenter[3])) <= 3,
                        "translucent filter layer drifted from Software") && ok;
        } else {
            ok = expect(false, "translucent parity image sizes differ") && ok;
        }
    }

    {
        constexpr int width = 96;
        constexpr int height = 72;
        std::vector<unsigned char> vulkanPixels;
        std::vector<unsigned char> softwarePixels;
        wsc::Canvas::RenderStats vulkanStats;
        wsc::Canvas::RenderStats softwareStats;
        ok = expect(renderInnerShadowLayerScene(
                        wsc::Canvas::Backend::Vulkan, width, height,
                        vulkanPixels, vulkanStats),
                    "Vulkan public inner-shadow layer failed") && ok;
        ok = expect(renderInnerShadowLayerScene(
                        wsc::Canvas::Backend::Software, width, height,
                        softwarePixels, softwareStats),
                    "Software public inner-shadow layer failed") && ok;
        if (!vulkanPixels.empty()) {
            const auto *outside = pixelAt(vulkanPixels, width, 8, 8);
            const auto *topLeft = pixelAt(vulkanPixels, width, 17, 13);
            const auto *center = pixelAt(vulkanPixels, width, 48, 36);
            ok = expect(outside[0] == 18 && outside[1] == 30 && outside[2] == 54,
                        "Canvas inner shadow changed pixels outside the layer") && ok;
            ok = expect(topLeft[0] < center[0] && topLeft[1] < center[1],
                        "Canvas inner shadow did not darken its inside edge") && ok;
        }
        ok = expect(vulkanStats.filterCount == 1
                        && vulkanStats.filterPassCount == 3,
                    "Canvas did not expose Vulkan inner-shadow pass statistics") && ok;
        if (vulkanPixels.size() == softwarePixels.size() && !vulkanPixels.empty()) {
            int maxDifference = 0;
            std::uint64_t totalDifference = 0;
            std::size_t comparedChannels = 0;
            for (int y = 12; y < 60; ++y) {
                for (int x = 16; x < 80; ++x) {
                    const std::size_t index =
                        (static_cast<std::size_t>(y) * width + x) * 4u;
                    for (int channel = 0; channel < 4; ++channel) {
                        const int difference = std::abs(
                            static_cast<int>(vulkanPixels[index + channel])
                            - static_cast<int>(softwarePixels[index + channel]));
                        maxDifference = std::max(maxDifference, difference);
                        totalDifference += static_cast<std::uint64_t>(difference);
                        ++comparedChannels;
                    }
                }
            }
            const double meanDifference = comparedChannels > 0
                ? static_cast<double>(totalDifference) / comparedChannels : 0.0;
            if (maxDifference > 4 || meanDifference > 0.75) {
                std::cerr << "[VulkanImageFilterTests] inner-shadow parity "
                             "max_difference="
                          << maxDifference << " mean_difference=" << meanDifference
                          << std::endl;
            }
            ok = expect(maxDifference <= 4 && meanDifference <= 0.75,
                        "Vulkan inner-shadow pixels drifted from Software") && ok;
        } else {
            ok = expect(false, "inner-shadow parity image sizes differ") && ok;
        }
    }

    {
        constexpr int width = 96;
        constexpr int height = 72;
        std::vector<unsigned char> vulkanPixels;
        std::vector<unsigned char> softwarePixels;
        ok = expect(renderCroppedClipScene(
                        wsc::Canvas::Backend::Vulkan, width, height, vulkanPixels),
                    "Vulkan cropped clip scene failed") && ok;
        ok = expect(renderCroppedClipScene(
                        wsc::Canvas::Backend::Software, width, height, softwarePixels),
                    "Software cropped clip scene failed") && ok;
        if (!vulkanPixels.empty()) {
            const auto *outside = pixelAt(vulkanPixels, width, 17, 10);
            const auto *gradientLeft = pixelAt(vulkanPixels, width, 24, 36);
            const auto *gradientRight = pixelAt(vulkanPixels, width, 44, 36);
            const auto *imageTopLeft = pixelAt(vulkanPixels, width, 54, 20);
            const auto *imageBottomRight = pixelAt(vulkanPixels, width, 70, 52);
            ok = expect(outside[0] < 20 && outside[1] < 20 && outside[2] < 20,
                        "rounded clip leaked outside its cropped layer") && ok;
            ok = expect(gradientLeft[0] > gradientLeft[1]
                            && gradientRight[1] > gradientRight[0],
                        "cropped clip shifted the gradient coordinates") && ok;
            if (!(imageTopLeft[0] > 220 && imageTopLeft[1] > 180)
                || !(imageBottomRight[0] < 40 && imageBottomRight[2] > 220)) {
                const auto *softwareTopLeft =
                    pixelAt(softwarePixels, width, 54, 20);
                const auto *softwareBottomRight =
                    pixelAt(softwarePixels, width, 70, 52);
                std::cerr << "[VulkanImageFilterTests] cropped image samples: top-left="
                          << static_cast<int>(imageTopLeft[0]) << ","
                          << static_cast<int>(imageTopLeft[1]) << ","
                          << static_cast<int>(imageTopLeft[2]) << " bottom-right="
                          << static_cast<int>(imageBottomRight[0]) << ","
                          << static_cast<int>(imageBottomRight[1]) << ","
                          << static_cast<int>(imageBottomRight[2]) << " software="
                          << static_cast<int>(softwareTopLeft[0]) << ","
                          << static_cast<int>(softwareTopLeft[1]) << ","
                          << static_cast<int>(softwareTopLeft[2]) << "/"
                          << static_cast<int>(softwareBottomRight[0]) << ","
                          << static_cast<int>(softwareBottomRight[1]) << ","
                          << static_cast<int>(softwareBottomRight[2]) << std::endl;
            }
            ok = expect(imageTopLeft[0] > 220 && imageTopLeft[1] > 180,
                        "cropped clip shifted the image's top-left quadrant") && ok;
            ok = expect(imageBottomRight[0] < 40 && imageBottomRight[2] > 220,
                        "cropped clip shifted the image's bottom-right quadrant") && ok;
        }
        ok = expect(vulkanPixels.size() == softwarePixels.size(),
                    "cropped clip parity image sizes differ") && ok;
    }

    if (ok) {
        std::cout << "[VulkanImageFilterTests] PASS: GPU Gaussian blur, inner shadow, "
                     "tile modes, color treatment, downsampling, and Canvas effects "
                     "verified on \""
                  << deviceName << "\"." << std::endl;
    }
    return ok ? 0 : 1;
}

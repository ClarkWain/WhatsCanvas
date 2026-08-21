#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "wsc/wsc.h"

#include "platforms/ios/WhatsCanvasDemo/DemoScene.h"
#include "platforms/shared/scenes/CanonicalViewport.h"

namespace {

constexpr int kWidth = 390;
constexpr int kHeight = 844;

struct Region
{
    const char *name;
    float x;
    float y;
    float width;
    float height;
};

struct RegionDiff
{
    double meanChannelError = 0.0;
    double divergentPixelRatio = 0.0;
};

RegionDiff compareRegion(const std::vector<unsigned char> &actual,
                         const std::vector<unsigned char> &reference,
                         const Region &region)
{
    const auto viewport = whatscanvas::scenes::makeCanonicalViewport(
        static_cast<float>(kWidth), static_cast<float>(kHeight),
        {47.0f, 34.0f, 0.0f, 0.0f});
    const int left = std::max(0, static_cast<int>(std::floor(
        viewport.offsetX + region.x * viewport.scale)));
    const int top = std::max(
        0, static_cast<int>(std::floor(
            viewport.offsetY + region.y * viewport.scale)));
    const int right = std::min(
        kWidth, static_cast<int>(std::ceil(
            viewport.offsetX + (region.x + region.width) * viewport.scale)));
    const int bottom = std::min(
        kHeight, static_cast<int>(std::ceil(
            viewport.offsetY + (region.y + region.height) * viewport.scale)));

    std::uint64_t absoluteError = 0;
    std::size_t divergentPixels = 0;
    std::size_t pixelCount = 0;
    for (int y = top; y < bottom; ++y) {
        for (int x = left; x < right; ++x) {
            const std::size_t pixel = static_cast<std::size_t>(y * kWidth + x);
            int maximumChannelError = 0;
            for (std::size_t channel = 0; channel < 4; ++channel) {
                const std::size_t index = pixel * 4u + channel;
                const int difference = std::abs(
                    static_cast<int>(actual[index])
                    - static_cast<int>(reference[index]));
                absoluteError += static_cast<std::uint64_t>(difference);
                maximumChannelError = std::max(maximumChannelError, difference);
            }
            divergentPixels += maximumChannelError > 32 ? 1u : 0u;
            ++pixelCount;
        }
    }

    return {
        static_cast<double>(absoluteError)
            / static_cast<double>(pixelCount * 4u),
        static_cast<double>(divergentPixels) / static_cast<double>(pixelCount)
    };
}

bool renderScene(wsc::Canvas::Backend backend,
                 std::vector<unsigned char> &pixels)
{
    auto canvas = wsc::Canvas::create(backend, kWidth, kHeight);
    if (!canvas || !canvas->initializeContext()) {
        return false;
    }
    canvas->setDevicePixelRatio(1.0f);
    if (!canvas->setTextBackend(wsc::Canvas::TextBackend::CoreText)) {
        return false;
    }

    std::unique_ptr<wsc::Image> checker;
    whatscanvas::demo::createCheckerImage(*canvas, checker);
    const auto staticScene = whatscanvas::demo::recordStaticScene(
        *canvas, static_cast<float>(kWidth), static_cast<float>(kHeight),
        47.0f, 34.0f, 0.0f, 0.0f);
    if (!staticScene) {
        return false;
    }

    canvas->beginFrame();
    canvas->drawPictureRasterized(*staticScene);
    whatscanvas::demo::drawDynamicScene(
        *canvas, checker.get(), static_cast<float>(kWidth),
        static_cast<float>(kHeight), 47.0f, 34.0f, 0.0f, 0.0f, 1.25f);
    canvas->endFrame();
    return canvas->readPixelsRGBA(pixels);
}

} // namespace

int main()
{
    if (!wsc::Canvas::isBackendAvailable(wsc::Canvas::Backend::Metal)) {
        std::cerr << "Metal backend is unavailable." << std::endl;
        return 1;
    }

    std::vector<unsigned char> metal;
    std::vector<unsigned char> software;
    if (!renderScene(wsc::Canvas::Backend::Metal, metal)
        || !renderScene(wsc::Canvas::Backend::Software, software)) {
        std::cerr << "Could not render the iOS demo parity scene." << std::endl;
        return 1;
    }
    if (metal.size() != software.size() || metal.empty()) {
        std::cerr << "Parity images have different sizes." << std::endl;
        return 1;
    }

    std::uint64_t absoluteError = 0;
    std::size_t divergentPixels = 0;
    const std::size_t pixelCount = metal.size() / 4u;
    for (std::size_t pixel = 0; pixel < pixelCount; ++pixel) {
        int maximumChannelError = 0;
        for (std::size_t channel = 0; channel < 4; ++channel) {
            const int difference = std::abs(
                static_cast<int>(metal[pixel * 4u + channel])
                - static_cast<int>(software[pixel * 4u + channel]));
            absoluteError += static_cast<std::uint64_t>(difference);
            maximumChannelError = std::max(maximumChannelError, difference);
        }
        if (maximumChannelError > 32) {
            ++divergentPixels;
        }
    }

    const double meanChannelError = static_cast<double>(absoluteError)
        / static_cast<double>(metal.size());
    const double divergentRatio = static_cast<double>(divergentPixels)
        / static_cast<double>(pixelCount);

    // Canonical portrait card content rectangles. Reporting every region keeps
    // a backend bug in one feature from being hidden by the full-screen mean.
    const Region regions[] = {
        {"text", 25.0f, 99.0f, 160.0f, 131.0f},
        {"path", 215.0f, 99.0f, 160.0f, 131.0f},
        {"clip", 25.0f, 275.0f, 160.0f, 131.0f},
        {"arc", 215.0f, 275.0f, 160.0f, 131.0f},
        {"transform", 25.0f, 451.0f, 160.0f, 131.0f},
        {"blend", 215.0f, 451.0f, 160.0f, 131.0f},
        {"image", 25.0f, 627.0f, 160.0f, 131.0f},
        {"motion", 215.0f, 627.0f, 160.0f, 131.0f}
    };
    bool regionsPassed = true;
    for (const Region &region : regions) {
        const RegionDiff diff = compareRegion(metal, software, region);
        std::cout << "iOS demo region " << region.name
                  << ": mean channel error " << diff.meanChannelError
                  << ", divergent pixel ratio " << diff.divergentPixelRatio
                  << std::endl;
        regionsPassed = regionsPassed
            && diff.meanChannelError < 4.0
            && diff.divergentPixelRatio < 0.04;
    }
    if (meanChannelError >= 8.0 || divergentRatio >= 0.08 || !regionsPassed) {
        std::cerr << "iOS demo Metal/Software parity drift: mean channel error "
                  << meanChannelError << ", divergent pixel ratio "
                  << divergentRatio << std::endl;
        return 1;
    }

    std::cout << "iOS demo Metal/Software parity: mean channel error "
              << meanChannelError << ", divergent pixel ratio "
              << divergentRatio << std::endl;
    return 0;
}

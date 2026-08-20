#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

#include "wsc/wsc.h"

#include "platforms/ios/WhatsCanvasDemo/DemoScene.h"

namespace {

constexpr int kWidth = 390;
constexpr int kHeight = 844;

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
    if (meanChannelError >= 8.0 || divergentRatio >= 0.08) {
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

#include <algorithm>
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

    if (ok) {
        std::cout << "[VulkanImageFilterTests] PASS: GPU Gaussian blur, tile modes, "
                     "color treatment, downsampling, and Canvas backdrop verified on \""
                  << deviceName << "\"." << std::endl;
    }
    return ok ? 0 : 1;
}

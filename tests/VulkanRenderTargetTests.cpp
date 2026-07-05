// M2 test: Vulkan offscreen render target creation + solid fill + pixel readback.
//
// Only built when configured with -DWHATSCANVAS_ENABLE_VULKAN=ON and a Vulkan
// SDK is present. Validates the full M1/M2 pipeline: command pool, single-time
// submission, offscreen VkImage + memory + render pass + framebuffer, a solid
// device clear, and an image->staging->host readback.

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "render/IRenderTarget.h"
#include "render/IRenderer.h"
#include "render/vulkan/VulkanRenderDevice.h"

namespace {

bool checkSolidColor(const std::vector<unsigned char> &pixels, int width, int height, std::uint8_t r, std::uint8_t g,
                     std::uint8_t b, std::uint8_t a)
{
    const std::size_t expected = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    if (pixels.size() != expected) {
        std::cerr << "[VulkanRenderTargetTests] FAIL: readback size " << pixels.size() << " != expected " << expected
                  << "." << std::endl;
        return false;
    }
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
        if (pixels[i] != r || pixels[i + 1] != g || pixels[i + 2] != b || pixels[i + 3] != a) {
            std::cerr << "[VulkanRenderTargetTests] FAIL: pixel " << (i / 4) << " = (" << int(pixels[i]) << ","
                      << int(pixels[i + 1]) << "," << int(pixels[i + 2]) << "," << int(pixels[i + 3])
                      << "), expected (" << int(r) << "," << int(g) << "," << int(b) << "," << int(a) << ")."
                      << std::endl;
            return false;
        }
    }
    return true;
}

} // namespace

int main()
{
    if (!VulkanRenderDevice::isAvailable()) {
        std::cerr << "[VulkanRenderTargetTests] FAIL: Vulkan support was not compiled into the library." << std::endl;
        return 1;
    }

    VulkanRenderDevice device;
    device.initializeBackend();
    if (!device.isDeviceReady()) {
        std::cerr << "[VulkanRenderTargetTests] FAIL: Vulkan device was not created." << std::endl;
        return 1;
    }

    const int width = 64;
    const int height = 48;

    auto target = device.createRenderTarget(width, height);
    if (!target || !target->isValid()) {
        std::cerr << "[VulkanRenderTargetTests] FAIL: could not create a valid render target." << std::endl;
        return 1;
    }
    if (target->width() != width || target->height() != height) {
        std::cerr << "[VulkanRenderTargetTests] FAIL: render target size mismatch." << std::endl;
        return 1;
    }
    if (device.resourceStats().renderTargetCount != 1) {
        std::cerr << "[VulkanRenderTargetTests] FAIL: renderTargetCount != 1." << std::endl;
        return 1;
    }

    // 1) Solid non-zero fill, then read it back exactly.
    const std::uint8_t r = 12, g = 200, b = 56, a = 255;
    if (!device.fillRenderTargetSolid(target, r, g, b, a)) {
        std::cerr << "[VulkanRenderTargetTests] FAIL: fillRenderTargetSolid returned false." << std::endl;
        return 1;
    }
    std::vector<unsigned char> pixels;
    if (!device.readPixelsRGBA(width, height, pixels)) {
        std::cerr << "[VulkanRenderTargetTests] FAIL: readPixelsRGBA returned false after solid fill." << std::endl;
        return 1;
    }
    if (!checkSolidColor(pixels, width, height, r, g, b, a)) {
        return 1;
    }

    // 2) Render-pass clear-to-zero via activate(), then read back all zeros.
    OffscreenRenderRequest request;
    request.canvasWidth = width;
    request.canvasHeight = height;
    request.targetWidth = width;
    request.targetHeight = height;
    target->begin(request);
    target->activate();
    if (!target->isActivated()) {
        std::cerr << "[VulkanRenderTargetTests] FAIL: render target did not activate." << std::endl;
        return 1;
    }
    target->end();

    std::vector<unsigned char> zeroPixels;
    if (!device.readPixelsRGBA(width, height, zeroPixels)) {
        std::cerr << "[VulkanRenderTargetTests] FAIL: readPixelsRGBA returned false after clear." << std::endl;
        return 1;
    }
    if (!checkSolidColor(zeroPixels, width, height, 0, 0, 0, 0)) {
        return 1;
    }

    const std::string deviceName = device.selectedDeviceName();
    auto survivorTarget = device.createRenderTarget(8, 8);
    if (!survivorTarget || !survivorTarget->isValid()) {
        std::cerr << "[VulkanRenderTargetTests] FAIL: could not create survivor render target." << std::endl;
        return 1;
    }
    device.finalizeBackend();
    survivorTarget.reset();

    std::cout << "[VulkanRenderTargetTests] PASS: render target + readback verified on \"" << deviceName << "\"."
              << std::endl;

    target.reset();
    return 0;
}

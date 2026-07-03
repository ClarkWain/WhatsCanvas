// M3 test: Vulkan solid-color geometry through a real graphics pipeline.
//
// Only built when configured with -DWHATSCANVAS_ENABLE_VULKAN=ON and a Vulkan
// SDK is present. Renders a solid triangle into an offscreen render target and
// verifies (via readback) that an interior pixel is the triangle color while a
// corner pixel remains the render-pass clear color.

#include <cstdint>
#include <iostream>
#include <vector>

#include "render/IRenderTarget.h"
#include "render/IRenderer.h"
#include "render/vulkan/VulkanRenderDevice.h"

namespace {

bool pixelEquals(const std::vector<unsigned char> &pixels, int width, int x, int y, std::uint8_t r, std::uint8_t g,
                 std::uint8_t b, std::uint8_t a)
{
    const std::size_t idx = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + x) * 4u;
    if (idx + 3 >= pixels.size()) {
        return false;
    }
    return pixels[idx] == r && pixels[idx + 1] == g && pixels[idx + 2] == b && pixels[idx + 3] == a;
}

} // namespace

int main()
{
    if (!VulkanRenderDevice::isAvailable()) {
        std::cerr << "[VulkanSolidGeometryTests] FAIL: Vulkan support was not compiled in." << std::endl;
        return 1;
    }

    VulkanRenderDevice device;
    device.initializeBackend();
    if (!device.isDeviceReady()) {
        std::cerr << "[VulkanSolidGeometryTests] FAIL: Vulkan device was not created." << std::endl;
        return 1;
    }

    const int width = 64;
    const int height = 48;
    auto target = device.createRenderTarget(width, height);
    if (!target || !target->isValid()) {
        std::cerr << "[VulkanSolidGeometryTests] FAIL: could not create render target." << std::endl;
        return 1;
    }

    // A large triangle covering the center (NDC) but not the top-left corner.
    const std::vector<float> triangle = {
        -0.9f, -0.9f,
         0.9f, -0.9f,
         0.0f,  0.9f,
    };
    if (!device.renderSolidTriangles(target, triangle, 1.0f, 0.0f, 0.0f, 1.0f)) {
        std::cerr << "[VulkanSolidGeometryTests] FAIL: renderSolidTriangles returned false." << std::endl;
        return 1;
    }

    std::vector<unsigned char> pixels;
    if (!device.readPixelsRGBA(width, height, pixels)) {
        std::cerr << "[VulkanSolidGeometryTests] FAIL: readPixelsRGBA returned false." << std::endl;
        return 1;
    }

    // Center pixel should be the triangle color (opaque red).
    if (!pixelEquals(pixels, width, width / 2, height / 2, 255, 0, 0, 255)) {
        const std::size_t idx = (static_cast<std::size_t>(height / 2) * width + width / 2) * 4u;
        std::cerr << "[VulkanSolidGeometryTests] FAIL: center pixel = (" << int(pixels[idx]) << ","
                  << int(pixels[idx + 1]) << "," << int(pixels[idx + 2]) << "," << int(pixels[idx + 3])
                  << "), expected (255,0,0,255)." << std::endl;
        return 1;
    }

    // Top-left corner should remain the clear color (transparent black).
    if (!pixelEquals(pixels, width, 0, 0, 0, 0, 0, 0)) {
        const std::size_t idx = 0;
        std::cerr << "[VulkanSolidGeometryTests] FAIL: corner pixel = (" << int(pixels[idx]) << ","
                  << int(pixels[idx + 1]) << "," << int(pixels[idx + 2]) << "," << int(pixels[idx + 3])
                  << "), expected (0,0,0,0)." << std::endl;
        return 1;
    }

    std::cout << "[VulkanSolidGeometryTests] PASS: solid triangle rasterized on \"" << device.selectedDeviceName()
              << "\"." << std::endl;

    target.reset();
    device.finalizeBackend();
    return 0;
}

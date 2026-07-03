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

    // Filled quad (two triangles) covering the whole target in green.
    const std::vector<float> quad = {
        -1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f,
        -1.0f, -1.0f, 1.0f,  1.0f, -1.0f, 1.0f,
    };
    if (!device.renderSolidTriangles(target, quad, 0.0f, 1.0f, 0.0f, 1.0f)) {
        std::cerr << "[VulkanSolidGeometryTests] FAIL: quad render returned false." << std::endl;
        return 1;
    }
    if (!device.readPixelsRGBA(width, height, pixels)) {
        std::cerr << "[VulkanSolidGeometryTests] FAIL: readback after quad failed." << std::endl;
        return 1;
    }
    if (!pixelEquals(pixels, width, 0, 0, 0, 255, 0, 255)
        || !pixelEquals(pixels, width, width - 1, height - 1, 0, 255, 0, 255)) {
        std::cerr << "[VulkanSolidGeometryTests] FAIL: full-target quad did not fill with green." << std::endl;
        return 1;
    }

    // Points topology: a single point at the center in blue.
    const std::vector<float> point = {0.0f, 0.0f};
    if (!device.renderSolidPrimitives(target, VulkanRenderDevice::SolidTopology::Points, point, 0.0f, 0.0f, 1.0f,
                                      1.0f)) {
        std::cerr << "[VulkanSolidGeometryTests] FAIL: point render returned false." << std::endl;
        return 1;
    }
    if (!device.readPixelsRGBA(width, height, pixels)) {
        std::cerr << "[VulkanSolidGeometryTests] FAIL: readback after point failed." << std::endl;
        return 1;
    }
    // A single 1px point lands near the center; its exact pixel depends on
    // rasterization rounding, so scan a small neighborhood for the blue pixel.
    int blueCount = 0;
    bool blueNearCenter = false;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (pixelEquals(pixels, width, x, y, 0, 0, 255, 255)) {
                ++blueCount;
                if (x >= width / 2 - 1 && x <= width / 2 + 1 && y >= height / 2 - 1 && y <= height / 2 + 1) {
                    blueNearCenter = true;
                }
            }
        }
    }
    if (blueCount != 1 || !blueNearCenter) {
        std::cerr << "[VulkanSolidGeometryTests] FAIL: expected exactly one blue point near center, found "
                  << blueCount << (blueNearCenter ? " (near center)" : " (not near center)") << "." << std::endl;
        return 1;
    }

    std::cout << "[VulkanSolidGeometryTests] PASS: triangle, quad, and point rasterized on \""
              << device.selectedDeviceName() << "\"." << std::endl;

    target.reset();
    device.finalizeBackend();
    return 0;
}

// M6 test: Vulkan offscreen-layer compositing (saveLayer composite-back).
// Only built with -DWHATSCANVAS_ENABLE_VULKAN=ON.

#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

#include "render/IRenderTarget.h"
#include "render/IRenderer.h"
#include "render/vulkan/VulkanRenderDevice.h"

namespace {

bool pixelNear(const std::vector<unsigned char> &pixels, int width, int x, int y, int r, int g, int b, int a,
               int tol, const char *label)
{
    const std::size_t idx = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + x) * 4u;
    if (idx + 3 >= pixels.size()) {
        std::cerr << "[VulkanLayerTests] FAIL: " << label << " out of range." << std::endl;
        return false;
    }
    const int pr = pixels[idx], pg = pixels[idx + 1], pb = pixels[idx + 2], pa = pixels[idx + 3];
    if (std::abs(pr - r) > tol || std::abs(pg - g) > tol || std::abs(pb - b) > tol || std::abs(pa - a) > tol) {
        std::cerr << "[VulkanLayerTests] FAIL: " << label << " = (" << pr << "," << pg << "," << pb << "," << pa
                  << "), expected ~(" << r << "," << g << "," << b << "," << a << ") +/-" << tol << "." << std::endl;
        return false;
    }
    return true;
}

} // namespace

int main()
{
    if (!VulkanRenderDevice::isAvailable()) {
        std::cerr << "[VulkanLayerTests] FAIL: Vulkan support was not compiled in." << std::endl;
        return 1;
    }

    VulkanRenderDevice device;
    device.initializeBackend();
    if (!device.isDeviceReady()) {
        std::cerr << "[VulkanLayerTests] FAIL: Vulkan device was not created." << std::endl;
        return 1;
    }

    const int width = 64;
    const int height = 48;
    auto layer = device.createRenderTarget(width, height);
    auto dst = device.createRenderTarget(width, height);
    if (!layer || !dst || !layer->isValid() || !dst->isValid()) {
        std::cerr << "[VulkanLayerTests] FAIL: could not create render targets." << std::endl;
        return 1;
    }

    // Render an opaque red layer (full-target quad) into the offscreen layer.
    const std::vector<float> quad = {
        -1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f,
        -1.0f, -1.0f, 1.0f,  1.0f, -1.0f, 1.0f,
    };
    if (!device.renderSolidTriangles(layer, quad, 1.0f, 0.0f, 0.0f, 1.0f)) {
        std::cerr << "[VulkanLayerTests] FAIL: could not render layer content." << std::endl;
        return 1;
    }

    // Composite the red layer at 50% over an opaque blue background -> purple.
    if (!device.compositeLayer(dst, layer, 0.0f, 0.0f, 1.0f, 1.0f, 0.5f)) {
        std::cerr << "[VulkanLayerTests] FAIL: compositeLayer returned false." << std::endl;
        return 1;
    }

    std::vector<unsigned char> pixels;
    if (!device.readPixelsRGBA(width, height, pixels)) {
        std::cerr << "[VulkanLayerTests] FAIL: readback failed." << std::endl;
        return 1;
    }
    // red*0.5 + blue*0.5 = (128,0,128,255).
    if (!pixelNear(pixels, width, width / 2, height / 2, 128, 0, 128, 255, 3, "composite center")) return 1;
    if (!pixelNear(pixels, width, 4, 4, 128, 0, 128, 255, 3, "composite corner")) return 1;

    std::cout << "[VulkanLayerTests] PASS: offscreen layer composited on \"" << device.selectedDeviceName() << "\"."
              << std::endl;

    dst.reset();
    layer.reset();
    device.finalizeBackend();
    return 0;
}

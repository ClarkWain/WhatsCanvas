// M4 test: Vulkan Paint features - per-vertex gradient interpolation and
// fixed-function blend modes. Only built with -DWHATSCANVAS_ENABLE_VULKAN=ON.

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
        std::cerr << "[VulkanPaintTests] FAIL: " << label << " out of range." << std::endl;
        return false;
    }
    const int pr = pixels[idx], pg = pixels[idx + 1], pb = pixels[idx + 2], pa = pixels[idx + 3];
    if (std::abs(pr - r) > tol || std::abs(pg - g) > tol || std::abs(pb - b) > tol || std::abs(pa - a) > tol) {
        std::cerr << "[VulkanPaintTests] FAIL: " << label << " = (" << pr << "," << pg << "," << pb << "," << pa
                  << "), expected ~(" << r << "," << g << "," << b << "," << a << ") +/-" << tol << "." << std::endl;
        return false;
    }
    return true;
}

} // namespace

int main()
{
    if (!VulkanRenderDevice::isAvailable()) {
        std::cerr << "[VulkanPaintTests] FAIL: Vulkan support was not compiled in." << std::endl;
        return 1;
    }

    VulkanRenderDevice device;
    device.initializeBackend();
    if (!device.isDeviceReady()) {
        std::cerr << "[VulkanPaintTests] FAIL: Vulkan device was not created." << std::endl;
        return 1;
    }

    const int width = 64;
    const int height = 48;
    auto target = device.createRenderTarget(width, height);
    if (!target || !target->isValid()) {
        std::cerr << "[VulkanPaintTests] FAIL: could not create render target." << std::endl;
        return 1;
    }

    // 1) Horizontal gradient: left red -> right blue over a full-target quad.
    // Quad vertex order matches fullTargetQuad(): (-1,-1),(1,-1),(1,1),(-1,-1),(1,1),(-1,1).
    const std::vector<float> quad = {
        -1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f,
        -1.0f, -1.0f, 1.0f,  1.0f, -1.0f, 1.0f,
    };
    const float red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    const float blue[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    auto colorFor = [&](float ndcX) -> const float * { return ndcX < 0.0f ? red : blue; };
    std::vector<float> colors;
    for (std::size_t v = 0; v < quad.size() / 2; ++v) {
        const float *c = colorFor(quad[v * 2]);
        colors.insert(colors.end(), c, c + 4);
    }
    if (!device.renderGradientTriangles(target, quad, colors)) {
        std::cerr << "[VulkanPaintTests] FAIL: renderGradientTriangles returned false." << std::endl;
        return 1;
    }
    std::vector<unsigned char> pixels;
    if (!device.readPixelsRGBA(width, height, pixels)) {
        std::cerr << "[VulkanPaintTests] FAIL: gradient readback failed." << std::endl;
        return 1;
    }
    if (!pixelNear(pixels, width, 0, height / 2, 255, 0, 0, 255, 8, "gradient left")) return 1;
    if (!pixelNear(pixels, width, width - 1, height / 2, 0, 0, 255, 255, 8, "gradient right")) return 1;
    if (!pixelNear(pixels, width, width / 2, height / 2, 128, 0, 128, 255, 8, "gradient center")) return 1;

    // 2) SrcOver blend: red @50% over opaque blue -> purple.
    if (!device.renderBlendedOverlay(target, VulkanRenderDevice::SolidBlendMode::SrcOver, 0.0f, 0.0f, 1.0f, 1.0f,
                                     1.0f, 0.0f, 0.0f, 0.5f)) {
        std::cerr << "[VulkanPaintTests] FAIL: SrcOver overlay returned false." << std::endl;
        return 1;
    }
    if (!device.readPixelsRGBA(width, height, pixels)) {
        std::cerr << "[VulkanPaintTests] FAIL: SrcOver readback failed." << std::endl;
        return 1;
    }
    if (!pixelNear(pixels, width, width / 2, height / 2, 128, 0, 128, 255, 3, "SrcOver center")) return 1;

    // 3) Additive blend: white @50% (additive) over gray -> brighter gray.
    if (!device.renderBlendedOverlay(target, VulkanRenderDevice::SolidBlendMode::Add, 0.2f, 0.2f, 0.2f, 1.0f, 1.0f,
                                     1.0f, 1.0f, 0.5f)) {
        std::cerr << "[VulkanPaintTests] FAIL: Add overlay returned false." << std::endl;
        return 1;
    }
    if (!device.readPixelsRGBA(width, height, pixels)) {
        std::cerr << "[VulkanPaintTests] FAIL: Add readback failed." << std::endl;
        return 1;
    }
    // 0.2 + 1.0*0.5 = 0.7 -> ~178.
    if (!pixelNear(pixels, width, width / 2, height / 2, 178, 178, 178, 255, 3, "Add center")) return 1;

    std::cout << "[VulkanPaintTests] PASS: gradient + SrcOver + Add verified on \"" << device.selectedDeviceName()
              << "\"." << std::endl;

    target.reset();
    device.finalizeBackend();
    return 0;
}

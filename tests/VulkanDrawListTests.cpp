// ADR-006 first slice test: execute a backend-neutral DrawList on the Vulkan
// backend. Only built with -DWHATSCANVAS_ENABLE_VULKAN=ON.

#include <cstdint>
#include <iostream>
#include <vector>

#include "render/DrawList.h"
#include "render/IRenderTarget.h"
#include "render/IRenderer.h"
#include "render/vulkan/VulkanRenderDevice.h"

namespace {

bool pixelIs(const std::vector<unsigned char> &pixels, int width, int x, int y, int r, int g, int b, int a,
             const char *label)
{
    const std::size_t idx = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + x) * 4u;
    if (idx + 3 >= pixels.size()) {
        std::cerr << "[VulkanDrawListTests] FAIL: " << label << " out of range." << std::endl;
        return false;
    }
    if (pixels[idx] != r || pixels[idx + 1] != g || pixels[idx + 2] != b || pixels[idx + 3] != a) {
        std::cerr << "[VulkanDrawListTests] FAIL: " << label << " = (" << int(pixels[idx]) << ","
                  << int(pixels[idx + 1]) << "," << int(pixels[idx + 2]) << "," << int(pixels[idx + 3])
                  << "), expected (" << r << "," << g << "," << b << "," << a << ")." << std::endl;
        return false;
    }
    return true;
}

} // namespace

int main()
{
    if (!VulkanRenderDevice::isAvailable()) {
        std::cerr << "[VulkanDrawListTests] FAIL: Vulkan support was not compiled in." << std::endl;
        return 1;
    }

    VulkanRenderDevice device;
    device.initializeBackend();
    if (!device.isDeviceReady()) {
        std::cerr << "[VulkanDrawListTests] FAIL: Vulkan device was not created." << std::endl;
        return 1;
    }

    const int width = 64;
    const int height = 48;
    auto target = device.createRenderTarget(width, height);
    if (!target || !target->isValid()) {
        std::cerr << "[VulkanDrawListTests] FAIL: could not create render target." << std::endl;
        return 1;
    }

    // Two primitives in one list: a green full-target background, then a red
    // triangle over it.
    wsc::DrawList drawList;

    wsc::DrawPrimitive background;
    background.kind = wsc::DrawPrimitiveKind::SolidTriangles;
    background.blendMode = 0; // SrcOver
    background.positions = {
        -1.0f, -1.0f, 1.0f, -1.0f, 1.0f, 1.0f,
        -1.0f, -1.0f, 1.0f,  1.0f, -1.0f, 1.0f,
    };
    background.color[0] = 0.0f;
    background.color[1] = 1.0f;
    background.color[2] = 0.0f;
    background.color[3] = 1.0f;
    drawList.push_back(background);

    wsc::DrawPrimitive triangle;
    triangle.kind = wsc::DrawPrimitiveKind::SolidTriangles;
    triangle.blendMode = 0;
    triangle.positions = {-0.9f, -0.9f, 0.9f, -0.9f, 0.0f, 0.9f};
    triangle.color[0] = 1.0f;
    triangle.color[1] = 0.0f;
    triangle.color[2] = 0.0f;
    triangle.color[3] = 1.0f;
    drawList.push_back(triangle);

    if (!device.executeDrawList(target, drawList)) {
        std::cerr << "[VulkanDrawListTests] FAIL: executeDrawList returned false." << std::endl;
        return 1;
    }

    std::vector<unsigned char> pixels;
    if (!device.readPixelsRGBA(width, height, pixels)) {
        std::cerr << "[VulkanDrawListTests] FAIL: readback failed." << std::endl;
        return 1;
    }
    // Center is inside the triangle -> red; top-left corner -> green background.
    if (!pixelIs(pixels, width, width / 2, height / 2, 255, 0, 0, 255, "center red")) return 1;
    if (!pixelIs(pixels, width, 0, 0, 0, 255, 0, 255, "corner green")) return 1;

    std::cout << "[VulkanDrawListTests] PASS: backend-neutral DrawList executed on \"" << device.selectedDeviceName()
              << "\"." << std::endl;

    target.reset();
    device.finalizeBackend();
    return 0;
}

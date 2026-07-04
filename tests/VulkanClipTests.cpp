// M7 test: Vulkan coverage-mask clipping. Only built with
// -DWHATSCANVAS_ENABLE_VULKAN=ON.

#include <cstdint>
#include <iostream>
#include <vector>

#include "render/IRenderTarget.h"
#include "render/IRenderer.h"
#include "render/RenderTypes.h"
#include "render/vulkan/VulkanRenderDevice.h"

namespace {

bool pixelIs(const std::vector<unsigned char> &pixels, int width, int x, int y, int r, int g, int b, int a,
             const char *label)
{
    const std::size_t idx = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + x) * 4u;
    if (idx + 3 >= pixels.size()) {
        std::cerr << "[VulkanClipTests] FAIL: " << label << " out of range." << std::endl;
        return false;
    }
    if (pixels[idx] != r || pixels[idx + 1] != g || pixels[idx + 2] != b || pixels[idx + 3] != a) {
        std::cerr << "[VulkanClipTests] FAIL: " << label << " = (" << int(pixels[idx]) << "," << int(pixels[idx + 1])
                  << "," << int(pixels[idx + 2]) << "," << int(pixels[idx + 3]) << "), expected (" << r << "," << g
                  << "," << b << "," << a << ")." << std::endl;
        return false;
    }
    return true;
}

} // namespace

int main()
{
    if (!VulkanRenderDevice::isAvailable()) {
        std::cerr << "[VulkanClipTests] FAIL: Vulkan support was not compiled in." << std::endl;
        return 1;
    }

    VulkanRenderDevice device;
    device.initializeBackend();
    if (!device.isDeviceReady()) {
        std::cerr << "[VulkanClipTests] FAIL: Vulkan device was not created." << std::endl;
        return 1;
    }

    // createClipMaskResource should now return a valid resource.
    ClipMaskPath path;
    path.points = {-0.9f, -0.9f, 0.9f, -0.9f, 0.0f, 0.9f};
    path.coverage = {1.0f, 1.0f, 1.0f};
    auto clipResource = device.createClipMaskResource(path);
    if (!clipResource || !clipResource->isValid()) {
        std::cerr << "[VulkanClipTests] FAIL: createClipMaskResource did not return a valid resource." << std::endl;
        return 1;
    }

    const int width = 64;
    const int height = 48;
    auto maskTarget = device.createRenderTarget(width, height);
    auto dst = device.createRenderTarget(width, height);
    if (!maskTarget || !dst || !maskTarget->isValid() || !dst->isValid()) {
        std::cerr << "[VulkanClipTests] FAIL: could not create render targets." << std::endl;
        return 1;
    }

    // Rasterize a white triangle into the mask (coverage 1 inside).
    const std::vector<float> triangle = {
        -0.9f, -0.9f,
         0.9f, -0.9f,
         0.0f,  0.9f,
    };
    if (!device.renderSolidTriangles(maskTarget, triangle, 1.0f, 1.0f, 1.0f, 1.0f)) {
        std::cerr << "[VulkanClipTests] FAIL: could not rasterize clip mask." << std::endl;
        return 1;
    }

    // Fill the destination with green, clipped by the triangle mask.
    if (!device.renderClippedSolid(dst, maskTarget, 0.0f, 1.0f, 0.0f, 1.0f)) {
        std::cerr << "[VulkanClipTests] FAIL: renderClippedSolid returned false." << std::endl;
        return 1;
    }

    std::vector<unsigned char> pixels;
    if (!device.readPixelsRGBA(width, height, pixels)) {
        std::cerr << "[VulkanClipTests] FAIL: readback failed." << std::endl;
        return 1;
    }
    // Center is inside the triangle -> green; top-left corner is outside -> clear.
    if (!pixelIs(pixels, width, width / 2, height / 2, 0, 255, 0, 255, "clipped center green")) return 1;
    if (!pixelIs(pixels, width, 0, 0, 0, 0, 0, 0, "clipped corner clear")) return 1;

    std::cout << "[VulkanClipTests] PASS: coverage-mask clip verified on \"" << device.selectedDeviceName() << "\"."
              << std::endl;

    dst.reset();
    maskTarget.reset();
    device.finalizeBackend();
    return 0;
}

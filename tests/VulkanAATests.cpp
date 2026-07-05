// Analytic-AA coverage test: per-vertex coverage modulates the fill alpha (the
// mechanism the Canvas AA feather band uses). Only built with
// -DWHATSCANVAS_ENABLE_VULKAN=ON.

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

#include "command/DrawCommand.h"
#include "render/IRenderTarget.h"
#include "render/IRenderer.h"
#include "render/vulkan/VulkanRenderDevice.h"

namespace {

bool pixelNear(const std::vector<unsigned char> &pixels, int width, int x, int y, int r, int g, int b, int a,
               int tol, const char *label)
{
    const std::size_t idx = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + x) * 4u;
    if (idx + 3 >= pixels.size()) {
        std::cerr << "[VulkanAATests] FAIL: " << label << " out of range." << std::endl;
        return false;
    }
    const int pr = pixels[idx], pg = pixels[idx + 1], pb = pixels[idx + 2], pa = pixels[idx + 3];
    if (std::abs(pr - r) > tol || std::abs(pg - g) > tol || std::abs(pb - b) > tol || std::abs(pa - a) > tol) {
        std::cerr << "[VulkanAATests] FAIL: " << label << " = (" << pr << "," << pg << "," << pb << "," << pa
                  << "), expected ~(" << r << "," << g << "," << b << "," << a << ") +/-" << tol << "." << std::endl;
        return false;
    }
    return true;
}

} // namespace

int main()
{
    if (!VulkanRenderDevice::isAvailable()) {
        std::cerr << "[VulkanAATests] FAIL: Vulkan support was not compiled in." << std::endl;
        return 1;
    }

    VulkanRenderDevice device;
    device.initializeBackend();
    if (!device.isDeviceReady()) {
        std::cerr << "[VulkanAATests] FAIL: Vulkan device was not created." << std::endl;
        return 1;
    }

    const int width = 32;
    const int height = 24;
    auto target = device.createRenderTarget(width, height);
    if (!target || !target->isValid()) {
        std::cerr << "[VulkanAATests] FAIL: could not create render target." << std::endl;
        return 1;
    }

    OffscreenRenderRequest request;
    request.canvasWidth = width;
    request.canvasHeight = height;
    request.targetWidth = width;
    request.targetHeight = height;

    // A full-canvas red fill with uniform 0.5 coverage -> alpha halved.
    DrawPathData d;
    d.points = {
        0.0f, 0.0f, static_cast<float>(width), 0.0f, static_cast<float>(width), static_cast<float>(height),
        0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, static_cast<float>(height),
    };
    d.color[0] = 1.0f;
    d.color[1] = 0.0f;
    d.color[2] = 0.0f;
    d.color[3] = 1.0f;
    d.drawMode = PathDrawMode::Fill;
    d.capStyle = PathCapStyle::Round;
    d.coverage = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};

    std::vector<std::unique_ptr<Command>> commands;
    commands.push_back(std::make_unique<DrawPathCommand>(d));
    if (!device.executeCommands(target, commands, request)) {
        std::cerr << "[VulkanAATests] FAIL: executeCommands returned false." << std::endl;
        return 1;
    }
    std::vector<unsigned char> pixels;
    if (!device.readPixelsRGBA(width, height, pixels)) {
        return 1;
    }
    // Premultiplied SrcOver over transparent black: (0.5,0,0,0.5) -> ~(128,0,0,128).
    if (!pixelNear(pixels, width, width / 2, height / 2, 128, 0, 0, 128, 3, "coverage half alpha")) return 1;

    std::cout << "[VulkanAATests] PASS: analytic-AA coverage on \"" << device.selectedDeviceName() << "\"."
              << std::endl;

    commands.clear();
    target.reset();
    device.finalizeBackend();
    return 0;
}

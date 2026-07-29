// Fragment shader gradient translation test. Only built with
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
        std::cerr << "[VulkanGradientTests] FAIL: " << label << " out of range." << std::endl;
        return false;
    }
    const int pr = pixels[idx], pg = pixels[idx + 1], pb = pixels[idx + 2], pa = pixels[idx + 3];
    if (std::abs(pr - r) > tol || std::abs(pg - g) > tol || std::abs(pb - b) > tol || std::abs(pa - a) > tol) {
        std::cerr << "[VulkanGradientTests] FAIL: " << label << " = (" << pr << "," << pg << "," << pb << "," << pa
                  << "), expected ~(" << r << "," << g << "," << b << "," << a << ") +/-" << tol << "." << std::endl;
        return false;
    }
    return true;
}

} // namespace

int main()
{
    if (!VulkanRenderDevice::isAvailable()) {
        std::cerr << "[VulkanGradientTests] FAIL: Vulkan support was not compiled in." << std::endl;
        return 1;
    }

    VulkanRenderDevice device;
    device.initializeBackend();
    if (!device.isDeviceReady()) {
        std::cerr << "[VulkanGradientTests] FAIL: Vulkan device was not created." << std::endl;
        return 1;
    }

    const int width = 64;
    const int height = 48;
    auto target = device.createRenderTarget(width, height);
    if (!target || !target->isValid()) {
        std::cerr << "[VulkanGradientTests] FAIL: could not create render target." << std::endl;
        return 1;
    }

    OffscreenRenderRequest request;
    request.canvasWidth = width;
    request.canvasHeight = height;
    request.targetWidth = width;
    request.targetHeight = height;

    // A horizontal linear gradient (red -> blue) across a full-canvas quad.
    DrawPathData d;
    d.points = {
        0.0f, 0.0f, static_cast<float>(width), 0.0f, static_cast<float>(width), static_cast<float>(height),
        0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, static_cast<float>(height),
    };
    d.drawMode = PathDrawMode::Fill;
    d.capStyle = PathCapStyle::Round;
    d.gradientType = DrawGradientType::Linear;
    d.gradientTileMode = DrawGradientTileMode::Clamp;
    d.gradientStart[0] = 0.0f;
    d.gradientStart[1] = 0.0f;
    d.gradientEnd[0] = static_cast<float>(width);
    d.gradientEnd[1] = 0.0f;
    d.gradientStopCount = 2;
    DrawPathGradientStops &stops =
        d.writableGradientStops();
    stops.positions[0] = 0.0f;
    stops.positions[1] = 1.0f;
    stops.colors[0] = 1.0f; // red
    stops.colors[3] = 1.0f;
    stops.colors[6] = 1.0f; // blue (index 4..7 -> b at 6)
    stops.colors[7] = 1.0f;

    std::vector<std::unique_ptr<Command>> commands;
    commands.push_back(std::make_unique<DrawPathCommand>(d));
    if (!device.executeCommands(target, commands, request)) {
        std::cerr << "[VulkanGradientTests] FAIL: executeCommands returned false." << std::endl;
        return 1;
    }
    std::vector<unsigned char> pixels;
    if (!device.readPixelsRGBA(width, height, pixels)) {
        return 1;
    }
    if (!pixelNear(pixels, width, 1, height / 2, 255, 0, 0, 255, 10, "linear left red")) return 1;
    if (!pixelNear(pixels, width, width - 2, height / 2, 0, 0, 255, 255, 10, "linear right blue")) return 1;
    if (!pixelNear(pixels, width, width / 2, height / 2, 128, 0, 128, 255, 12, "linear center purple")) return 1;

    std::cout << "[VulkanGradientTests] PASS: fragment linear gradient on \"" << device.selectedDeviceName() << "\"."
              << std::endl;

    commands.clear();
    target.reset();
    device.finalizeBackend();
    return 0;
}

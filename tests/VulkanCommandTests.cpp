// ADR-006 command-translation slice: render a real WhatsCanvas Command stream on
// Vulkan. Only built with -DWHATSCANVAS_ENABLE_VULKAN=ON.

#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

#include "command/DrawCommand.h"
#include "render/IRenderTarget.h"
#include "render/IRenderer.h"
#include "render/vulkan/VulkanRenderDevice.h"

namespace {

bool pixelIs(const std::vector<unsigned char> &pixels, int width, int x, int y, int r, int g, int b, int a,
             const char *label)
{
    const std::size_t idx = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + x) * 4u;
    if (idx + 3 >= pixels.size()) {
        std::cerr << "[VulkanCommandTests] FAIL: " << label << " out of range." << std::endl;
        return false;
    }
    if (pixels[idx] != r || pixels[idx + 1] != g || pixels[idx + 2] != b || pixels[idx + 3] != a) {
        std::cerr << "[VulkanCommandTests] FAIL: " << label << " = (" << int(pixels[idx]) << ","
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
        std::cerr << "[VulkanCommandTests] FAIL: Vulkan support was not compiled in." << std::endl;
        return 1;
    }

    VulkanRenderDevice device;
    device.initializeBackend();
    if (!device.isDeviceReady()) {
        std::cerr << "[VulkanCommandTests] FAIL: Vulkan device was not created." << std::endl;
        return 1;
    }

    const int width = 64;
    const int height = 48;
    auto target = device.createRenderTarget(width, height);
    if (!target || !target->isValid()) {
        std::cerr << "[VulkanCommandTests] FAIL: could not create render target." << std::endl;
        return 1;
    }

    // Build a real DrawPathCommand: a filled triangle (canvas-space, triangle
    // list) covering the center, in red.
    DrawPathData pathData;
    pathData.points = {5.0f, 5.0f, 59.0f, 5.0f, 32.0f, 43.0f};
    pathData.color[0] = 1.0f;
    pathData.color[1] = 0.0f;
    pathData.color[2] = 0.0f;
    pathData.color[3] = 1.0f;
    pathData.drawMode = PathDrawMode::Fill;
    pathData.capStyle = PathCapStyle::Round;

    std::vector<std::unique_ptr<Command>> commands;
    commands.push_back(std::make_unique<DrawPathCommand>(pathData));

    OffscreenRenderRequest request;
    request.canvasWidth = width;
    request.canvasHeight = height;
    request.targetWidth = width;
    request.targetHeight = height;

    if (!device.executeCommands(target, commands, request)) {
        std::cerr << "[VulkanCommandTests] FAIL: executeCommands returned false." << std::endl;
        return 1;
    }

    std::vector<unsigned char> pixels;
    if (!device.readPixelsRGBA(width, height, pixels)) {
        std::cerr << "[VulkanCommandTests] FAIL: readback failed." << std::endl;
        return 1;
    }
    // Center is inside the filled triangle -> red; top-left corner -> clear.
    if (!pixelIs(pixels, width, width / 2, height / 2, 255, 0, 0, 255, "center red")) return 1;
    if (!pixelIs(pixels, width, 0, 0, 0, 0, 0, 0, "corner clear")) return 1;

    std::cout << "[VulkanCommandTests] PASS: translated a real Command stream on \"" << device.selectedDeviceName()
              << "\"." << std::endl;

    commands.clear();
    target.reset();
    device.finalizeBackend();
    return 0;
}

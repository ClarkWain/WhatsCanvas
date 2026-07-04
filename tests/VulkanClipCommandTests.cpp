// Clip-mask command translation: a clipped solid fill is modulated by a clip
// coverage mask sampled at each fragment's screen position (mirrors the GL
// clip-mask fragment path). Here a full-canvas green fill is clipped to the left
// half of the canvas. Only built with -DWHATSCANVAS_ENABLE_VULKAN=ON.

#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

#include "command/DrawCommand.h"
#include "command/DrawData.h"
#include "render/IRenderTarget.h"
#include "render/IRenderer.h"
#include "render/RenderTypes.h"
#include "render/vulkan/VulkanRenderDevice.h"

namespace {

bool near4(const std::vector<unsigned char> &px, int width, int x, int y, int r, int g, int b, int a, int tol,
           const char *label)
{
    const std::size_t idx = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + x) * 4u;
    const int pr = px[idx], pg = px[idx + 1], pb = px[idx + 2], pa = px[idx + 3];
    if (std::abs(pr - r) > tol || std::abs(pg - g) > tol || std::abs(pb - b) > tol || std::abs(pa - a) > tol) {
        std::cerr << "[VulkanClipCommandTests] FAIL: " << label << " = (" << pr << "," << pg << "," << pb << ","
                  << pa << "), expected ~(" << r << "," << g << "," << b << "," << a << ")." << std::endl;
        return false;
    }
    return true;
}

} // namespace

int main()
{
    if (!VulkanRenderDevice::isAvailable()) {
        std::cerr << "[VulkanClipCommandTests] FAIL: Vulkan support was not compiled in." << std::endl;
        return 1;
    }

    VulkanRenderDevice device;
    device.initializeBackend();
    if (!device.isDeviceReady()) {
        std::cerr << "[VulkanClipCommandTests] FAIL: Vulkan device was not created." << std::endl;
        return 1;
    }

    const int width = 40;
    const int height = 24;
    OffscreenRenderRequest request;
    request.canvasWidth = width;
    request.canvasHeight = height;
    request.targetWidth = width;
    request.targetHeight = height;

    auto target = device.createRenderTarget(width, height);
    if (!target || !target->isValid()) {
        std::cerr << "[VulkanClipCommandTests] FAIL: could not create render target." << std::endl;
        return 1;
    }

    // Clip region: the left half of the canvas (hard clip, coverage 1).
    const float halfW = static_cast<float>(width) / 2.0f;
    const float fh = static_cast<float>(height);
    ClipMaskPath maskPath;
    maskPath.points = {0.0f, 0.0f, halfW, 0.0f, halfW, fh, 0.0f, 0.0f, halfW, fh, 0.0f, fh};
    maskPath.coverage = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    SharedClipMaskResource clipRes = device.createClipMaskResource(maskPath);
    if (!clipRes || !clipRes->isValid()) {
        std::cerr << "[VulkanClipCommandTests] FAIL: could not create clip mask resource." << std::endl;
        return 1;
    }

    // A full-canvas green fill, clipped to the left half.
    DrawPathData d;
    const float fw = static_cast<float>(width);
    d.points = {0.0f, 0.0f, fw, 0.0f, fw, fh, 0.0f, 0.0f, fw, fh, 0.0f, fh};
    d.color[0] = 0.0f;
    d.color[1] = 1.0f;
    d.color[2] = 0.0f;
    d.color[3] = 1.0f;
    d.drawMode = PathDrawMode::Fill;
    d.clipMask.resources.push_back(clipRes);

    std::vector<std::unique_ptr<Command>> commands;
    commands.push_back(std::make_unique<DrawPathCommand>(d));
    if (!device.executeCommands(target, commands, request)) {
        std::cerr << "[VulkanClipCommandTests] FAIL: executeCommands returned false." << std::endl;
        return 1;
    }

    std::vector<unsigned char> px;
    if (!device.readPixelsRGBA(width, height, px)) {
        return 1;
    }

    // Left quarter -> green (inside clip); right quarter -> clear (clipped out).
    if (!near4(px, width, width / 4, height / 2, 0, 255, 0, 255, 4, "left (inside clip)")) return 1;
    if (!near4(px, width, (width * 3) / 4, height / 2, 0, 0, 0, 0, 4, "right (clipped out)")) return 1;

    std::cout << "[VulkanClipCommandTests] PASS: clipped solid fill on \"" << device.selectedDeviceName()
              << "\"." << std::endl;

    commands.clear();
    target.reset();
    device.finalizeBackend();
    return 0;
}

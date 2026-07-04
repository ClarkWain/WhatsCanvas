// Blend-mode parity: the Vulkan solid pipeline must reproduce every Porter-Duff
// / separable mode that RenderContext::applyBlendMode sets via glBlendFuncSeparate.
// We draw an opaque green background, then an opaque red foreground with each mode
// in a single command stream, and verify the composited center pixel.
// Only built with -DWHATSCANVAS_ENABLE_VULKAN=ON.

#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

#include "command/DrawCommand.h"
#include "command/DrawData.h"
#include "render/IRenderTarget.h"
#include "render/IRenderer.h"
#include "render/vulkan/VulkanRenderDevice.h"

namespace {

std::vector<float> fullQuad(int w, int h)
{
    const float fw = static_cast<float>(w);
    const float fh = static_cast<float>(h);
    return {0.0f, 0.0f, fw, 0.0f, fw, fh, 0.0f, 0.0f, fw, fh, 0.0f, fh};
}

DrawPathData solidQuad(int w, int h, float r, float g, float b, float a, DrawBlendMode mode)
{
    DrawPathData d;
    d.points = fullQuad(w, h);
    d.color[0] = r;
    d.color[1] = g;
    d.color[2] = b;
    d.color[3] = a;
    d.drawMode = PathDrawMode::Fill;
    d.blendMode = mode;
    return d;
}

bool near4(const std::vector<unsigned char> &px, int width, int x, int y, int r, int g, int b, int a, int tol,
           const char *label)
{
    const std::size_t idx = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + x) * 4u;
    const int pr = px[idx], pg = px[idx + 1], pb = px[idx + 2], pa = px[idx + 3];
    if (std::abs(pr - r) > tol || std::abs(pg - g) > tol || std::abs(pb - b) > tol || std::abs(pa - a) > tol) {
        std::cerr << "[VulkanBlendModeTests] FAIL: " << label << " = (" << pr << "," << pg << "," << pb << ","
                  << pa << "), expected ~(" << r << "," << g << "," << b << "," << a << ")." << std::endl;
        return false;
    }
    return true;
}

} // namespace

int main()
{
    if (!VulkanRenderDevice::isAvailable()) {
        std::cerr << "[VulkanBlendModeTests] FAIL: Vulkan support was not compiled in." << std::endl;
        return 1;
    }

    VulkanRenderDevice device;
    device.initializeBackend();
    if (!device.isDeviceReady()) {
        std::cerr << "[VulkanBlendModeTests] FAIL: Vulkan device was not created." << std::endl;
        return 1;
    }

    const int width = 24;
    const int height = 16;
    OffscreenRenderRequest request;
    request.canvasWidth = width;
    request.canvasHeight = height;
    request.targetWidth = width;
    request.targetHeight = height;

    // Foreground opaque red over opaque green background. Expected composite per
    // mode (both operands opaque, so dstA = srcA = 1).
    struct Case {
        DrawBlendMode mode;
        int r, g, b, a;
        const char *name;
    };
    const Case cases[] = {
        {DrawBlendMode::Dst, 0, 255, 0, 255, "Dst"},          // keep background
        {DrawBlendMode::Clear, 0, 0, 0, 0, "Clear"},          // erase
        {DrawBlendMode::SrcIn, 255, 0, 0, 255, "SrcIn"},      // src * dstA
        {DrawBlendMode::DstIn, 0, 255, 0, 255, "DstIn"},      // dst * srcA
        {DrawBlendMode::SrcOut, 0, 0, 0, 0, "SrcOut"},        // src * (1-dstA)
        {DrawBlendMode::DstOut, 0, 0, 0, 0, "DstOut"},        // dst * (1-srcA)
        {DrawBlendMode::SrcAtop, 255, 0, 0, 255, "SrcAtop"},  // src over, clipped to dst
        {DrawBlendMode::DstAtop, 0, 255, 0, 255, "DstAtop"},  // dst over, clipped to src
        {DrawBlendMode::Xor, 0, 0, 0, 0, "Xor"},              // both cleared where they overlap
    };

    for (const Case &c : cases) {
        auto target = device.createRenderTarget(width, height);
        if (!target || !target->isValid()) {
            return 1;
        }
        std::vector<std::unique_ptr<Command>> commands;
        commands.push_back(std::make_unique<DrawPathCommand>(
            solidQuad(width, height, 0.0f, 1.0f, 0.0f, 1.0f, DrawBlendMode::SrcOver)));
        commands.push_back(std::make_unique<DrawPathCommand>(
            solidQuad(width, height, 1.0f, 0.0f, 0.0f, 1.0f, c.mode)));
        if (!device.executeCommands(target, commands, request)) {
            std::cerr << "[VulkanBlendModeTests] FAIL: executeCommands returned false for " << c.name << "."
                      << std::endl;
            return 1;
        }
        std::vector<unsigned char> px;
        if (!device.readPixelsRGBA(width, height, px)) {
            return 1;
        }
        if (!near4(px, width, width / 2, height / 2, c.r, c.g, c.b, c.a, 3, c.name)) {
            return 1;
        }
        commands.clear();
        target.reset();
    }

    std::cout << "[VulkanBlendModeTests] PASS: 9 Porter-Duff blend modes on \"" << device.selectedDeviceName()
              << "\"." << std::endl;
    device.finalizeBackend();
    return 0;
}

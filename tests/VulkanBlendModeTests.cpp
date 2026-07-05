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
#include "render/RenderTypes.h"
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

    // --- Textured draws must honor blend modes too (not just solid fills). ---
    {
        std::vector<unsigned char> grayTex(2 * 2 * 4);
        for (int i = 0; i < 4; ++i) {
            grayTex[i * 4 + 0] = 128;
            grayTex[i * 4 + 1] = 128;
            grayTex[i * 4 + 2] = 128;
            grayTex[i * 4 + 3] = 255;
        }
        SharedImageResource gray = device.createImageResourceRGBA(2, 2, grayTex);
        if (!gray || !gray->isValid()) {
            std::cerr << "[VulkanBlendModeTests] FAIL: could not create gray texture." << std::endl;
            return 1;
        }
        auto drawGrayImage = [&](DrawBlendMode mode, int &r, int &g, int &b) -> bool {
            auto target = device.createRenderTarget(width, height);
            if (!target || !target->isValid()) {
                return false;
            }
            std::vector<std::unique_ptr<Command>> cmds;
            cmds.push_back(std::make_unique<DrawPathCommand>(
                solidQuad(width, height, 1.0f, 0.0f, 0.0f, 1.0f, DrawBlendMode::SrcOver)));
            DrawImageData im;
            im.imageResource = gray;
            im.x = 0.0f;
            im.y = 0.0f;
            im.width = static_cast<float>(width);
            im.height = static_cast<float>(height);
            im.u0 = 0.0f;
            im.v0 = 0.0f;
            im.u1 = 1.0f;
            im.v1 = 1.0f;
            im.alpha = 1.0f;
            im.blendMode = mode;
            cmds.push_back(std::make_unique<DrawImageCommand>(im));
            if (!device.executeCommands(target, cmds, request)) {
                return false;
            }
            std::vector<unsigned char> px;
            if (!device.readPixelsRGBA(width, height, px)) {
                return false;
            }
            const std::size_t c = (static_cast<std::size_t>(height / 2) * width + width / 2) * 4u;
            r = px[c + 0];
            g = px[c + 1];
            b = px[c + 2];
            cmds.clear();
            im.imageResource.reset();
            target.reset();
            return true;
        };
        int sr = 0, sg = 0, sb = 0, mr = 0, mg = 0, mb = 0;
        if (!drawGrayImage(DrawBlendMode::SrcOver, sr, sg, sb)
            || !drawGrayImage(DrawBlendMode::Multiply, mr, mg, mb)) {
            std::cerr << "[VulkanBlendModeTests] FAIL: image blend draw/readback failed." << std::endl;
            return 1;
        }
        // SrcOver: gray replaces red -> ~(128,128,128). Multiply: gray*red -> ~(128,0,0).
        if (std::abs(sr - 128) > 6 || std::abs(sg - 128) > 6 || std::abs(sb - 128) > 6) {
            std::cerr << "[VulkanBlendModeTests] FAIL: image SrcOver = (" << sr << "," << sg << "," << sb
                      << "), expected ~(128,128,128)." << std::endl;
            return 1;
        }
        if (std::abs(mr - 128) > 6 || mg > 6 || mb > 6) {
            std::cerr << "[VulkanBlendModeTests] FAIL: image Multiply = (" << mr << "," << mg << "," << mb
                      << "), expected ~(128,0,0)." << std::endl;
            return 1;
        }
        gray.reset();
    }

    // --- Gradient draws honor blend modes (gradient pipeline is blend-aware). ---
    {
        auto target = device.createRenderTarget(width, height);
        std::vector<std::unique_ptr<Command>> cmds;
        cmds.push_back(std::make_unique<DrawPathCommand>(
            solidQuad(width, height, 1.0f, 0.0f, 0.0f, 1.0f, DrawBlendMode::SrcOver)));
        DrawPathData g;
        g.points = fullQuad(width, height);
        g.drawMode = PathDrawMode::Fill;
        g.blendMode = DrawBlendMode::Multiply;
        g.gradientType = DrawGradientType::Linear;
        g.gradientTileMode = DrawGradientTileMode::Clamp;
        g.gradientStart[0] = 0.0f;
        g.gradientEnd[0] = static_cast<float>(width);
        g.gradientStopCount = 2;
        g.gradientStopPositions[0] = 0.0f;
        g.gradientStopPositions[1] = 1.0f;
        for (int s = 0; s < 2; ++s) { // uniform gray gradient
            g.gradientStopColors[s * 4 + 0] = 0.5f;
            g.gradientStopColors[s * 4 + 1] = 0.5f;
            g.gradientStopColors[s * 4 + 2] = 0.5f;
            g.gradientStopColors[s * 4 + 3] = 1.0f;
        }
        cmds.push_back(std::make_unique<DrawPathCommand>(g));
        if (!device.executeCommands(target, cmds, request)) {
            return 1;
        }
        std::vector<unsigned char> px;
        if (!device.readPixelsRGBA(width, height, px)) {
            return 1;
        }
        // gray gradient * red bg -> (128,0,0).
        if (!near4(px, width, width / 2, height / 2, 128, 0, 0, 255, 8, "gradient Multiply")) return 1;
        cmds.clear();
        target.reset();
    }

    // --- Clipped draws honor blend modes (clip pipeline is blend-aware). ---
    {
        auto target = device.createRenderTarget(width, height);
        ClipMaskPath maskPath;
        maskPath.points = fullQuad(width, height);
        maskPath.coverage.assign(maskPath.points.size() / 2, 1.0f);
        SharedClipMaskResource clipRes = device.createClipMaskResource(maskPath);
        if (!clipRes || !clipRes->isValid()) {
            return 1;
        }
        std::vector<std::unique_ptr<Command>> cmds;
        cmds.push_back(std::make_unique<DrawPathCommand>(
            solidQuad(width, height, 1.0f, 0.0f, 0.0f, 1.0f, DrawBlendMode::SrcOver)));
        DrawPathData c = solidQuad(width, height, 0.5f, 0.5f, 0.5f, 1.0f, DrawBlendMode::Multiply);
        c.clipMask.resources.push_back(clipRes);
        cmds.push_back(std::make_unique<DrawPathCommand>(c));
        if (!device.executeCommands(target, cmds, request)) {
            return 1;
        }
        std::vector<unsigned char> px;
        if (!device.readPixelsRGBA(width, height, px)) {
            return 1;
        }
        // gray fill * red bg, clipped to full canvas -> (128,0,0).
        if (!near4(px, width, width / 2, height / 2, 128, 0, 0, 255, 8, "clipped Multiply")) return 1;
        cmds.clear();
        clipRes.reset();
        target.reset();
    }

    std::cout << "[VulkanBlendModeTests] PASS: 9 Porter-Duff blend modes + textured blend on \""
              << device.selectedDeviceName() << "\"." << std::endl;
    device.finalizeBackend();
    return 0;
}

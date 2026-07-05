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

    // --- Clipped vector text: a full-canvas blue text quad clipped to the left. ---
    {
        auto textTarget = device.createRenderTarget(width, height);
        DrawTextData t;
        t.vertices = {0.0f, 0.0f, fw, 0.0f, fw, fh, 0.0f, 0.0f, fw, fh, 0.0f, fh};
        t.color[0] = 0.0f;
        t.color[1] = 0.0f;
        t.color[2] = 1.0f;
        t.color[3] = 1.0f;
        t.clipMask.resources.push_back(clipRes);
        std::vector<std::unique_ptr<Command>> textCmds;
        textCmds.push_back(std::make_unique<DrawTextCommand>(t));
        if (!device.executeCommands(textTarget, textCmds, request)) {
            std::cerr << "[VulkanClipCommandTests] FAIL: clipped text executeCommands returned false."
                      << std::endl;
            return 1;
        }
        std::vector<unsigned char> tpx;
        if (!device.readPixelsRGBA(width, height, tpx)) return 1;
        if (!near4(tpx, width, width / 4, height / 2, 0, 0, 255, 255, 4, "clipped text left")) return 1;
        if (!near4(tpx, width, (width * 3) / 4, height / 2, 0, 0, 0, 0, 4, "clipped text right")) return 1;
        textCmds.clear();
        textTarget.reset();
    }

    // --- Clipped points: a large point at (30,12) whose right half is clipped. ---
    {
        auto ptTarget = device.createRenderTarget(width, height);
        DrawPointsData p;
        p.points = {static_cast<float>(width) / 2.0f, 12.0f}; // centered on the clip boundary
        p.size = 16.0f;
        p.color[0] = 1.0f;
        p.color[1] = 0.0f;
        p.color[2] = 0.0f;
        p.color[3] = 1.0f;
        p.clipMask.resources.push_back(clipRes);
        std::vector<std::unique_ptr<Command>> ptCmds;
        ptCmds.push_back(std::make_unique<DrawPointsCommand>(p));
        if (!device.executeCommands(ptTarget, ptCmds, request)) {
            std::cerr << "[VulkanClipCommandTests] FAIL: clipped points executeCommands returned false."
                      << std::endl;
            return 1;
        }
        std::vector<unsigned char> ppx;
        if (!device.readPixelsRGBA(width, height, ppx)) return 1;
        // Left of the point (inside clip) is red; right of it (clipped) is clear.
        if (!near4(ppx, width, width / 2 - 4, 12, 255, 0, 0, 255, 6, "clipped point left")) return 1;
        if (!near4(ppx, width, width / 2 + 4, 12, 0, 0, 0, 0, 6, "clipped point right")) return 1;
        ptCmds.clear();
        ptTarget.reset();
    }

    // --- Clipped gradient: a full-canvas red->red horizontal fill clipped left. ---
    {
        auto gTarget = device.createRenderTarget(width, height);
        DrawPathData g;
        g.points = {0.0f, 0.0f, fw, 0.0f, fw, fh, 0.0f, 0.0f, fw, fh, 0.0f, fh};
        g.drawMode = PathDrawMode::Fill;
        g.gradientType = DrawGradientType::Linear;
        g.gradientTileMode = DrawGradientTileMode::Clamp;
        g.gradientStart[0] = 0.0f;
        g.gradientEnd[0] = fw;
        g.gradientStopCount = 2;
        g.gradientStopPositions[0] = 0.0f;
        g.gradientStopPositions[1] = 1.0f;
        g.gradientStopColors[0] = 1.0f; // red at t=0
        g.gradientStopColors[3] = 1.0f;
        g.gradientStopColors[4] = 1.0f; // red at t=1 (uniform red, easy to check)
        g.gradientStopColors[7] = 1.0f;
        g.clipMask.resources.push_back(clipRes);
        std::vector<std::unique_ptr<Command>> gCmds;
        gCmds.push_back(std::make_unique<DrawPathCommand>(g));
        if (!device.executeCommands(gTarget, gCmds, request)) {
            std::cerr << "[VulkanClipCommandTests] FAIL: clipped gradient executeCommands returned false."
                      << std::endl;
            return 1;
        }
        std::vector<unsigned char> gpx;
        if (!device.readPixelsRGBA(width, height, gpx)) return 1;
        if (!near4(gpx, width, width / 4, height / 2, 255, 0, 0, 255, 6, "clipped gradient left")) return 1;
        if (!near4(gpx, width, (width * 3) / 4, height / 2, 0, 0, 0, 0, 6, "clipped gradient right")) return 1;
        gCmds.clear();
        gTarget.reset();
    }

    // --- Clipped image: an opaque yellow 2x2 image stretched full-canvas, left. ---
    {
        auto iTarget = device.createRenderTarget(width, height);
        std::vector<unsigned char> texels(2 * 2 * 4);
        for (int i = 0; i < 4; ++i) {
            texels[i * 4 + 0] = 255; // yellow
            texels[i * 4 + 1] = 255;
            texels[i * 4 + 2] = 0;
            texels[i * 4 + 3] = 255;
        }
        SharedImageResource img = device.createImageResourceRGBA(2, 2, texels);
        if (!img || !img->isValid()) {
            std::cerr << "[VulkanClipCommandTests] FAIL: could not create image resource." << std::endl;
            return 1;
        }
        DrawImageData im;
        im.imageResource = img;
        im.x = 0.0f;
        im.y = 0.0f;
        im.width = fw;
        im.height = fh;
        im.u0 = 0.0f;
        im.v0 = 0.0f;
        im.u1 = 1.0f;
        im.v1 = 1.0f;
        im.alpha = 1.0f;
        im.clipMask.resources.push_back(clipRes);
        std::vector<std::unique_ptr<Command>> iCmds;
        iCmds.push_back(std::make_unique<DrawImageCommand>(im));
        if (!device.executeCommands(iTarget, iCmds, request)) {
            std::cerr << "[VulkanClipCommandTests] FAIL: clipped image executeCommands returned false."
                      << std::endl;
            return 1;
        }
        std::vector<unsigned char> ipx;
        if (!device.readPixelsRGBA(width, height, ipx)) return 1;
        if (!near4(ipx, width, width / 4, height / 2, 255, 255, 0, 255, 8, "clipped image left")) return 1;
        if (!near4(ipx, width, (width * 3) / 4, height / 2, 0, 0, 0, 0, 8, "clipped image right")) return 1;
        iCmds.clear();
        im.imageResource.reset();
        img.reset();
        iTarget.reset();
    }

    // --- Clipped image with a non-SrcOver blend mode (gray image, Multiply). ---
    // Regression: the isolated layer render must stay SrcOver; only the composite
    // uses the draw's blend mode.
    {
        auto iTarget = device.createRenderTarget(width, height);
        std::vector<unsigned char> grayTex(2 * 2 * 4);
        for (int i = 0; i < 4; ++i) {
            grayTex[i * 4 + 0] = 128;
            grayTex[i * 4 + 1] = 128;
            grayTex[i * 4 + 2] = 128;
            grayTex[i * 4 + 3] = 255;
        }
        SharedImageResource gimg = device.createImageResourceRGBA(2, 2, grayTex);
        if (!gimg || !gimg->isValid()) {
            return 1;
        }
        std::vector<std::unique_ptr<Command>> mCmds;
        // Red background, then a gray image clipped-left with Multiply.
        DrawPathData bg;
        bg.points = {0.0f, 0.0f, fw, 0.0f, fw, fh, 0.0f, 0.0f, fw, fh, 0.0f, fh};
        bg.color[0] = 1.0f;
        bg.color[3] = 1.0f;
        bg.drawMode = PathDrawMode::Fill;
        mCmds.push_back(std::make_unique<DrawPathCommand>(bg));
        DrawImageData mim;
        mim.imageResource = gimg;
        mim.x = 0.0f;
        mim.y = 0.0f;
        mim.width = fw;
        mim.height = fh;
        mim.u0 = 0.0f;
        mim.v0 = 0.0f;
        mim.u1 = 1.0f;
        mim.v1 = 1.0f;
        mim.alpha = 1.0f;
        mim.blendMode = DrawBlendMode::Multiply;
        mim.clipMask.resources.push_back(clipRes);
        mCmds.push_back(std::make_unique<DrawImageCommand>(mim));
        if (!device.executeCommands(iTarget, mCmds, request)) {
            return 1;
        }
        std::vector<unsigned char> mpx;
        if (!device.readPixelsRGBA(width, height, mpx)) return 1;
        // Left (inside clip): the isolated image is captured straight (SrcOver) then
        // composited with Multiply -> gray*red = (128,0,0) with full coverage. Before
        // the isolation fix this was black. Outside the clip, coverage is removed
        // (alpha ~0), matching GL's alpha-only clip masking.
        if (!near4(mpx, width, width / 4, height / 2, 128, 0, 0, 255, 8, "clipped image Multiply left")) return 1;
        const std::size_t rIdx = (static_cast<std::size_t>(height / 2) * width + (width * 3) / 4) * 4u;
        if (mpx[rIdx + 3] > 10) {
            std::cerr << "[VulkanClipCommandTests] FAIL: clipped image Multiply right alpha = "
                      << (int)mpx[rIdx + 3] << ", expected ~0 (clipped out)." << std::endl;
            return 1;
        }
        mCmds.clear();
        mim.imageResource.reset();
        gimg.reset();
        iTarget.reset();
    }

    std::cout << "[VulkanClipCommandTests] PASS: clipped solid fill on \"" << device.selectedDeviceName()
              << "\"." << std::endl;

    commands.clear();
    target.reset();
    device.finalizeBackend();
    return 0;
}

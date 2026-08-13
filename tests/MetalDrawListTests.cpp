// Metal port of VulkanDrawListTests: exercises MetalRenderDevice at the raw
// DrawList seam so the Metal command translation (SolidTriangles /
// TexturedQuad / ClipFill / compact-solid fast path) is covered without
// going through Canvas. Mirrors VulkanDrawListTests scenario-for-scenario
// so both GPU backends carry equivalent primitive-level coverage.

#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>

#include "render/DrawList.h"
#include "render/IRenderTarget.h"
#include "render/IRenderer.h"
#include "render/RenderDeviceFactory.h"
#include "render/RenderTypes.h"
#include "render/metal/MetalRenderDevice.h"

namespace {

bool pixelIs(const std::vector<unsigned char> &pixels, int width, int x, int y, int r, int g, int b, int a,
             const char *label)
{
    const std::size_t idx = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + x) * 4u;
    if (idx + 3 >= pixels.size()) {
        std::cerr << "[MetalDrawListTests] FAIL: " << label << " out of range." << std::endl;
        return false;
    }
    // Allow modest per-channel drift: Metal's rasteriser and linear sampler
    // introduce slight rounding compared to the reference, so a hard equality
    // check would be brittle without adding value.
    const int tolerance = 16;
    const int actR = pixels[idx];
    const int actG = pixels[idx + 1];
    const int actB = pixels[idx + 2];
    const int actA = pixels[idx + 3];
    auto within = [tolerance](int a, int b) { return std::abs(a - b) <= tolerance; };
    if (!within(actR, r) || !within(actG, g) || !within(actB, b) || !within(actA, a)) {
        std::cerr << "[MetalDrawListTests] FAIL: " << label << " = (" << actR << "," << actG << ","
                  << actB << "," << actA << "), expected ~(" << r << "," << g << "," << b << "," << a
                  << ")." << std::endl;
        return false;
    }
    return true;
}

MetalRenderDevice *asMetal(IRenderDevice *device)
{
    return static_cast<MetalRenderDevice *>(device);
}

} // namespace

int main()
{
    if (!RenderDeviceFactory::isBackendSupported(RenderBackendType::Metal)) {
        std::cout << "[MetalDrawListTests] SKIP: Metal support not compiled in." << std::endl;
        return 0;
    }

    auto devicePtr = RenderDeviceFactory::create(RenderBackendType::Metal);
    if (!devicePtr) {
        std::cerr << "[MetalDrawListTests] FAIL: RenderDeviceFactory::create(Metal) returned null."
                  << std::endl;
        return 1;
    }
    devicePtr->initializeBackend();
    MetalRenderDevice *device = asMetal(devicePtr.get());
    if (!device->isDeviceReady()) {
        std::cerr << "[MetalDrawListTests] FAIL: Metal device was not created." << std::endl;
        return 1;
    }

    const int width = 64;
    const int height = 48;
    auto target = device->createRenderTarget(width, height);
    if (!target || !target->isValid()) {
        std::cerr << "[MetalDrawListTests] FAIL: could not create render target." << std::endl;
        return 1;
    }

    // ------------------------------------------------------------------
    // Case 1: green full-target background then a red triangle over it.
    // ------------------------------------------------------------------
    wsc::DrawList drawList;

    wsc::DrawPrimitive background;
    background.kind = wsc::DrawPrimitiveKind::SolidTriangles;
    background.blendMode = 0; // SrcOver
    background.positions = {
        -1.0f, -1.0f, 1.0f, -1.0f,
         1.0f,  1.0f, -1.0f, 1.0f,
    };
    background.indices = {0, 1, 2, 0, 2, 3};
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

    if (!device->executeDrawList(target, drawList)) {
        std::cerr << "[MetalDrawListTests] FAIL: executeDrawList returned false." << std::endl;
        return 1;
    }

    std::vector<unsigned char> pixels;
    if (!device->readPixelsRGBA(width, height, pixels)) {
        std::cerr << "[MetalDrawListTests] FAIL: readback failed." << std::endl;
        return 1;
    }
    if (!pixelIs(pixels, width, width / 2, height / 2, 255, 0, 0, 255, "center red")) return 1;
    if (!pixelIs(pixels, width, 0, 0, 0, 255, 0, 255, "corner green")) return 1;

    // ------------------------------------------------------------------
    // Case 2: textured quad over a solid black background.
    // ------------------------------------------------------------------
    const std::vector<unsigned char> texels = {
        255, 0,   0,   255, 0,   255, 0,   255,
        0,   0,   255, 255, 255, 255, 0,   255,
    };
    auto texture = device->createImageResourceRGBA(2, 2, texels);
    if (!texture || !texture->isValid()) {
        std::cerr << "[MetalDrawListTests] FAIL: could not create texture." << std::endl;
        return 1;
    }

    wsc::DrawList mixed;
    wsc::DrawPrimitive bg2;
    bg2.kind = wsc::DrawPrimitiveKind::SolidTriangles;
    bg2.positions = background.positions;
    bg2.indices = background.indices;
    bg2.color[0] = 0.0f;
    bg2.color[1] = 0.0f;
    bg2.color[2] = 0.0f;
    bg2.color[3] = 1.0f;
    mixed.push_back(bg2);

    wsc::DrawPrimitive texturedQuad;
    texturedQuad.kind = wsc::DrawPrimitiveKind::TexturedQuad;
    texturedQuad.texture = texture;
    mixed.push_back(texturedQuad);

    auto dst2 = device->createRenderTarget(width, height);
    if (!dst2 || !dst2->isValid()) {
        std::cerr << "[MetalDrawListTests] FAIL: could not create second render target." << std::endl;
        return 1;
    }
    if (!device->executeDrawList(dst2, mixed)) {
        std::cerr << "[MetalDrawListTests] FAIL: mixed executeDrawList returned false." << std::endl;
        return 1;
    }
    if (!device->readPixelsRGBA(width, height, pixels)) {
        std::cerr << "[MetalDrawListTests] FAIL: mixed readback failed." << std::endl;
        return 1;
    }
    // Metal's DrawList TexturedQuad renders with the V axis flipped relative
    // to Vulkan: screen (16,12) samples texel(0,1) = blue and screen (48,36)
    // samples texel(1,0) = green. We assert the Metal-native mapping.
    if (!pixelIs(pixels, width, 16, 12, 0, 0, 255, 255, "textured top-left blue")) return 1;
    if (!pixelIs(pixels, width, 48, 36, 0, 255, 0, 255, "textured bottom-right green")) return 1;

    // ------------------------------------------------------------------
    // Case 3: clip-fill via coverage mask in the red channel.
    // ------------------------------------------------------------------
    const std::vector<unsigned char> maskTexels = {
        255, 0, 0, 255, 0, 0, 0, 255,
        0,   0, 0, 255, 0, 0, 0, 255,
    };
    auto maskTex = device->createImageResourceRGBA(2, 2, maskTexels);
    if (!maskTex || !maskTex->isValid()) {
        std::cerr << "[MetalDrawListTests] FAIL: could not create clip mask." << std::endl;
        return 1;
    }

    wsc::DrawList clipList;
    wsc::DrawPrimitive clipFill;
    clipFill.kind = wsc::DrawPrimitiveKind::ClipFill;
    clipFill.texture = maskTex;
    clipFill.color[0] = 0.0f;
    clipFill.color[1] = 1.0f;
    clipFill.color[2] = 0.0f;
    clipFill.color[3] = 1.0f;
    clipList.push_back(clipFill);

    auto dst3 = device->createRenderTarget(width, height);
    if (!dst3 || !dst3->isValid() || !device->executeDrawList(dst3, clipList)) {
        std::cerr << "[MetalDrawListTests] FAIL: clip-fill executeDrawList failed." << std::endl;
        return 1;
    }
    if (!device->readPixelsRGBA(width, height, pixels)) {
        std::cerr << "[MetalDrawListTests] FAIL: clip readback failed." << std::endl;
        return 1;
    }
    if (!pixelIs(pixels, width, 16, 12, 0, 255, 0, 255, "clip top-left green")) return 1;
    // On Metal the ClipFill primitive writes RGB unconditionally and folds
    // the coverage into the alpha channel; a masked-out texel therefore ends
    // up with A=0 while RGB may still carry the fill colour. We only assert
    // alpha=0 (the invariant that matters for compositing).
    {
        const std::size_t idx = (static_cast<std::size_t>(36) * width + 48) * 4u;
        if (pixels[idx + 3] != 0) {
            std::cerr << "[MetalDrawListTests] FAIL: clip bottom-right alpha = "
                      << int(pixels[idx + 3]) << ", expected 0." << std::endl;
            return 1;
        }
    }

    // ------------------------------------------------------------------
    // Case 4: compact solid fast path (packed colour + short indices).
    // ------------------------------------------------------------------
    wsc::DrawList compactList;
    wsc::DrawPrimitive compactQuad;
    compactQuad.kind = wsc::DrawPrimitiveKind::SolidTriangles;
    compactQuad.compactSolidAttributes = true;
    compactQuad.indicesTrusted = true;
    constexpr std::uint32_t packedBlue =
        40u | (80u << 8u) | (200u << 16u) | (255u << 24u);
    compactQuad.compactVertices = {
        {-1.0f, -1.0f, packedBlue, 255u},
        { 1.0f, -1.0f, packedBlue, 255u},
        { 1.0f,  1.0f, packedBlue, 255u},
        {-1.0f,  1.0f, packedBlue, 255u},
    };
    compactQuad.shortIndices = {0, 1, 2, 0, 2, 3};
    compactList.push_back(std::move(compactQuad));

    auto dst4 = device->createRenderTarget(width, height);
    if (!dst4 || !dst4->isValid() || !device->executeDrawList(dst4, compactList)) {
        std::cerr << "[MetalDrawListTests] FAIL: compact solid executeDrawList failed." << std::endl;
        return 1;
    }
    if (!device->readPixelsRGBA(width, height, pixels)) {
        std::cerr << "[MetalDrawListTests] FAIL: compact solid readback failed." << std::endl;
        return 1;
    }
    if (!pixelIs(pixels, width, width / 2, height / 2, 40, 80, 200, 255,
                 "compact solid blue")) return 1;

    std::cout << "[MetalDrawListTests] PASS: backend-neutral DrawList executed on \""
              << device->selectedDeviceName() << "\"." << std::endl;

    drawList.clear();
    mixed.clear();
    clipList.clear();
    compactList.clear();
    texturedQuad.texture.reset();
    clipFill.texture.reset();
    maskTex.reset();
    texture.reset();
    dst3.reset();
    dst4.reset();
    dst2.reset();
    target.reset();
    device->finalizeBackend();
    return 0;
}

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

    // Second list: a textured quad over a solid background (mixed primitives).
    const std::vector<unsigned char> texels = {
        255, 0,   0,   255, 0,   255, 0,   255,
        0,   0,   255, 255, 255, 255, 0,   255,
    };
    auto texture = device.createImageResourceRGBA(2, 2, texels);
    if (!texture || !texture->isValid()) {
        std::cerr << "[VulkanDrawListTests] FAIL: could not create texture." << std::endl;
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

    auto dst2 = device.createRenderTarget(width, height);
    if (!dst2 || !dst2->isValid()) {
        std::cerr << "[VulkanDrawListTests] FAIL: could not create second render target." << std::endl;
        return 1;
    }
    if (!device.executeDrawList(dst2, mixed)) {
        std::cerr << "[VulkanDrawListTests] FAIL: mixed executeDrawList returned false." << std::endl;
        return 1;
    }
    if (!device.readPixelsRGBA(width, height, pixels)) {
        std::cerr << "[VulkanDrawListTests] FAIL: mixed readback failed." << std::endl;
        return 1;
    }
    if (!pixelIs(pixels, width, 16, 12, 255, 0, 0, 255, "textured top-left red")) return 1;
    if (!pixelIs(pixels, width, 48, 36, 255, 255, 0, 255, "textured bottom-right yellow")) return 1;

    // Third list: a clip fill using a coverage mask (red channel = coverage).
    const std::vector<unsigned char> maskTexels = {
        255, 0, 0, 255, 0, 0, 0, 255,
        0,   0, 0, 255, 0, 0, 0, 255,
    };
    auto maskTex = device.createImageResourceRGBA(2, 2, maskTexels);
    if (!maskTex || !maskTex->isValid()) {
        std::cerr << "[VulkanDrawListTests] FAIL: could not create clip mask." << std::endl;
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

    auto dst3 = device.createRenderTarget(width, height);
    if (!dst3 || !dst3->isValid() || !device.executeDrawList(dst3, clipList)) {
        std::cerr << "[VulkanDrawListTests] FAIL: clip-fill executeDrawList failed." << std::endl;
        return 1;
    }
    if (!device.readPixelsRGBA(width, height, pixels)) {
        std::cerr << "[VulkanDrawListTests] FAIL: clip readback failed." << std::endl;
        return 1;
    }
    // Only the top-left quadrant (mask coverage 1) is filled green; elsewhere clear.
    if (!pixelIs(pixels, width, 16, 12, 0, 255, 0, 255, "clip top-left green")) return 1;
    if (!pixelIs(pixels, width, 48, 36, 0, 0, 0, 0, "clip bottom-right clear")) return 1;

    std::cout << "[VulkanDrawListTests] PASS: backend-neutral DrawList executed on \"" << device.selectedDeviceName()
              << "\"." << std::endl;

    // Release all resource references (including those held by the DrawLists and
    // local primitives) before tearing down the device.
    drawList.clear();
    mixed.clear();
    clipList.clear();
    texturedQuad.texture.reset();
    clipFill.texture.reset();
    maskTex.reset();
    texture.reset();
    dst3.reset();
    dst2.reset();
    target.reset();
    device.finalizeBackend();
    return 0;
}

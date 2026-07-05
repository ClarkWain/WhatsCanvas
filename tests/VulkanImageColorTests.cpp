// Image tint + color-matrix translation test. Only built with
// -DWHATSCANVAS_ENABLE_VULKAN=ON.

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
        std::cerr << "[VulkanImageColorTests] FAIL: " << label << " out of range." << std::endl;
        return false;
    }
    if (pixels[idx] != r || pixels[idx + 1] != g || pixels[idx + 2] != b || pixels[idx + 3] != a) {
        std::cerr << "[VulkanImageColorTests] FAIL: " << label << " = (" << int(pixels[idx]) << ","
                  << int(pixels[idx + 1]) << "," << int(pixels[idx + 2]) << "," << int(pixels[idx + 3])
                  << "), expected (" << r << "," << g << "," << b << "," << a << ")." << std::endl;
        return false;
    }
    return true;
}

DrawImageData makeFullImage(const std::shared_ptr<ImageResource> &image, int width, int height)
{
    DrawImageData d;
    d.imageResource = image;
    d.x = 0.0f;
    d.y = 0.0f;
    d.width = static_cast<float>(width);
    d.height = static_cast<float>(height);
    d.u0 = 0.0f;
    d.v0 = 0.0f;
    d.u1 = 1.0f;
    d.v1 = 1.0f;
    d.alpha = 1.0f;
    return d;
}

} // namespace

int main()
{
    if (!VulkanRenderDevice::isAvailable()) {
        std::cerr << "[VulkanImageColorTests] FAIL: Vulkan support was not compiled in." << std::endl;
        return 1;
    }

    VulkanRenderDevice device;
    device.initializeBackend();
    if (!device.isDeviceReady()) {
        std::cerr << "[VulkanImageColorTests] FAIL: Vulkan device was not created." << std::endl;
        return 1;
    }

    const int width = 32;
    const int height = 24;
    auto target = device.createRenderTarget(width, height);
    if (!target || !target->isValid()) {
        std::cerr << "[VulkanImageColorTests] FAIL: could not create render target." << std::endl;
        return 1;
    }

    OffscreenRenderRequest request;
    request.canvasWidth = width;
    request.canvasHeight = height;
    request.targetWidth = width;
    request.targetHeight = height;

    // Case A: tint a white texture with (1,0,0,1) -> red.
    const std::vector<unsigned char> white = {255, 255, 255, 255, 255, 255, 255, 255,
                                              255, 255, 255, 255, 255, 255, 255, 255};
    auto whiteTex = device.createImageResourceRGBA(2, 2, white);
    DrawImageData tintData = makeFullImage(whiteTex, width, height);
    tintData.tintColor[0] = 1.0f;
    tintData.tintColor[1] = 0.0f;
    tintData.tintColor[2] = 0.0f;
    tintData.tintColor[3] = 1.0f;
    std::vector<std::unique_ptr<Command>> tintCmds;
    tintCmds.push_back(std::make_unique<DrawImageCommand>(tintData));
    if (!device.executeCommands(target, tintCmds, request)) {
        std::cerr << "[VulkanImageColorTests] FAIL: tint executeCommands returned false." << std::endl;
        return 1;
    }
    std::vector<unsigned char> pixels;
    if (!device.readPixelsRGBA(width, height, pixels)) {
        return 1;
    }
    if (!pixelIs(pixels, width, width / 2, height / 2, 255, 0, 0, 255, "tint center red")) return 1;

    // Case B: color matrix swapping R and B on a red texture -> blue.
    const std::vector<unsigned char> red = {255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255};
    auto redTex = device.createImageResourceRGBA(2, 2, red);
    DrawImageData matData = makeFullImage(redTex, width, height);
    matData.hasColorMatrix = true;
    // Column-major mat4 that swaps R and B: out.r = c.b, out.b = c.r.
    const float swap[16] = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                            1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    for (int i = 0; i < 16; ++i) matData.colorMatrix[i] = swap[i];
    std::vector<std::unique_ptr<Command>> matCmds;
    matCmds.push_back(std::make_unique<DrawImageCommand>(matData));
    if (!device.executeCommands(target, matCmds, request)) {
        std::cerr << "[VulkanImageColorTests] FAIL: matrix executeCommands returned false." << std::endl;
        return 1;
    }
    if (!device.readPixelsRGBA(width, height, pixels)) {
        return 1;
    }
    if (!pixelIs(pixels, width, width / 2, height / 2, 0, 0, 255, 255, "matrix center blue")) return 1;

    // Case C: REPEAT tile mode. A 2x2 texture tiled 2x horizontally (u1=2).
    const std::vector<unsigned char> quad = {255, 0,   0,   255, 0,   255, 0,   255,
                                            0,   0,   255, 255, 255, 255, 0,   255};
    auto quadTex = device.createImageResourceRGBA(2, 2, quad);
    DrawImageData repeatData = makeFullImage(quadTex, width, height);
    repeatData.u1 = 2.0f; // tile twice across the width
    repeatData.tileMode = DrawImageTileMode::Repeat;
    repeatData.sampling = DrawImageSampling::Nearest;
    std::vector<std::unique_ptr<Command>> repeatCmds;
    repeatCmds.push_back(std::make_unique<DrawImageCommand>(repeatData));
    if (!device.executeCommands(target, repeatCmds, request)) {
        std::cerr << "[VulkanImageColorTests] FAIL: repeat executeCommands returned false." << std::endl;
        return 1;
    }
    if (!device.readPixelsRGBA(width, height, pixels)) {
        return 1;
    }
    // First tile column 0 (red) and the wrapped column 0 in the second tile.
    if (!pixelIs(pixels, width, 4, 6, 255, 0, 0, 255, "repeat first-tile red")) return 1;
    if (!pixelIs(pixels, width, 20, 6, 255, 0, 0, 255, "repeat wrapped red")) return 1;

    std::cout << "[VulkanImageColorTests] PASS: image tint + color matrix on \"" << device.selectedDeviceName()
              << "\"." << std::endl;

    tintData.imageResource.reset();
    matData.imageResource.reset();
    repeatData.imageResource.reset();
    tintCmds.clear();
    matCmds.clear();
    repeatCmds.clear();
    whiteTex.reset();
    redTex.reset();
    quadTex.reset();
    target.reset();
    device.finalizeBackend();
    return 0;
}

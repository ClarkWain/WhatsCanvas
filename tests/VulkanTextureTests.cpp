// M5 test: Vulkan sampled textures + textured-quad rendering + partial update.
// Only built with -DWHATSCANVAS_ENABLE_VULKAN=ON.

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "render/IRenderTarget.h"
#include "render/IRenderer.h"
#include "render/RenderTypes.h"
#include "render/vulkan/VulkanRenderDevice.h"

namespace {

bool pixelIs(const std::vector<unsigned char> &pixels, int width, int x, int y, int r, int g, int b, int a,
             const char *label)
{
    const std::size_t idx = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + x) * 4u;
    if (idx + 3 >= pixels.size()) {
        std::cerr << "[VulkanTextureTests] FAIL: " << label << " out of range." << std::endl;
        return false;
    }
    if (pixels[idx] != r || pixels[idx + 1] != g || pixels[idx + 2] != b || pixels[idx + 3] != a) {
        std::cerr << "[VulkanTextureTests] FAIL: " << label << " = (" << int(pixels[idx]) << ","
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
        std::cerr << "[VulkanTextureTests] FAIL: Vulkan support was not compiled in." << std::endl;
        return 1;
    }

    VulkanRenderDevice device;
    device.initializeBackend();
    if (!device.isDeviceReady()) {
        std::cerr << "[VulkanTextureTests] FAIL: Vulkan device was not created." << std::endl;
        return 1;
    }

    const int width = 64;
    const int height = 48;
    auto target = device.createRenderTarget(width, height);
    if (!target || !target->isValid()) {
        std::cerr << "[VulkanTextureTests] FAIL: could not create render target." << std::endl;
        return 1;
    }

    // 2x2 texture (row-major, top-to-bottom): red green / blue yellow.
    const std::vector<unsigned char> texels = {
        255, 0,   0,   255, /* red */    0,   255, 0,   255, /* green */
        0,   0,   255, 255, /* blue */   255, 255, 0,   255, /* yellow */
    };
    auto texture = device.createImageResourceRGBA(2, 2, texels);
    if (!texture || !texture->isValid()) {
        std::cerr << "[VulkanTextureTests] FAIL: could not create texture." << std::endl;
        return 1;
    }
    if (device.resourceStats().imageTextureCount != 1) {
        std::cerr << "[VulkanTextureTests] FAIL: imageTextureCount != 1." << std::endl;
        return 1;
    }

    if (!device.renderTexturedQuad(target, texture)) {
        std::cerr << "[VulkanTextureTests] FAIL: renderTexturedQuad returned false." << std::endl;
        return 1;
    }
    std::vector<unsigned char> pixels;
    if (!device.readPixelsRGBA(width, height, pixels)) {
        std::cerr << "[VulkanTextureTests] FAIL: readback failed." << std::endl;
        return 1;
    }
    if (!pixelIs(pixels, width, 16, 12, 255, 0, 0, 255, "top-left red")) return 1;
    if (!pixelIs(pixels, width, 48, 12, 0, 255, 0, 255, "top-right green")) return 1;
    if (!pixelIs(pixels, width, 16, 36, 0, 0, 255, 255, "bottom-left blue")) return 1;
    if (!pixelIs(pixels, width, 48, 36, 255, 255, 0, 255, "bottom-right yellow")) return 1;

    // Partial update: replace the top-left texel with magenta and re-render.
    const std::vector<unsigned char> magenta = {255, 0, 255, 255};
    if (!device.updateImageResourceRGBA(texture, 0, 0, 1, 1, magenta.data(), false)) {
        std::cerr << "[VulkanTextureTests] FAIL: updateImageResourceRGBA returned false." << std::endl;
        return 1;
    }
    if (!device.renderTexturedQuad(target, texture)) {
        std::cerr << "[VulkanTextureTests] FAIL: re-render after update failed." << std::endl;
        return 1;
    }
    if (!device.readPixelsRGBA(width, height, pixels)) {
        std::cerr << "[VulkanTextureTests] FAIL: readback after update failed." << std::endl;
        return 1;
    }
    if (!pixelIs(pixels, width, 16, 12, 255, 0, 255, 255, "updated top-left magenta")) return 1;
    if (!pixelIs(pixels, width, 48, 36, 255, 255, 0, 255, "bottom-right still yellow")) return 1;

    const std::string deviceName = device.selectedDeviceName();
    auto survivorTexture = device.createImageResourceRGBA(2, 2, texels);
    if (!survivorTexture || !survivorTexture->isValid()) {
        std::cerr << "[VulkanTextureTests] FAIL: could not create survivor texture." << std::endl;
        return 1;
    }
    device.finalizeBackend();
    survivorTexture.reset();

    std::cout << "[VulkanTextureTests] PASS: texture sample + partial update verified on \"" << deviceName << "\"."
              << std::endl;

    texture.reset();
    target.reset();
    return 0;
}

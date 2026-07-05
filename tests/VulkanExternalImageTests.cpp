// External image wrapping: round-trip an owned texture's native VkImage handle
// through wrapExternalImageResource and draw the (non-owning) wrapper. Verifies
// the last IRenderDevice method. Only built with -DWHATSCANVAS_ENABLE_VULKAN=ON.

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

int main()
{
    if (!VulkanRenderDevice::isAvailable()) {
        std::cerr << "[VulkanExternalImageTests] FAIL: Vulkan support was not compiled in." << std::endl;
        return 1;
    }

    VulkanRenderDevice device;
    device.initializeBackend();
    if (!device.isDeviceReady()) {
        std::cerr << "[VulkanExternalImageTests] FAIL: Vulkan device was not created." << std::endl;
        return 1;
    }

    // An owned 2x2 opaque red texture whose VkImage we will borrow.
    std::vector<unsigned char> pixels(2 * 2 * 4);
    for (int i = 0; i < 4; ++i) {
        pixels[i * 4 + 0] = 255;
        pixels[i * 4 + 1] = 0;
        pixels[i * 4 + 2] = 0;
        pixels[i * 4 + 3] = 255;
    }
    SharedImageResource owned = device.createImageResourceRGBA(2, 2, pixels);
    if (!owned || !owned->isValid()) {
        std::cerr << "[VulkanExternalImageTests] FAIL: could not create owned texture." << std::endl;
        return 1;
    }

    // Borrow its native VkImage as a 64-bit handle and wrap it (non-owning).
    ImageResourceHandle handle = device.nativeImageHandle(owned);
    if (!handle.isValid()) {
        std::cerr << "[VulkanExternalImageTests] FAIL: nativeImageHandle returned an invalid handle."
                  << std::endl;
        return 1;
    }
    SharedImageResource wrapped = device.wrapExternalImageResource(handle);
    if (!wrapped || !wrapped->isValid()) {
        std::cerr << "[VulkanExternalImageTests] FAIL: wrapExternalImageResource returned null." << std::endl;
        return 1;
    }

    const int width = 16;
    const int height = 16;
    OffscreenRenderRequest request;
    request.canvasWidth = width;
    request.canvasHeight = height;
    request.targetWidth = width;
    request.targetHeight = height;
    auto target = device.createRenderTarget(width, height);
    if (!target || !target->isValid()) {
        return 1;
    }

    DrawImageData im;
    im.imageResource = wrapped;
    im.x = 0.0f;
    im.y = 0.0f;
    im.width = static_cast<float>(width);
    im.height = static_cast<float>(height);
    im.u0 = 0.0f;
    im.v0 = 0.0f;
    im.u1 = 1.0f;
    im.v1 = 1.0f;
    im.alpha = 1.0f;

    std::vector<std::unique_ptr<Command>> commands;
    commands.push_back(std::make_unique<DrawImageCommand>(im));
    if (!device.executeCommands(target, commands, request)) {
        std::cerr << "[VulkanExternalImageTests] FAIL: executeCommands returned false." << std::endl;
        return 1;
    }
    std::vector<unsigned char> px;
    if (!device.readPixelsRGBA(width, height, px)) {
        return 1;
    }
    const std::size_t center = (8u * width + 8u) * 4u;
    if (std::abs(static_cast<int>(px[center + 0]) - 255) > 4 || px[center + 1] > 4 || px[center + 2] > 4
        || std::abs(static_cast<int>(px[center + 3]) - 255) > 4) {
        std::cerr << "[VulkanExternalImageTests] FAIL: wrapped image center = (" << (int)px[center + 0] << ","
                  << (int)px[center + 1] << "," << (int)px[center + 2] << "," << (int)px[center + 3]
                  << "), expected red." << std::endl;
        return 1;
    }

    std::cout << "[VulkanExternalImageTests] PASS: external image wrap on \"" << device.selectedDeviceName()
              << "\"." << std::endl;

    // Release the wrapper (non-owning) before the owned texture, both before finalize.
    commands.clear();
    im.imageResource.reset();
    target.reset();
    wrapped.reset();
    owned.reset();
    device.finalizeBackend();
    return 0;
}

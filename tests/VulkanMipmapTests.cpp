// Mipmap sampling: a 4x4 texture with a single white corner texel, minified to a
// 1x1 target. With MipmapLinear the sampler reads the averaged mip (~255/16 ~= 16);
// with Linear (no mip) a bilinear tap at the center reads black (~0). This proves
// mip generation + mipmap sampling. Only built with -DWHATSCANVAS_ENABLE_VULKAN=ON.

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

int drawMinified(VulkanRenderDevice &device, const SharedImageResource &img, DrawImageSampling sampling)
{
    OffscreenRenderRequest request;
    request.canvasWidth = 1;
    request.canvasHeight = 1;
    request.targetWidth = 1;
    request.targetHeight = 1;
    auto target = device.createRenderTarget(1, 1);
    if (!target || !target->isValid()) {
        return -1;
    }
    DrawImageData im;
    im.imageResource = img;
    im.x = 0.0f;
    im.y = 0.0f;
    im.width = 1.0f;
    im.height = 1.0f;
    im.u0 = 0.0f;
    im.v0 = 0.0f;
    im.u1 = 1.0f;
    im.v1 = 1.0f;
    im.alpha = 1.0f;
    im.sampling = sampling;
    std::vector<std::unique_ptr<Command>> commands;
    commands.push_back(std::make_unique<DrawImageCommand>(im));
    if (!device.executeCommands(target, commands, request)) {
        return -1;
    }
    std::vector<unsigned char> px;
    if (!device.readPixelsRGBA(1, 1, px) || px.size() < 4) {
        return -1;
    }
    const int r = px[0];
    commands.clear();
    im.imageResource.reset();
    target.reset();
    return r;
}

} // namespace

int main()
{
    if (!VulkanRenderDevice::isAvailable()) {
        std::cerr << "[VulkanMipmapTests] FAIL: Vulkan support was not compiled in." << std::endl;
        return 1;
    }

    VulkanRenderDevice device;
    device.initializeBackend();
    if (!device.isDeviceReady()) {
        std::cerr << "[VulkanMipmapTests] FAIL: Vulkan device was not created." << std::endl;
        return 1;
    }

    // 4x4 opaque texture, black except the top-left texel which is white.
    std::vector<unsigned char> pixels(4 * 4 * 4, 0);
    for (std::size_t i = 0; i < 16; ++i) {
        pixels[i * 4 + 3] = 255; // opaque
    }
    pixels[0] = pixels[1] = pixels[2] = 255; // white corner texel

    SharedImageResource mipped = device.createImageResourceFromImageData(4, 4, 4, pixels.data(),
                                                                         /*generateMipmaps=*/true);
    if (!mipped || !mipped->isValid()) {
        std::cerr << "[VulkanMipmapTests] FAIL: could not create mipped image." << std::endl;
        return 1;
    }

    const int mipR = drawMinified(device, mipped, DrawImageSampling::MipmapLinear);
    const int linR = drawMinified(device, mipped, DrawImageSampling::Linear);
    if (mipR < 0 || linR < 0) {
        std::cerr << "[VulkanMipmapTests] FAIL: draw/readback failed." << std::endl;
        return 1;
    }

    // MipmapLinear averages the 16 texels (~16); Linear bilinear-taps black center (~0).
    if (mipR < 8 || mipR > 28) {
        std::cerr << "[VulkanMipmapTests] FAIL: MipmapLinear red = " << mipR << ", expected ~16." << std::endl;
        return 1;
    }
    if (linR > 4) {
        std::cerr << "[VulkanMipmapTests] FAIL: Linear red = " << linR << ", expected ~0." << std::endl;
        return 1;
    }
    if (mipR <= linR) {
        std::cerr << "[VulkanMipmapTests] FAIL: MipmapLinear (" << mipR << ") should exceed Linear (" << linR
                  << ")." << std::endl;
        return 1;
    }

    std::cout << "[VulkanMipmapTests] PASS: mipmap sampling (mip=" << mipR << ", linear=" << linR << ") on \""
              << device.selectedDeviceName() << "\"." << std::endl;

    mipped.reset();
    device.finalizeBackend();
    return 0;
}

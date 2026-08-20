// Public Metal render-device API contract coverage. This test intentionally
// drives both successful calls and rejected inputs while Metal API Validation
// is enabled by CTest, so resource creation, updates, readback, interop, and
// presentation cannot silently fall out of the validation suite.

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "command/DrawCommand.h"
#include "render/IRenderTarget.h"
#include "render/IRenderer.h"
#include "render/RenderTypes.h"
#include "render/Surface.h"
#include "render/metal/MetalRenderDevice.h"

namespace {

bool expect(bool condition, const std::string &message)
{
    if (!condition) {
        std::cerr << "[MetalApiContractTests] FAIL: " << message << std::endl;
    }
    return condition;
}

class ForeignImageResource final : public ImageResource
{
public:
    bool isValid() const override { return true; }
    void bind(const RenderContext &) const override {}
    bool updateRGBA(int, int, int, int, const unsigned char *, bool) override { return false; }
};

wsc::DrawList texturedDrawList(const SharedImageResource &texture, int sampling = 1, int tileMode = 0)
{
    wsc::DrawPrimitive primitive;
    primitive.kind = wsc::DrawPrimitiveKind::TexturedQuad;
    primitive.texture = texture;
    primitive.sampling = sampling;
    primitive.tileMode = tileMode;
    primitive.useCustomSampler = true;
    return {std::move(primitive)};
}

bool pixelNear(const std::vector<unsigned char> &pixels, int width, int x, int y,
               int r, int g, int b, int a)
{
    const std::size_t offset =
        (static_cast<std::size_t>(y) * static_cast<std::size_t>(width)
         + static_cast<std::size_t>(x)) * 4u;
    if (offset + 3u >= pixels.size()) {
        return false;
    }
    const auto close = [](unsigned char actual, int expected) {
        const int delta = static_cast<int>(actual) - expected;
        return delta >= -3 && delta <= 3;
    };
    return close(pixels[offset], r) && close(pixels[offset + 1u], g)
        && close(pixels[offset + 2u], b) && close(pixels[offset + 3u], a);
}

bool testMetalApiContract()
{
    if (!MetalRenderDevice::isAvailable()) {
        std::cout << "[MetalApiContractTests] SKIP: Metal support not compiled in.\n";
        return true;
    }

    bool ok = true;
    MetalRenderDevice device;
    std::vector<unsigned char> readback = {1, 2, 3, 4};
    std::vector<unsigned char> rgba(2u * 2u * 4u, 0xffu);
    std::vector<unsigned char> alpha(2u * 2u, 0x40u);
    OffscreenRenderRequest request;
    request.canvasWidth = 8;
    request.canvasHeight = 8;
    request.targetWidth = 8;
    request.targetHeight = 8;

    // Pre-initialization rejection paths.
    ok = expect(!device.isDeviceReady(), "device must start uninitialized") && ok;
    ok = expect(device.selectedDeviceName().empty(), "device name must start empty") && ok;
    ok = expect(!device.supportsPresentation(), "presentation must require initialization") && ok;
    ok = expect(device.nativeHandle(0) == 0 && device.nativeHandle(1) == 0
                    && device.nativeHandle(2) == 0,
                "native handles must be empty before initialization") && ok;
    ok = expect(!device.readPixelsRGBA(8, 8, readback) && readback.empty(),
                "readback must reject an uninitialized device") && ok;
    ok = expect(device.createRenderTarget(8, 8) == nullptr,
                "render-target creation must reject an uninitialized device") && ok;
    ok = expect(!device.createImageResourceRGBA(2, 2, rgba),
                "RGBA creation must reject an uninitialized device") && ok;
    ok = expect(!device.createImageResourceAlpha8(2, 2, alpha),
                "Alpha8 creation must reject an uninitialized device") && ok;
    ok = expect(!device.createImageResourceFromImageData(2, 2, 4, rgba.data(), false),
                "generic image creation must reject an uninitialized device") && ok;
    ok = expect(!device.beginGpuFrameTiming(),
                "GPU timing must reject an uninitialized device") && ok;

    NativeSurface invalidSurface;
    SwapchainConfig swapchainConfig;
    ok = expect(device.createSwapchain(invalidSurface, swapchainConfig) == nullptr,
                "swapchain creation must reject an uninitialized device") && ok;

    device.initializeBackend();
    device.initializeBackend();
    if (!expect(device.isDeviceReady(), "Metal device initialization must succeed")) {
        return false;
    }
    ok = expect(!device.selectedDeviceName().empty(), "selected Metal device must have a name") && ok;
    ok = expect(device.nativeHandle(0) != 0 && device.nativeHandle(1) != 0,
                "Metal device and command queue handles must be exposed") && ok;
    ok = expect(device.nativeHandle(2) == 0 && device.nativeHandle(-1) == 0
                    && device.nativeHandle(99) == 0,
                "unset texture and unknown native-handle selectors must return zero") && ok;
    ok = expect(device.usesDeviceCommandExecution(),
                "Metal must advertise device command execution") && ok;
    ok = expect(device.supportsPresentation(),
                "initialized Metal device must advertise presentation") && ok;
    BackendRenderTarget unsupportedTarget;
    ok = expect(!device.wrapBackendRenderTarget(unsupportedTarget),
                "Metal must explicitly reject unsupported backend target wrapping") && ok;

    // Invalid dimensions, short inputs, null data, and unsupported channel counts.
    ok = expect(device.createRenderTarget(0, 8) == nullptr
                    && device.createRenderTarget(8, -1) == nullptr,
                "render targets must reject non-positive dimensions") && ok;
    ok = expect(!device.createImageResourceRGBA(0, 2, rgba)
                    && !device.createImageResourceRGBA(2, 2, std::vector<unsigned char>(15u)),
                "RGBA creation must reject invalid dimensions and short data") && ok;
    ok = expect(!device.createImageResourceAlpha8(2, 0, alpha)
                    && !device.createImageResourceAlpha8(2, 2, std::vector<unsigned char>(3u)),
                "Alpha8 creation must reject invalid dimensions and short data") && ok;
    ok = expect(!device.createImageResourceFromImageData(2, 2, 4, nullptr, false)
                    && !device.createImageResourceFromImageData(2, 2, 2, rgba.data(), false),
                "generic image creation must reject null data and unsupported channels") && ok;

    // Exercise every supported upload format.
    rgba = {
        255, 0, 0, 255,   0, 255, 0, 255,
        0, 0, 255, 255,   255, 255, 0, 255,
    };
    const std::vector<unsigned char> rgb = {
        20, 40, 60,   80, 100, 120,
        140, 160, 180, 200, 220, 240,
    };
    alpha = {0, 64, 128, 255};
    SharedImageResource rgbaImage = device.createImageResourceRGBA(2, 2, rgba);
    SharedImageResource rgbaGeneric =
        device.createImageResourceFromImageData(2, 2, 4, rgba.data(), false);
    SharedImageResource rgbGeneric =
        device.createImageResourceFromImageData(2, 2, 3, rgb.data(), false);
    SharedImageResource alphaImage = device.createImageResourceAlpha8(2, 2, alpha);
    SharedImageResource alphaGeneric =
        device.createImageResourceFromImageData(2, 2, 1, alpha.data(), false);
    SharedImageResource mipImage =
        device.createImageResourceFromImageData(2, 2, 4, rgba.data(), true);
    ok = expect(rgbaImage && rgbaGeneric && rgbGeneric && alphaImage && alphaGeneric && mipImage,
                "all supported image upload formats must succeed") && ok;
    ok = expect(alphaImage->isAlphaOnly() && alphaGeneric->isAlphaOnly()
                    && !rgbaImage->isAlphaOnly() && !rgbGeneric->isAlphaOnly(),
                "Alpha8 and color resource metadata must stay distinct") && ok;

    const std::vector<unsigned char> magenta = {255, 0, 255, 255};
    ok = expect(!device.updateImageResourceRGBA({}, 0, 0, 1, 1, magenta.data(), false)
                    && !device.updateImageResourceRGBA(rgbaImage, 0, 0, 1, 1, nullptr, false)
                    && !device.updateImageResourceRGBA(alphaImage, 0, 0, 1, 1, magenta.data(), false)
                    && !device.updateImageResourceRGBA(rgbaImage, -1, 0, 1, 1, magenta.data(), false)
                    && !device.updateImageResourceRGBA(rgbaImage, 2, 2, 1, 1, magenta.data(), false),
                "RGBA updates must reject invalid resource types, data, and regions") && ok;
    ok = expect(device.updateImageResourceRGBA(rgbaImage, 0, 0, 1, 1, magenta.data(), false),
                "RGBA partial update without mipmap regeneration must succeed") && ok;
    ok = expect(device.updateImageResourceRGBA(mipImage, 0, 0, 1, 1, magenta.data(), true),
                "RGBA partial update with mipmap regeneration must succeed") && ok;

    const unsigned char opaque = 255;
    ok = expect(!device.updateImageResourceAlpha8({}, 0, 0, 1, 1, &opaque)
                    && !device.updateImageResourceAlpha8(alphaImage, 0, 0, 1, 1, nullptr)
                    && !device.updateImageResourceAlpha8(rgbaImage, 0, 0, 1, 1, &opaque)
                    && !device.updateImageResourceAlpha8(alphaImage, 0, 0, 0, 1, &opaque)
                    && !device.updateImageResourceAlpha8(alphaImage, 0, 0, 1, 0, &opaque)
                    && !device.updateImageResourceAlpha8(alphaImage, -1, 0, 1, 1, &opaque)
                    && !device.updateImageResourceAlpha8(alphaImage, 2, 2, 1, 1, &opaque),
                "Alpha8 updates must reject invalid resource types, data, dimensions, and regions") && ok;
    std::vector<unsigned char> opaqueAlpha(4u, 255u);
    ok = expect(device.updateImageResourceAlpha8(alphaImage, 0, 0, 2, 2, opaqueAlpha.data()),
                "Alpha8 full update must succeed") && ok;

    // Owned texture handles must round-trip; foreign/empty resources must not.
    ForeignImageResource foreign;
    SharedImageResource foreignShared(&foreign, [](ImageResource *) {});
    const ImageResourceHandle rgbaHandle = device.nativeImageHandle(rgbaImage);
    ok = expect(rgbaHandle.isValid(), "owned Metal texture must expose a native handle") && ok;
    ok = expect(!device.nativeImageHandle({}).isValid()
                    && !device.nativeImageHandle(foreignShared).isValid(),
                "empty and foreign resources must not expose Metal handles") && ok;
    ok = expect(!device.wrapExternalImageResource({}),
                "empty external image handles must be rejected") && ok;
    SharedImageResource wrapped = device.wrapExternalImageResource(rgbaHandle);
    ok = expect(wrapped && wrapped->isValid()
                    && device.nativeImageHandle(wrapped).value == rgbaHandle.value,
                "same-device Metal texture handles must round-trip without copying") && ok;

    ClipMaskPath shortClip;
    shortClip.points = {0.0f, 0.0f, 1.0f, 0.0f};
    ClipMaskPath validClip;
    validClip.points = {0.0f, 0.0f, 8.0f, 0.0f, 0.0f, 8.0f};
    validClip.coverage = {1.0f, 1.0f, 1.0f};
    ok = expect(!device.createClipMaskResource(shortClip),
                "clip masks must reject fewer than three vertices") && ok;
    SharedClipMaskResource clip = device.createClipMaskResource(validClip);
    ok = expect(clip && clip->isValid(), "valid clip-mask geometry must be accepted") && ok;

    auto target = device.createRenderTarget(8, 8);
    ok = expect(target && target->isValid(), "valid render target must be created") && ok;
    ok = expect(!device.executeDrawList(std::unique_ptr<IRenderTarget>{}, {}),
                "draw-list execution must reject a null target") && ok;
    ok = expect(device.executeDrawList(target, {}), "empty draw lists must be accepted") && ok;
    ok = expect(!device.executeCommands(std::unique_ptr<IRenderTarget>{}, {}, request),
                "command execution must reject a null target") && ok;

    wsc::DrawList alphaDraw = texturedDrawList(alphaImage);
    ok = expect(device.executeDrawList(target, alphaDraw),
                "Alpha8 texture draw must succeed under API Validation") && ok;
    ok = expect(device.nativeHandle(2) != 0,
                "the last rendered texture must be exposed for presentation") && ok;
    ok = expect(!device.readPixelsRGBA(7, 8, readback) && readback.empty(),
                "readback must reject dimensions that differ from the rendered target") && ok;
    ok = expect(device.readPixelsRGBA(8, 8, readback), "valid readback must succeed") && ok;
    ok = expect(pixelNear(readback, 8, 4, 4, 255, 255, 255, 255),
                "updated Alpha8 texture must render as opaque white coverage") && ok;
    ok = expect(device.lastExecutionDrawCallCount() == 1
                    && device.lastExecutionMergedBatchCount() == 1
                    && device.lastCompiledPacketCount() == 1
                    && device.lastCompiledVertexBytes() > 0,
                "draw compilation statistics must describe the executed primitive") && ok;
    (void)device.lastCompiledIndexBytes();
    (void)device.lastFrameCompileCpuTimeNs();

    // Force every sampler descriptor combination used by the public DrawList
    // contract through a real draw. This catches unsupported address/filter
    // settings and missing texture/sampler bindings under API Validation.
    for (int tileMode = 0; tileMode <= 3; ++tileMode) {
        wsc::DrawList sampled = texturedDrawList(rgbaImage, 0, tileMode);
        ok = expect(device.executeDrawList(target, sampled),
                    "linear sampler tile mode must execute") && ok;
    }
    wsc::DrawList nearest = texturedDrawList(rgbaImage, 1, 0);
    wsc::DrawList mipLinear = texturedDrawList(mipImage, 2, 0);
    ok = expect(device.executeDrawList(target, nearest),
                "nearest sampler must execute") && ok;
    ok = expect(device.executeDrawList(target, mipLinear),
                "mipmap-linear sampler must execute") && ok;

    SharedImageResource rendered = device.renderCommandsToImageResource({}, request);
    ok = expect(rendered && rendered->isValid(),
                "empty command streams must still produce a valid image resource") && ok;
    ok = expect(device.executeCommands(target, {}, request),
                "empty command streams must execute successfully") && ok;

    // A CAMetalLayer is sufficient to validate swapchain construction,
    // acquisition, resizing, and the full render-to-drawable presentation path.
    CAMetalLayer *layer = [CAMetalLayer layer];
    layer.drawableSize = CGSizeMake(8.0, 8.0);
    NativeSurface metalSurface;
    metalSurface.platform = NativeSurface::Platform::Cocoa;
    metalSurface.window = (__bridge void *)layer;
    invalidSurface.platform = NativeSurface::Platform::Android;
    invalidSurface.window = (__bridge void *)layer;
    ok = expect(device.createSwapchain(invalidSurface, swapchainConfig) == nullptr,
                "swapchain creation must reject non-Cocoa surfaces") && ok;
    std::unique_ptr<ISwapchain> swapchain = device.createSwapchain(metalSurface, swapchainConfig);
    ok = expect(swapchain != nullptr, "CAMetalLayer swapchain creation must succeed") && ok;
    if (swapchain) {
        AcquiredImage acquired = swapchain->acquire();
        ok = expect(acquired.valid && acquired.width == 8 && acquired.height == 8,
                    "swapchain acquire must report the drawable size") && ok;
        swapchain->resize(12, 10);
        acquired = swapchain->acquire();
        ok = expect(acquired.valid && acquired.width == 12 && acquired.height == 10,
                    "swapchain resize must update acquisition dimensions") && ok;
        ok = expect(swapchain->present(),
                    "swapchain must present the last rendered Metal texture") && ok;
    }

    RenderResourceStats stats = device.resourceStats();
    ok = expect(stats.imageTextureCount >= 8 && stats.renderTargetCount >= 2,
                "resource statistics must include images and render targets") && ok;

    SharedImageResource retainedAcrossFinalize = rgbaImage;
    swapchain.reset();
    target.reset();
    rendered.reset();
    wrapped.reset();
    mipImage.reset();
    alphaGeneric.reset();
    alphaImage.reset();
    rgbGeneric.reset();
    rgbaGeneric.reset();
    rgbaImage.reset();
    clip.reset();
    device.finalizeBackend();
    device.finalizeBackend();
    ok = expect(!device.isDeviceReady() && !device.supportsPresentation()
                    && device.nativeHandle(0) == 0,
                "finalization must clear device readiness and native handles") && ok;
    ok = expect(!device.readPixelsRGBA(8, 8, readback) && readback.empty(),
                "post-finalization readback must fail cleanly") && ok;
    ok = expect(device.selectedDeviceName().empty(),
                "finalization must clear the selected device name") && ok;
    ok = expect(!device.updateImageResourceRGBA(
                    retainedAcrossFinalize, 0, 0, 1, 1, magenta.data(), false),
                "image updates must reject a finalized backend") && ok;
    retainedAcrossFinalize.reset();

    // Background recovery reuses the render-device object. A fresh backend
    // must never expose the old device's last presentation texture.
    device.initializeBackend();
    ok = expect(device.isDeviceReady() && device.nativeHandle(0) != 0
                    && device.nativeHandle(2) == 0,
                "reinitialized backend must not retain the previous presentation texture") && ok;
    device.abandonBackend();
    ok = expect(!device.isDeviceReady() && device.nativeHandle(0) == 0,
                "abandonBackend must tear down the Metal backend") && ok;
    return ok;
}

} // namespace

int main()
{
    return testMetalApiContract() ? 0 : 1;
}

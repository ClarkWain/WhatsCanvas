// Metal wrap-external-texture test. Creates an MTLTexture on the same
// MTLDevice that a Metal Canvas holds, uploads pixels manually, feeds the
// texture pointer through Image::wrapExternalMetalTexture, and asserts the
// wrapped image draws correctly on the Canvas.
//
// Exercises Canvas::metalDevice() + wrapExternalMetalTexture() +
// MetalRenderDevice::wrapExternalImageResource end to end.

#import <Metal/Metal.h>

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "wsc/wsc.h"

using namespace wsc;

namespace {

bool expect(bool condition, const std::string &message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << std::endl;
    }
    return condition;
}

bool testMetalWrapExternalMTLTexture()
{
    if (!Canvas::isBackendAvailable(Canvas::Backend::Metal)) {
        return true;
    }
    const int w = 64;
    const int h = 64;
    auto canvas = Canvas::create(Canvas::Backend::Metal, w, h);
    if (!expect(canvas != nullptr, "create(Metal) should succeed")) {
        return false;
    }
    canvas->initializeContext();

    id<MTLDevice> device = (__bridge id<MTLDevice>)(canvas->metalDevice());
    if (!expect(device != nil, "Metal device should be reachable via Canvas")) {
        return false;
    }

    const int iw = 32;
    const int ih = 32;
    MTLTextureDescriptor *desc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                           width:iw
                                                          height:ih
                                                       mipmapped:NO];
    desc.usage = MTLTextureUsageShaderRead;
    desc.storageMode = MTLStorageModeShared;
    id<MTLTexture> tex = [device newTextureWithDescriptor:desc];
    if (!expect(tex != nil, "should be able to allocate an MTLTexture")) {
        return false;
    }
    std::vector<unsigned char> pixels(static_cast<std::size_t>(iw) * ih * 4u);
    for (std::size_t i = 0; i < pixels.size() / 4; ++i) {
        pixels[i * 4 + 0] = 255;
        pixels[i * 4 + 1] = 20;
        pixels[i * 4 + 2] = 10;
        pixels[i * 4 + 3] = 255;
    }
    MTLRegion region = MTLRegionMake2D(0, 0, iw, ih);
    [tex replaceRegion:region mipmapLevel:0 withBytes:pixels.data()
           bytesPerRow:static_cast<NSUInteger>(iw) * 4];

    // Wrap the real MTLTexture through the public 64-bit-safe API, then mutate
    // it after wrapping. Seeing the new color proves the Image samples the
    // external texture instead of a copied upload.
    Image img;
    if (!expect(img.wrapExternalMetalTexture(*canvas, (__bridge void *)tex, iw, ih),
                "wrapExternalMetalTexture should accept a same-device MTLTexture")) {
        return false;
    }
    for (std::size_t i = 0; i < pixels.size() / 4; ++i) {
        pixels[i * 4 + 0] = 30;
        pixels[i * 4 + 1] = 200;
        pixels[i * 4 + 2] = 255;
        pixels[i * 4 + 3] = 255;
    }
    [tex replaceRegion:region mipmapLevel:0 withBytes:pixels.data()
           bytesPerRow:static_cast<NSUInteger>(iw) * 4];

    canvas->beginFrame();
    Paint imgPaint;
    imgPaint.setColor(Color(255, 255, 255, 255));
    canvas->drawImage(img, RectF(16.0f, 16.0f, 32.0f, 32.0f), imgPaint);
    canvas->endFrame();

    std::vector<unsigned char> readback;
    if (!expect(canvas->readPixelsRGBA(readback), "readPixels should succeed")) {
        return false;
    }
    const std::size_t idx = (static_cast<std::size_t>(h / 2) * w + w / 2) * 4u;
    bool ok = expect(readback[idx + 0] < 80 && readback[idx + 1] > 150 && readback[idx + 2] > 200,
                     "post-wrap MTLTexture mutations should be sampled without a copy");
    return ok;
}

} // namespace

int main()
{
    return testMetalWrapExternalMTLTexture() ? 0 : 1;
}

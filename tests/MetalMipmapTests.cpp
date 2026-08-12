// Metal mipmap test. Loads an 8x8 image with generateMipmaps=true and draws
// it downscaled to 1x1 through the mipmap-linear sampling filter, then reads
// back the resulting pixel. The image is a bright green source, so the
// downscaled pixel must still be green — the check catches broken mipmap
// generation (which would leave the deeper levels transparent black, so the
// mipmap-linear sample would land on the empty level and read zero).

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

bool testMetalMipmapGenerate()
{
    if (!Canvas::isBackendAvailable(Canvas::Backend::Metal)) {
        return true;
    }
    const int w = 32;
    const int h = 32;
    auto canvas = Canvas::create(Canvas::Backend::Metal, w, h);
    if (!expect(canvas != nullptr, "create(Metal) should succeed")) {
        return false;
    }
    canvas->initializeContext();

    // Build a solid green 16x16 source; ask Image to generate mipmaps.
    const int iw = 16;
    const int ih = 16;
    std::vector<unsigned char> data(static_cast<std::size_t>(iw) * ih * 4u, 0);
    for (std::size_t i = 0; i < static_cast<std::size_t>(iw) * ih; ++i) {
        data[i * 4 + 0] = 0;
        data[i * 4 + 1] = 220;
        data[i * 4 + 2] = 0;
        data[i * 4 + 3] = 255;
    }
    Image img;
    if (!expect(img.loadFromRGBA(*canvas, data.data(), iw, ih, /*generateMipmaps=*/true),
                "loadFromRGBA with mipmaps should succeed")) {
        return false;
    }
    if (!expect(img.hasMipmaps(), "Image should report mipmaps present after generation")) {
        return false;
    }

    // Draw the 16x16 source into a small 4x4 destination; the sampler falls
    // onto the appropriate mip level via mipmap-linear filtering.
    canvas->beginFrame();
    Paint p;
    p.setColor(Color(255, 255, 255, 255));
    p.setImageSampling(Paint::ImageSampling::MIPMAP_LINEAR);
    canvas->drawImage(img, RectF(8.0f, 8.0f, 12.0f, 12.0f), p);
    canvas->endFrame();

    std::vector<unsigned char> pixels;
    if (!expect(canvas->readPixelsRGBA(pixels), "readPixels should succeed")) {
        return false;
    }
    const std::size_t idx = (static_cast<std::size_t>(10) * w + 10) * 4u;
    bool ok = expect(pixels[idx + 1] > 150 && pixels[idx + 3] > 200,
                     "downscaled mipmap sample should still be green");
    return ok;
}

} // namespace

int main()
{
    return testMetalMipmapGenerate() ? 0 : 1;
}

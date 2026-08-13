// End-to-end Metal tests for higher-level draw primitives (gradients and
// images). Only exercised on Apple hosts where the Metal backend is available;
// the test contract mirrors MetalBackendSelectionTests when Metal is not built.

#include <cmath>
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

bool testMetalLinearGradient()
{
    if (!Canvas::isBackendAvailable(Canvas::Backend::Metal)) {
        std::cout << "Metal unavailable in this build: skipping gradient test.\n";
        return true;
    }

    const int w = 64;
    const int h = 64;
    auto canvas = Canvas::create(Canvas::Backend::Metal, w, h);
    if (!expect(canvas != nullptr, "create(Metal) should succeed")) {
        return false;
    }
    canvas->initializeContext();
    canvas->beginFrame();

    Paint fill;
    fill.setStyle(Paint::Style::FILL);
    fill.setAntiAlias(false);
    // Horizontal linear gradient: red (left) → blue (right).
    fill.setLinearGradient(0.0f, 32.0f, 64.0f, 32.0f, Color(255, 0, 0, 255), Color(0, 0, 255, 255));
    canvas->drawRect(RectF(0.0f, 0.0f, 64.0f, 64.0f), fill);
    canvas->endFrame();

    std::vector<unsigned char> pixels;
    if (!expect(canvas->readPixelsRGBA(pixels), "readPixelsRGBA should succeed")) {
        return false;
    }
    auto at = [&](int x, int y) { return &pixels[(static_cast<std::size_t>(y) * w + x) * 4u]; };
    const unsigned char *left = at(2, 32);
    const unsigned char *right = at(61, 32);
    const unsigned char *mid = at(32, 32);

    bool ok = expect(left[0] > 220 && left[2] < 30, "gradient start should be red");
    ok = expect(right[2] > 220 && right[0] < 30, "gradient end should be blue") && ok;
    ok = expect(std::abs(int(mid[0]) - int(mid[2])) < 60,
                "gradient midpoint should blend red and blue roughly evenly") && ok;
    return ok;
}

bool testMetalDrawImage()
{
    if (!Canvas::isBackendAvailable(Canvas::Backend::Metal)) {
        std::cout << "Metal unavailable in this build: skipping image test.\n";
        return true;
    }

    const int w = 64;
    const int h = 64;
    auto canvas = Canvas::create(Canvas::Backend::Metal, w, h);
    if (!expect(canvas != nullptr, "create(Metal) should succeed")) {
        return false;
    }
    canvas->initializeContext();

    // Build a 32x32 green source image via ImageInfo.
    const int iw = 32;
    const int ih = 32;
    std::vector<unsigned char> data(static_cast<std::size_t>(iw) * ih * 4u, 0);
    for (std::size_t i = 0; i < static_cast<std::size_t>(iw) * ih; ++i) {
        data[i * 4 + 0] = 0;
        data[i * 4 + 1] = 200;
        data[i * 4 + 2] = 0;
        data[i * 4 + 3] = 255;
    }
    Image img;
    if (!expect(img.loadFromRGBA(*canvas, data.data(), iw, ih, /*generateMipmaps=*/false),
                "constructed image should load")) {
        return false;
    }
    if (!expect(img.isTextureValid(), "loaded image should be valid")) {
        return false;
    }

    canvas->beginFrame();
    Paint imgPaint;
    imgPaint.setColor(Color(255, 255, 255, 255));
    canvas->drawImage(img, 16.0f, 16.0f, imgPaint);
    canvas->endFrame();

    std::vector<unsigned char> pixels;
    if (!expect(canvas->readPixelsRGBA(pixels), "readPixelsRGBA should succeed")) {
        return false;
    }
    auto at = [&](int x, int y) { return &pixels[(static_cast<std::size_t>(y) * w + x) * 4u]; };
    const unsigned char *center = at(32, 32);
    bool ok = expect(center[1] > 150 && center[0] < 60 && center[2] < 60 && center[3] > 200,
                     "image interior should be green");
    const unsigned char *corner = at(2, 2);
    ok = expect(corner[3] < 20, "outside the image rectangle should stay transparent") && ok;
    return ok;
}

} // namespace

int main()
{
    bool ok = true;
    ok = testMetalLinearGradient() && ok;
    ok = testMetalDrawImage() && ok;
    return ok ? 0 : 1;
}

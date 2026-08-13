// Metal tests for the two Textured pipeline features added in stage 2c:
// rounded-rect corner masking on drawImageRounded, and per-image color
// matrix (paint setColorMatrix). Both feed the same textured pipeline via
// prim.roundedRadius / prim.hasColorMatrix, so they share the same test file.

#include <array>
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

// drawImageRounded must round the corners of the drawn image: the near-corner
// pixel of the source image should be masked out (transparent), while the
// centre stays fully opaque.
bool testMetalDrawImageRounded()
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

    const int iw = 32;
    const int ih = 32;
    std::vector<unsigned char> data(static_cast<std::size_t>(iw) * ih * 4u, 0);
    for (std::size_t i = 0; i < static_cast<std::size_t>(iw) * ih; ++i) {
        data[i * 4 + 0] = 0;
        data[i * 4 + 1] = 220;
        data[i * 4 + 2] = 0;
        data[i * 4 + 3] = 255;
    }
    Image img;
    if (!expect(img.loadFromRGBA(*canvas, data.data(), iw, ih, false), "image load should succeed")) {
        return false;
    }

    canvas->beginFrame();
    Paint imgPaint;
    imgPaint.setColor(Color(255, 255, 255, 255));
    canvas->drawImageRounded(img, RectF(16.0f, 16.0f, 48.0f, 48.0f), 10.0f, imgPaint);
    canvas->endFrame();

    std::vector<unsigned char> pixels;
    if (!expect(canvas->readPixelsRGBA(pixels), "readPixels should succeed")) {
        return false;
    }
    auto at = [&](int x, int y) { return &pixels[(static_cast<std::size_t>(y) * w + x) * 4u]; };

    // Centre must be fully opaque green (image passthrough).
    const unsigned char *centre = at(32, 32);
    bool ok = expect(centre[1] > 180 && centre[3] > 200,
                     "rounded image centre should stay opaque green");

    // Top-left corner of the destination rect (16,16). With radius 10 the
    // corner should be clipped to zero alpha.
    const unsigned char *corner = at(17, 17);
    ok = expect(corner[3] < 40, "rounded corner should be masked out (near transparent)") && ok;
    return ok;
}

// setColorMatrix that inverts the R and B channels of an image should flip a
// blue source image to red on the output.
bool testMetalDrawImageColorMatrix()
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

    const int iw = 32;
    const int ih = 32;
    std::vector<unsigned char> data(static_cast<std::size_t>(iw) * ih * 4u, 0);
    for (std::size_t i = 0; i < static_cast<std::size_t>(iw) * ih; ++i) {
        data[i * 4 + 0] = 0;
        data[i * 4 + 1] = 0;
        data[i * 4 + 2] = 240;
        data[i * 4 + 3] = 255;
    }
    Image img;
    if (!expect(img.loadFromRGBA(*canvas, data.data(), iw, ih, false), "image load should succeed")) {
        return false;
    }

    canvas->beginFrame();
    Paint imgPaint;
    imgPaint.setColor(Color(255, 255, 255, 255));
    // Swap R and B; leave G and A unchanged. Matrix rows are r,g,b,a,offset.
    std::array<float, 20> swapRB = {
        0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
    };
    imgPaint.setColorMatrix(swapRB);
    canvas->drawImage(img, RectF(16.0f, 16.0f, 48.0f, 48.0f), imgPaint);
    canvas->endFrame();

    std::vector<unsigned char> pixels;
    if (!expect(canvas->readPixelsRGBA(pixels), "readPixels should succeed")) {
        return false;
    }
    auto at = [&](int x, int y) { return &pixels[(static_cast<std::size_t>(y) * w + x) * 4u]; };

    // Blue in the source should be swapped to red in the readback.
    const unsigned char *centre = at(32, 32);
    bool ok = expect(centre[0] > 200 && centre[2] < 40 && centre[3] > 200,
                     "colour-matrix swap should render blue source as red");
    return ok;
}

} // namespace

int main()
{
    bool ok = true;
    ok = testMetalDrawImageRounded() && ok;
    ok = testMetalDrawImageColorMatrix() && ok;
    return ok ? 0 : 1;
}

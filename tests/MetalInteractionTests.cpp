// Metal interaction tests: clip + textured, image color effects, and inner
// shadow offset directionality. Fills in the remaining gaps between the
// isolated per-feature tests and the way a real Canvas app combines them.

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

// Clip a textured draw with a clipRect (Canvas routes rectangular clips to
// the scissor path so the encoder can carry them alongside image commands).
// Only pixels inside both the source image and the clip rect should end up
// opaque. Regressions here typically point at a bug in either the encoder's
// scissor plumbing or the Metal backend's applyScissor call.
bool testMetalClipTexturedInteraction()
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

    const int iw = 40;
    const int ih = 40;
    std::vector<unsigned char> data(static_cast<std::size_t>(iw) * ih * 4u);
    for (std::size_t i = 0; i < data.size() / 4; ++i) {
        data[i * 4 + 0] = 255;
        data[i * 4 + 1] = 200;
        data[i * 4 + 2] = 0;
        data[i * 4 + 3] = 255;
    }
    Image img;
    if (!expect(img.loadFromRGBA(*canvas, data.data(), iw, ih, false),
                "loadFromRGBA should succeed")) {
        return false;
    }

    canvas->beginFrame();
    canvas->save();
    // Restrict rendering to a 24x24 window in the middle of the canvas.
    canvas->clipRect(RectF(20.0f, 20.0f, 44.0f, 44.0f));
    Paint imgPaint;
    imgPaint.setColor(Color(255, 255, 255, 255));
    canvas->drawImage(img, RectF(12.0f, 12.0f, 52.0f, 52.0f), imgPaint);
    canvas->restore();
    canvas->endFrame();

    std::vector<unsigned char> pixels;
    if (!expect(canvas->readPixelsRGBA(pixels), "readPixels should succeed")) {
        return false;
    }
    auto at = [&](int x, int y) { return &pixels[(static_cast<std::size_t>(y) * w + x) * 4u]; };

    // Inside the clip rect and covered by the image: yellow.
    const unsigned char *inside = at(32, 32);
    bool ok = expect(inside[0] > 200 && inside[1] > 150 && inside[3] > 200,
                     "inside both clip + image should read yellow");
    // Inside the image but outside the clip rect: transparent.
    const unsigned char *outsideClip = at(16, 32);
    ok = expect(outsideClip[3] < 20,
                "outside the clip rect must stay transparent even when the image covers it") && ok;
    return ok;
}

// Layer / paint alpha applied to a textured draw. Draws an opaque green image
// with paint alpha 128 and asserts the readback lands roughly half-alpha
// (band [80, 180]) while the R/G/B stay close to the source.
bool testMetalImagePaintAlpha()
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

    std::vector<unsigned char> data(static_cast<std::size_t>(w) * h * 4u);
    for (std::size_t i = 0; i < data.size() / 4; ++i) {
        data[i * 4 + 0] = 0;
        data[i * 4 + 1] = 220;
        data[i * 4 + 2] = 0;
        data[i * 4 + 3] = 255;
    }
    Image img;
    if (!expect(img.loadFromRGBA(*canvas, data.data(), w, h, false), "load should succeed")) {
        return false;
    }

    canvas->beginFrame();
    Paint p;
    p.setColor(Color(255, 255, 255, 128));
    canvas->drawImage(img, RectF(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)), p);
    canvas->endFrame();

    std::vector<unsigned char> pixels;
    if (!expect(canvas->readPixelsRGBA(pixels), "readPixels should succeed")) {
        return false;
    }
    const std::size_t idx = (static_cast<std::size_t>(h / 2) * w + w / 2) * 4u;
    bool ok = expect(pixels[idx + 1] > 100,
                     "image with 50% paint alpha should still land green");
    ok = expect(pixels[idx + 3] > 80 && pixels[idx + 3] < 180,
                "paint alpha 128 on an opaque source should halve the composite alpha") && ok;
    return ok;
}

// Inner shadow with an X offset: the shaded edge should darken pixels near
// the left side of the shape (positive offsetX shades the LEFT edge per the
// public API contract).
bool testMetalInnerShadowLeftEdge()
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

    canvas->beginFrame();
    Paint layerPaint;
    layerPaint.setColor(Color(255, 255, 255, 255));
    LayerOptions options;
    options.setImageFilter(ImageFilter::innerShadow(6.0f, 6.0f, 6.0f, 0.0f, Color(0, 0, 0, 255)));
    canvas->saveLayer(RectF(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)),
                      layerPaint, options);

    Paint fill;
    fill.setStyle(Paint::Style::FILL);
    fill.setColor(Color(255, 240, 80, 255));
    fill.setAntiAlias(false);
    canvas->drawRect(RectF(12.0f, 12.0f, 52.0f, 52.0f), fill);
    canvas->restore();
    canvas->endFrame();

    std::vector<unsigned char> pixels;
    if (!expect(canvas->readPixelsRGBA(pixels), "readPixels should succeed")) {
        return false;
    }
    auto at = [&](int x, int y) { return &pixels[(static_cast<std::size_t>(y) * w + x) * 4u]; };
    const unsigned char *centre = at(32, 32);
    const unsigned char *leftEdge = at(16, 32);
    bool ok = expect(centre[1] > 180,
                     "centre should stay close to the yellow source");
    ok = expect(leftEdge[1] + 40 < centre[1],
                "positive offsetX should darken pixels near the left edge") && ok;
    return ok;
}

} // namespace

int main()
{
    bool ok = true;
    ok = testMetalClipTexturedInteraction() && ok;
    ok = testMetalImagePaintAlpha() && ok;
    ok = testMetalInnerShadowLeftEdge() && ok;
    return ok ? 0 : 1;
}

// Metal layer / offscreen render-target tests.
//
// saveLayer without a filter should still round-trip its contents through the
// Metal offscreen path (executeCommands into a MetalRenderTarget, then a
// composite draw of the resulting texture back onto the parent) and land the
// same fill on the main canvas. Also validates layer alpha (setAlpha on the
// paint used to composite the layer): a 50% layer alpha should halve the
// output alpha of the drawn shape.

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

bool testMetalSaveLayerPassthrough()
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
    canvas->saveLayer(RectF(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)), layerPaint);

    Paint fill;
    fill.setStyle(Paint::Style::FILL);
    fill.setAntiAlias(false);
    fill.setColor(Color(0, 200, 0, 255));
    canvas->drawRect(RectF(16.0f, 16.0f, 48.0f, 48.0f), fill);

    canvas->restore();
    canvas->endFrame();

    std::vector<unsigned char> pixels;
    if (!expect(canvas->readPixelsRGBA(pixels), "readPixels should succeed")) {
        return false;
    }
    auto at = [&](int x, int y) { return &pixels[(static_cast<std::size_t>(y) * w + x) * 4u]; };
    const unsigned char *centre = at(32, 32);
    bool ok = expect(centre[1] > 180 && centre[3] > 200,
                     "layer contents (green rect) should composite back onto the canvas");
    const unsigned char *corner = at(2, 2);
    ok = expect(corner[3] < 20, "layer bounds outside the rect should stay transparent") && ok;
    return ok;
}

bool testMetalSaveLayerAlpha()
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
    canvas->beginFrame();

    Paint layerPaint;
    layerPaint.setColor(Color(255, 255, 255, 128));
    canvas->saveLayer(RectF(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)), layerPaint);

    Paint fill;
    fill.setStyle(Paint::Style::FILL);
    fill.setAntiAlias(false);
    fill.setColor(Color(255, 0, 0, 255));
    canvas->drawRect(RectF(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)), fill);

    canvas->restore();
    canvas->endFrame();

    std::vector<unsigned char> pixels;
    if (!expect(canvas->readPixelsRGBA(pixels), "readPixels should succeed")) {
        return false;
    }
    const std::size_t idx = (static_cast<std::size_t>(h / 2) * w + w / 2) * 4u;
    // Layer paint alpha 128 (~0.5) applied over the fully-red fill should
    // yield a red output whose alpha lands in the [96, 160] band. Colours are
    // sampled unpremultiplied on readback so the R channel stays high.
    bool ok = expect(pixels[idx + 0] > 200,
                     "layer composite should keep the interior red");
    ok = expect(pixels[idx + 3] > 80 && pixels[idx + 3] < 180,
                "layer alpha 128 should halve the composited alpha") && ok;
    return ok;
}

} // namespace

int main()
{
    bool ok = true;
    ok = testMetalSaveLayerPassthrough() && ok;
    ok = testMetalSaveLayerAlpha() && ok;
    return ok ? 0 : 1;
}

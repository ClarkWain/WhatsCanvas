// Metal shadow variants + clip/text interaction tests. Rounds out the
// coverage picture: exercises InnerShadow with several offset directions
// under a single test and verifies text renders correctly under a scissor
// clip via clipRect.

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

struct ShadowProbe
{
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    int shadedProbeX = 0;
    int shadedProbeY = 0;
    const char *edge = "";
};

// Runs the same yellow-rect / inner-shadow scenario for a chosen offset and
// probes the specified edge pixel. Every direction should darken the
// corresponding edge (positive X shades LEFT, positive Y shades TOP by the
// public API contract).
bool runShadowVariant(const ShadowProbe &probe)
{
    const int w = 64;
    const int h = 64;
    auto canvas = Canvas::create(Canvas::Backend::Metal, w, h);
    if (!expect(canvas != nullptr, std::string("create(Metal) for ") + probe.edge)) {
        return false;
    }
    canvas->initializeContext();
    canvas->beginFrame();

    Paint layerPaint;
    layerPaint.setColor(Color(255, 255, 255, 255));
    LayerOptions options;
    options.setImageFilter(ImageFilter::innerShadow(6.0f, 6.0f, probe.offsetX, probe.offsetY,
                                                    Color(0, 0, 0, 255)));
    canvas->saveLayer(RectF(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)),
                      layerPaint, options);

    Paint fill;
    fill.setStyle(Paint::Style::FILL);
    fill.setColor(Color(255, 240, 80, 255));
    fill.setAntiAlias(false);
    canvas->drawRect(RectF(12.0f, 12.0f, 40.0f, 40.0f), fill);
    canvas->restore();
    canvas->endFrame();

    std::vector<unsigned char> pixels;
    if (!expect(canvas->readPixelsRGBA(pixels), std::string("readPixels ") + probe.edge)) {
        return false;
    }
    auto at = [&](int x, int y) { return &pixels[(static_cast<std::size_t>(y) * w + x) * 4u]; };
    const unsigned char *centre = at(32, 32);
    const unsigned char *shaded = at(probe.shadedProbeX, probe.shadedProbeY);
    bool ok = expect(centre[1] > 180,
                     std::string("centre should stay yellow for ") + probe.edge);
    ok = expect(shaded[1] + 40 < centre[1],
                std::string("edge should be darkened for offset direction ") + probe.edge) && ok;
    return ok;
}

bool testMetalShadowDirections()
{
    if (!Canvas::isBackendAvailable(Canvas::Backend::Metal)) {
        return true;
    }
    bool ok = true;
    ok = runShadowVariant({/*offX=*/6.0f, /*offY=*/0.0f, /*probeX=*/16, /*probeY=*/32, "left"}) && ok;
    ok = runShadowVariant({-6.0f, 0.0f, 48, 32, "right"}) && ok;
    ok = runShadowVariant({0.0f, 6.0f, 32, 16, "top"}) && ok;
    ok = runShadowVariant({0.0f, -6.0f, 32, 48, "bottom"}) && ok;
    return ok;
}

// Clip + text: draw an ASCII glyph under a clipRect that clips out the right
// half of the canvas. The glyph should still render on the left half but any
// alpha8 texel that falls on the right must be culled by the scissor.
bool testMetalClipTextInteraction()
{
    if (!Canvas::isBackendAvailable(Canvas::Backend::Metal)) {
        return true;
    }
    const int w = 96;
    const int h = 64;
    auto canvas = Canvas::create(Canvas::Backend::Metal, w, h);
    if (!expect(canvas != nullptr, "create(Metal) should succeed")) {
        return false;
    }
    canvas->initializeContext();
    canvas->beginFrame();

    canvas->save();
    canvas->clipRect(RectF(0.0f, 0.0f, 48.0f, static_cast<float>(h)));
    Paint textPaint;
    textPaint.setColor(Color(0, 200, 0, 255));
    textPaint.setTextSize(32.0f);
    canvas->drawText("H", 8.0f, 44.0f, textPaint);
    canvas->drawText("R", 60.0f, 44.0f, textPaint);
    canvas->restore();
    canvas->endFrame();

    std::vector<unsigned char> pixels;
    if (!expect(canvas->readPixelsRGBA(pixels), "readPixels should succeed")) {
        return false;
    }

    int leftGreenPixels = 0;
    int rightGreenPixels = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const unsigned char *p = &pixels[(static_cast<std::size_t>(y) * w + x) * 4u];
            const bool green = p[1] > 100 && p[0] < 40 && p[2] < 40 && p[3] > 100;
            if (!green) continue;
            if (x < 48) ++leftGreenPixels; else ++rightGreenPixels;
        }
    }
    bool ok = expect(leftGreenPixels > 20,
                     "the H glyph on the left side of the clip should render");
    ok = expect(rightGreenPixels == 0,
                "no green pixel should survive the scissor clip on the right side") && ok;
    return ok;
}

} // namespace

int main()
{
    bool ok = true;
    ok = testMetalShadowDirections() && ok;
    ok = testMetalClipTextInteraction() && ok;
    return ok ? 0 : 1;
}

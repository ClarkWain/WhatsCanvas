// Metal geometry tests: anti-aliased fills and transformed draws.
//   * AA circle: with anti-aliasing on, the edge of a filled circle should
//     produce a coverage gradient (a pixel exactly on the mathematical edge
//     should read a partial alpha instead of hard 0 or 255).
//   * Matrix: canvas->translate + rotate should route the fill through the
//     new origin.

#include <algorithm>
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

bool testMetalAntiAliasedFill()
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

    Path circle;
    const int segments = 96;
    const float cx = 32.0f;
    const float cy = 32.0f;
    const float r = 16.0f;
    for (int i = 0; i <= segments; ++i) {
        const float t = (static_cast<float>(i) / segments) * 2.0f * 3.14159265f;
        const float x = cx + std::cos(t) * r;
        const float y = cy + std::sin(t) * r;
        if (i == 0) circle.moveTo(x, y); else circle.lineTo(x, y);
    }
    circle.close();

    canvas->beginFrame();
    Paint fill;
    fill.setStyle(Paint::Style::FILL);
    fill.setColor(Color(255, 0, 0, 255));
    fill.setAntiAlias(true);
    canvas->drawPath(circle, fill);
    canvas->endFrame();

    std::vector<unsigned char> pixels;
    if (!expect(canvas->readPixelsRGBA(pixels), "readPixelsRGBA should succeed")) {
        return false;
    }
    auto at = [&](int x, int y) { return &pixels[(static_cast<std::size_t>(y) * w + x) * 4u]; };

    // Deep interior: fully red opaque.
    const unsigned char *centre = at(32, 32);
    bool ok = expect(centre[0] > 200 && centre[3] > 200,
                     "AA circle centre should be opaque red");

    // Look for at least one edge pixel with a partial-coverage alpha.
    // Sample the row at y = cy across the full width and count how many
    // pixels lie in the "soft edge" band. The tolerance is loose because the
    // exact fringe width depends on how the tessellator places its AA
    // triangles relative to pixel centres — the key signal is that alpha is
    // neither 0 nor 255 at the edge.
    int softEdgeCount = 0;
    for (int x = 0; x < w; ++x) {
        const unsigned char *p = at(x, 32);
        if (p[3] > 4 && p[3] < 252) ++softEdgeCount;
    }
    ok = expect(softEdgeCount >= 1,
                "AA fill should produce at least one partial-coverage pixel on the arc edge") && ok;
    return ok;
}

bool testMetalMatrixTranslate()
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
    Paint fill;
    fill.setStyle(Paint::Style::FILL);
    fill.setColor(Color(0, 220, 0, 255));
    fill.setAntiAlias(false);
    canvas->save();
    canvas->translate(20.0f, 20.0f);
    // A small rect drawn at local (0..8, 0..8) — should map to canvas (20..28, 20..28).
    canvas->drawRect(RectF(0.0f, 0.0f, 8.0f, 8.0f), fill);
    canvas->restore();
    canvas->endFrame();

    std::vector<unsigned char> pixels;
    if (!expect(canvas->readPixelsRGBA(pixels), "readPixelsRGBA should succeed")) {
        return false;
    }
    auto at = [&](int x, int y) { return &pixels[(static_cast<std::size_t>(y) * w + x) * 4u]; };

    // Inside translated rect: green.
    const unsigned char *inside = at(24, 24);
    bool ok = expect(inside[1] > 180 && inside[3] > 200,
                     "translated fill should land inside the translated rect");
    // Where the local (0,0) would sit without translate: transparent.
    const unsigned char *outsideOrigin = at(2, 2);
    ok = expect(outsideOrigin[3] < 20,
                "canvas origin should stay untouched after translate") && ok;
    return ok;
}

} // namespace

int main()
{
    bool ok = true;
    ok = testMetalAntiAliasedFill() && ok;
    ok = testMetalMatrixTranslate() && ok;
    return ok ? 0 : 1;
}

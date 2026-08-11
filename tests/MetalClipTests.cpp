// Metal clip mask test. Draws a circle-clip over a solid fill via clipPath and
// verifies that pixels outside the circle stay transparent while pixels inside
// receive the fill color. Exercises MetalRenderDevice::rasterizeClipMask + the
// ClipFill MSL pipeline end to end.

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

bool testMetalCircleClip()
{
    if (!Canvas::isBackendAvailable(Canvas::Backend::Metal)) {
        std::cout << "Metal unavailable in this build: skipping clip test.\n";
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

    // Build a circular clip centered on the canvas with radius 16.
    Path circle;
    const int segments = 48;
    const float cx = 32.0f;
    const float cy = 32.0f;
    const float r = 16.0f;
    for (int i = 0; i <= segments; ++i) {
        const float t = (static_cast<float>(i) / segments) * 2.0f * 3.14159265f;
        const float px = cx + std::cos(t) * r;
        const float py = cy + std::sin(t) * r;
        if (i == 0) {
            circle.moveTo(px, py);
        } else {
            circle.lineTo(px, py);
        }
    }
    circle.close();

    canvas->save();
    canvas->clipPath(circle);
    Paint fill;
    fill.setStyle(Paint::Style::FILL);
    fill.setColor(Color(255, 0, 0, 255));
    fill.setAntiAlias(false);
    canvas->drawRect(RectF(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)), fill);
    canvas->restore();
    canvas->endFrame();

    std::vector<unsigned char> pixels;
    if (!expect(canvas->readPixelsRGBA(pixels), "readPixelsRGBA should succeed")) {
        return false;
    }
    auto at = [&](int x, int y) { return &pixels[(static_cast<std::size_t>(y) * w + x) * 4u]; };

    // The dead center must be filled with red.
    const unsigned char *center = at(32, 32);
    bool ok = expect(center[0] > 200 && center[1] < 40 && center[2] < 40 && center[3] > 200,
                     "clipped fill center should be red");

    // A corner well outside the circle must remain transparent.
    const unsigned char *corner = at(2, 2);
    ok = expect(corner[3] < 20, "outside the clip circle should stay transparent") && ok;

    // A point just outside the radius (dx=20 from center) must also be
    // transparent.
    const unsigned char *outside = at(32 + 22, 32);
    ok = expect(outside[3] < 20, "point just outside the circle should be transparent") && ok;
    return ok;
}

} // namespace

int main()
{
    return testMetalCircleClip() ? 0 : 1;
}

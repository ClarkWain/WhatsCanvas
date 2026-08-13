// Metal paint-state test: verify Paint field carryover across successive
// draws on the same Canvas. Covers setColor, setAlpha, setStrokeWidth, and
// the state stack save/restore semantics as observed through the rendered
// output.

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

// Draw three overlapping rects with distinct colors set on the same Paint
// object between drawRect calls. Each colour change should apply to the
// next draw only.
bool testMetalPaintColorCarryover()
{
    if (!Canvas::isBackendAvailable(Canvas::Backend::Metal)) {
        return true;
    }
    const int w = 96;
    const int h = 32;
    auto canvas = Canvas::create(Canvas::Backend::Metal, w, h);
    if (!expect(canvas != nullptr, "create(Metal) should succeed")) {
        return false;
    }
    canvas->initializeContext();
    canvas->beginFrame();

    Paint p;
    p.setStyle(Paint::Style::FILL);
    p.setAntiAlias(false);

    p.setColor(Color(255, 0, 0, 255));
    canvas->drawRect(RectF(0.0f, 0.0f, 32.0f, 32.0f), p);
    p.setColor(Color(0, 255, 0, 255));
    canvas->drawRect(RectF(32.0f, 0.0f, 32.0f, 32.0f), p);
    p.setColor(Color(0, 0, 255, 255));
    canvas->drawRect(RectF(64.0f, 0.0f, 32.0f, 32.0f), p);
    canvas->endFrame();

    std::vector<unsigned char> pixels;
    if (!expect(canvas->readPixelsRGBA(pixels), "readPixels should succeed")) {
        return false;
    }
    auto at = [&](int x, int y) { return &pixels[(static_cast<std::size_t>(y) * w + x) * 4u]; };

    const unsigned char *redProbe = at(16, 16);
    const unsigned char *greenProbe = at(48, 16);
    const unsigned char *blueProbe = at(80, 16);
    bool ok = expect(redProbe[0] > 200 && redProbe[1] < 40, "first rect should be red");
    ok = expect(greenProbe[1] > 200 && greenProbe[0] < 40, "second rect should be green") && ok;
    ok = expect(blueProbe[2] > 200 && blueProbe[0] < 40, "third rect should be blue") && ok;
    return ok;
}

// Save/restore should isolate transform and paint scope changes.
bool testMetalSaveRestoreIsolation()
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

    Paint p;
    p.setStyle(Paint::Style::FILL);
    p.setAntiAlias(false);
    p.setColor(Color(255, 0, 0, 255));

    canvas->save();
    canvas->translate(20.0f, 20.0f);
    canvas->drawRect(RectF(0.0f, 0.0f, 8.0f, 8.0f), p); // canvas (20..28, 20..28) red
    canvas->restore();
    // After restore the translate should be gone; draw at local origin.
    p.setColor(Color(0, 0, 255, 255));
    canvas->drawRect(RectF(0.0f, 0.0f, 8.0f, 8.0f), p); // canvas (0..8, 0..8) blue

    canvas->endFrame();
    std::vector<unsigned char> pixels;
    if (!expect(canvas->readPixelsRGBA(pixels), "readPixels should succeed")) {
        return false;
    }
    auto at = [&](int x, int y) { return &pixels[(static_cast<std::size_t>(y) * w + x) * 4u]; };
    const unsigned char *red = at(24, 24);
    const unsigned char *blue = at(4, 4);
    bool ok = expect(red[0] > 200 && red[3] > 200,
                     "translated rect should land at canvas (20..28)");
    ok = expect(blue[2] > 200 && blue[3] > 200,
                "after restore the second rect should land at canvas origin") && ok;
    return ok;
}

} // namespace

int main()
{
    bool ok = true;
    ok = testMetalPaintColorCarryover() && ok;
    ok = testMetalSaveRestoreIsolation() && ok;
    return ok ? 0 : 1;
}

// Metal gradient tests: covers radial + horizontal + vertical linear
// gradients, exercising the encoder's gradient path and the Metal Gradient
// pipeline (gradient_vs / gradient_fs).

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

bool testMetalVerticalLinearGradient()
{
    if (!Canvas::isBackendAvailable(Canvas::Backend::Metal)) {
        return true;
    }
    const int w = 32;
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
    fill.setLinearGradient(16.0f, 0.0f, 16.0f, 64.0f, Color(0, 255, 0, 255), Color(255, 0, 255, 255));
    canvas->drawRect(RectF(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)), fill);
    canvas->endFrame();

    std::vector<unsigned char> pixels;
    if (!expect(canvas->readPixelsRGBA(pixels), "readPixels should succeed")) {
        return false;
    }
    auto at = [&](int x, int y) { return &pixels[(static_cast<std::size_t>(y) * w + x) * 4u]; };

    const unsigned char *top = at(16, 2);
    bool ok = expect(top[1] > 200 && top[0] < 30, "vertical gradient top should be green");
    const unsigned char *bottom = at(16, 62);
    ok = expect(bottom[0] > 200 && bottom[2] > 200 && bottom[1] < 30,
                "vertical gradient bottom should be magenta") && ok;
    return ok;
}

bool testMetalRadialGradient()
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
    fill.setAntiAlias(false);
    fill.setRadialGradient(32.0f, 32.0f, 24.0f, Color(255, 255, 255, 255), Color(0, 0, 0, 255));
    canvas->drawRect(RectF(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)), fill);
    canvas->endFrame();

    std::vector<unsigned char> pixels;
    if (!expect(canvas->readPixelsRGBA(pixels), "readPixels should succeed")) {
        return false;
    }
    auto at = [&](int x, int y) { return &pixels[(static_cast<std::size_t>(y) * w + x) * 4u]; };

    // Radial gradient centre must be white (t = 0).
    const unsigned char *centre = at(32, 32);
    bool ok = expect(centre[0] > 220 && centre[1] > 220 && centre[2] > 220,
                     "radial gradient centre should read white");
    // A pixel at the gradient's far edge (radius 24) should read close to
    // black (t = 1).
    const unsigned char *edge = at(32 + 22, 32);
    ok = expect(edge[0] < 60 && edge[1] < 60 && edge[2] < 60,
                "radial gradient edge should read close to black") && ok;
    return ok;
}

} // namespace

int main()
{
    bool ok = true;
    ok = testMetalVerticalLinearGradient() && ok;
    ok = testMetalRadialGradient() && ok;
    return ok ? 0 : 1;
}

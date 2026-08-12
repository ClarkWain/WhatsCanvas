// Metal blend-mode tests. Draws a background rectangle then a foreground
// rectangle with a specific blend mode set on its Paint, and verifies the
// overlap pixel matches the expected blend of the two colors. Covers the
// blend modes MetalRenderDevice's Solid pipeline configures explicitly
// (SrcOver / Src / Add / Multiply / Screen).

#include <algorithm>
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

bool nearBy(int actual, int expected, int tolerance)
{
    return std::abs(actual - expected) <= tolerance;
}

bool drawOverlap(Paint::BlendMode mode, unsigned char (&outCenter)[4])
{
    const int w = 32;
    const int h = 32;
    auto canvas = Canvas::create(Canvas::Backend::Metal, w, h);
    if (!canvas) {
        return false;
    }
    canvas->initializeContext();
    canvas->beginFrame();
    // Background: opaque red covers the whole canvas.
    Paint bg;
    bg.setStyle(Paint::Style::FILL);
    bg.setColor(Color(255, 0, 0, 255));
    bg.setAntiAlias(false);
    canvas->drawRect(RectF(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)), bg);
    // Foreground: opaque blue, with the requested blend mode. Full-canvas
    // rect so every pixel receives the blend.
    Paint fg;
    fg.setStyle(Paint::Style::FILL);
    fg.setColor(Color(0, 0, 255, 255));
    fg.setBlendMode(mode);
    fg.setAntiAlias(false);
    canvas->drawRect(RectF(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)), fg);
    canvas->endFrame();
    std::vector<unsigned char> pixels;
    if (!canvas->readPixelsRGBA(pixels)) return false;
    const std::size_t idx = (static_cast<std::size_t>(h / 2) * w + w / 2) * 4u;
    outCenter[0] = pixels[idx + 0];
    outCenter[1] = pixels[idx + 1];
    outCenter[2] = pixels[idx + 2];
    outCenter[3] = pixels[idx + 3];
    return true;
}

bool testMetalBlendModes()
{
    if (!Canvas::isBackendAvailable(Canvas::Backend::Metal)) {
        return true;
    }
    unsigned char center[4];
    bool ok = true;

    // SrcOver: opaque blue completely covers the red because fg alpha=1.
    ok = expect(drawOverlap(Paint::BlendMode::SRC_OVER, center) && center[2] > 200 && center[0] < 30,
                "SrcOver of opaque blue over red should read blue") && ok;
    // Add: red + blue -> magenta.
    ok = expect(drawOverlap(Paint::BlendMode::ADD, center)
                && nearBy(center[0], 255, 10) && nearBy(center[2], 255, 10) && center[1] < 30,
                "Add of red + blue should read magenta") && ok;
    // Screen: 1 - (1 - red) * (1 - blue) -> magenta.
    ok = expect(drawOverlap(Paint::BlendMode::SCREEN, center)
                && nearBy(center[0], 255, 10) && nearBy(center[2], 255, 10),
                "Screen of red + blue should read magenta") && ok;
    // Multiply: red * blue -> black (they share no channels).
    ok = expect(drawOverlap(Paint::BlendMode::MULTIPLY, center)
                && center[0] < 30 && center[2] < 30,
                "Multiply of red and blue should read black") && ok;
    return ok;
}

} // namespace

int main()
{
    return testMetalBlendModes() ? 0 : 1;
}

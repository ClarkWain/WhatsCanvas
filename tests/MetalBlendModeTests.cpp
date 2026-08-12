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

    // The Porter-Duff modes below all draw a full-canvas foreground so
    // src.a = dst.a = 1 everywhere, giving predictable channel outcomes.
    // Dst: destination is left untouched, so the background red must come
    // through unchanged even though we drew a blue fg on top.
    ok = expect(drawOverlap(Paint::BlendMode::DST, center)
                && center[0] > 200 && center[2] < 30,
                "Dst blend should preserve the background red") && ok;
    // SrcIn (opaque fg over opaque bg): result = fg (destination alpha 1),
    // so the readback is pure blue.
    ok = expect(drawOverlap(Paint::BlendMode::SRC_IN, center)
                && center[2] > 200 && center[0] < 30,
                "SrcIn of opaque blue over opaque red should read blue") && ok;
    // DstIn: dst * src.a. src.a=1 so destination survives -> red.
    ok = expect(drawOverlap(Paint::BlendMode::DST_IN, center)
                && center[0] > 200 && center[2] < 30,
                "DstIn should preserve the destination red when src is opaque") && ok;
    // SrcOut: src * (1 - dst.a). dst.a=1 -> result is (0,0,0,0) transparent.
    ok = expect(drawOverlap(Paint::BlendMode::SRC_OUT, center) && center[3] < 20,
                "SrcOut against an opaque destination should be transparent") && ok;
    // DstOut: dst * (1 - src.a). src.a=1 -> destination punched to transparent.
    ok = expect(drawOverlap(Paint::BlendMode::DST_OUT, center) && center[3] < 20,
                "DstOut against an opaque source should erase the destination") && ok;
    // SrcAtop: src * dst.a + dst * (1 - src.a). With src.a=1 and dst.a=1 the
    // result is src -> blue.
    ok = expect(drawOverlap(Paint::BlendMode::SRC_ATOP, center)
                && center[2] > 200 && center[0] < 30,
                "SrcAtop with opaque src+dst should read blue") && ok;
    // Xor: src * (1 - dst.a) + dst * (1 - src.a). Both alphas 1 -> zero.
    ok = expect(drawOverlap(Paint::BlendMode::XOR, center) && center[3] < 20,
                "Xor of two opaque fills should read transparent") && ok;
    // Clear: always zero.
    ok = expect(drawOverlap(Paint::BlendMode::CLEAR, center)
                && center[0] < 20 && center[3] < 20,
                "Clear must zero out both RGB and alpha") && ok;
    return ok;
}

} // namespace

int main()
{
    return testMetalBlendModes() ? 0 : 1;
}

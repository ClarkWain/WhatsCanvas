// Metal text rendering test. Text drawing feeds the backend an alpha-8 glyph
// atlas texture per glyph batch; MetalRenderDevice's TexturedAlpha pipeline
// samples the R8 channel and modulates by the paint's color tint, so drawing a
// simple ASCII string should light up interior pixels with the tint color.

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

bool testMetalText()
{
    if (!Canvas::isBackendAvailable(Canvas::Backend::Metal)) {
        std::cout << "Metal unavailable in this build: skipping text test.\n";
        return true;
    }

    const int w = 128;
    const int h = 64;
    auto canvas = Canvas::create(Canvas::Backend::Metal, w, h);
    if (!expect(canvas != nullptr, "create(Metal) should succeed")) {
        return false;
    }
    canvas->initializeContext();

    canvas->beginFrame();
    Paint textPaint;
    textPaint.setColor(Color(255, 0, 0, 255));
    textPaint.setTextSize(32.0f);
    canvas->drawText("A", 20.0f, 44.0f, textPaint);
    canvas->endFrame();

    std::vector<unsigned char> pixels;
    if (!expect(canvas->readPixelsRGBA(pixels), "readPixelsRGBA should succeed")) {
        return false;
    }

    // Search the readback for at least one strongly-red pixel that came from a
    // rasterized glyph. If the alpha8 path is broken the whole canvas will be
    // transparent black; if the tint path is broken there will be no red.
    int redPixels = 0;
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
        if (pixels[i + 0] > 180 && pixels[i + 1] < 60 && pixels[i + 2] < 60 && pixels[i + 3] > 180) {
            ++redPixels;
        }
    }
    bool ok = expect(redPixels > 20, "glyph rasterization should produce interior red pixels");
    if (!ok) {
        std::cerr << "  (found only " << redPixels << " strongly-red pixels)" << std::endl;
    }
    return ok;
}

} // namespace

int main()
{
    return testMetalText() ? 0 : 1;
}

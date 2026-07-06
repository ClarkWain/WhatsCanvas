// Headless tests for the pure-CPU software rasterizer backend. No GPU or
// graphics context is required, so these run anywhere and assert exact pixels.

#include <iostream>
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

bool testCreateSoftwareCanvas()
{
    std::unique_ptr<Canvas> canvas = Canvas::createSoftware(32, 24);
    bool ok = expect(canvas != nullptr, "createSoftware should return a canvas");
    if (!canvas) {
        return false;
    }
    ok = expect(canvas->getWidth() == 32 && canvas->getHeight() == 24, "software canvas should keep its size") && ok;

    std::vector<unsigned char> pixels;
    ok = expect(canvas->readPixelsRGBA(pixels), "readPixelsRGBA should succeed") && ok;
    ok = expect(pixels.size() == 32u * 24u * 4u, "framebuffer should be width*height*4 bytes") && ok;
    return ok;
}

bool testSolidFillRasterizes()
{
    const int w = 64;
    const int h = 64;
    std::unique_ptr<Canvas> canvas = Canvas::createSoftware(w, h);
    if (!canvas) {
        return expect(false, "createSoftware should return a canvas");
    }

    canvas->beginFrame();
    Paint fill;
    fill.setStyle(Paint::Style::FILL);
    fill.setColor(Color(255, 0, 0, 255));
    fill.setAntiAlias(false); // crisp edges so we can assert exact colors
    canvas->drawRect(RectF(16.0f, 16.0f, 32.0f, 32.0f), fill);
    canvas->flush();

    std::vector<unsigned char> pixels;
    bool ok = expect(canvas->readPixelsRGBA(pixels), "readPixelsRGBA should succeed");
    ok = expect(pixels.size() == static_cast<std::size_t>(w) * h * 4u, "framebuffer size") && ok;
    if (!ok) {
        return false;
    }

    auto pixelAt = [&](int x, int y) -> const unsigned char * {
        return &pixels[(static_cast<std::size_t>(y) * w + x) * 4u];
    };

    const unsigned char *center = pixelAt(32, 32);
    ok = expect(center[0] == 255 && center[1] == 0 && center[2] == 0 && center[3] == 255,
                "rect interior should be opaque red") && ok;

    const unsigned char *corner = pixelAt(2, 2);
    ok = expect(corner[3] == 0, "outside the rect should be transparent (cleared)") && ok;

    return ok;
}

bool testSrcOverBlending()
{
    const int w = 32;
    const int h = 32;
    std::unique_ptr<Canvas> canvas = Canvas::createSoftware(w, h);
    if (!canvas) {
        return expect(false, "createSoftware should return a canvas");
    }

    canvas->beginFrame();
    // Opaque blue background, then 50% red on top -> SrcOver should mix.
    Paint bg;
    bg.setStyle(Paint::Style::FILL);
    bg.setColor(Color(0, 0, 255, 255));
    bg.setAntiAlias(false);
    canvas->drawRect(RectF(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)), bg);

    Paint overlay;
    overlay.setStyle(Paint::Style::FILL);
    overlay.setColor(Color(255, 0, 0, 128));
    overlay.setAntiAlias(false);
    canvas->drawRect(RectF(8.0f, 8.0f, 16.0f, 16.0f), overlay);
    canvas->flush();

    std::vector<unsigned char> pixels;
    if (!canvas->readPixelsRGBA(pixels) || pixels.size() != static_cast<std::size_t>(w) * h * 4u) {
        return expect(false, "readPixelsRGBA should succeed with the right size");
    }

    const unsigned char *mixed = &pixels[(16u * w + 16u) * 4u];
    // out = src*srcA + dst*(1-srcA); srcA=128/255~0.502. red ~128, blue ~127.
    bool ok = expect(mixed[0] > 120 && mixed[0] < 140, "blended red channel ~128");
    ok = expect(mixed[2] > 118 && mixed[2] < 138, "blended blue channel ~127") && ok;
    ok = expect(mixed[3] == 255, "blended alpha stays opaque") && ok;
    return ok;
}

} // namespace

int main()
{
    bool ok = testCreateSoftwareCanvas();
    ok = testSolidFillRasterizes() && ok;
    ok = testSrcOverBlending() && ok;
    return ok ? 0 : 1;
}

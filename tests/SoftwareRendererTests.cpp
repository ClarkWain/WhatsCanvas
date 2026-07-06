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

bool testLinearGradient()
{
    const int w = 64;
    const int h = 16;
    std::unique_ptr<Canvas> canvas = Canvas::createSoftware(w, h);
    if (!canvas) {
        return expect(false, "createSoftware should return a canvas");
    }

    canvas->beginFrame();
    Paint grad;
    grad.setStyle(Paint::Style::FILL);
    grad.setAntiAlias(false);
    grad.setLinearGradient(0.0f, 0.0f, static_cast<float>(w), 0.0f,
                           {Paint::ColorStop(0.0f, Color(255, 0, 0, 255)),
                            Paint::ColorStop(1.0f, Color(0, 0, 255, 255))});
    canvas->drawRect(RectF(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)), grad);
    canvas->flush();

    std::vector<unsigned char> pixels;
    if (!canvas->readPixelsRGBA(pixels) || pixels.size() != static_cast<std::size_t>(w) * h * 4u) {
        return expect(false, "readPixelsRGBA should succeed with the right size");
    }
    auto pixelAt = [&](int x, int y) { return &pixels[(static_cast<std::size_t>(y) * w + x) * 4u]; };

    const unsigned char *left = pixelAt(1, 8);
    const unsigned char *right = pixelAt(62, 8);
    bool ok = expect(left[0] > 220 && left[2] < 40, "gradient left edge should be mostly red");
    ok = expect(right[2] > 220 && right[0] < 40, "gradient right edge should be mostly blue") && ok;
    const unsigned char *mid = pixelAt(32, 8);
    ok = expect(mid[0] > 80 && mid[0] < 200 && mid[2] > 80 && mid[2] < 200, "gradient midpoint should be mixed") && ok;
    return ok;
}

bool testDrawLine()
{
    const int w = 32;
    const int h = 32;
    std::unique_ptr<Canvas> canvas = Canvas::createSoftware(w, h);
    if (!canvas) {
        return expect(false, "createSoftware should return a canvas");
    }

    canvas->beginFrame();
    Paint stroke;
    stroke.setStyle(Paint::Style::STROKE);
    stroke.setStrokeWidth(4.0f);
    stroke.setAntiAlias(false);
    stroke.setStrokeColor(Color(0, 255, 0, 255));
    canvas->drawLine(0.0f, 16.0f, 32.0f, 16.0f, stroke);
    canvas->flush();

    std::vector<unsigned char> pixels;
    if (!canvas->readPixelsRGBA(pixels) || pixels.size() != static_cast<std::size_t>(w) * h * 4u) {
        return expect(false, "readPixelsRGBA should succeed with the right size");
    }
    auto pixelAt = [&](int x, int y) { return &pixels[(static_cast<std::size_t>(y) * w + x) * 4u]; };

    const unsigned char *onLine = pixelAt(16, 16);
    bool ok = expect(onLine[1] == 255 && onLine[3] == 255, "pixel on the line should be opaque green");
    const unsigned char *offLine = pixelAt(16, 2);
    ok = expect(offLine[3] == 0, "pixel far from the line should be transparent") && ok;
    return ok;
}

bool testDrawImage()
{
    const int w = 16;
    const int h = 16;
    std::unique_ptr<Canvas> canvas = Canvas::createSoftware(w, h);
    if (!canvas) {
        return expect(false, "createSoftware should return a canvas");
    }

    // A 4x4 opaque green image.
    std::vector<unsigned char> src(4 * 4 * 4, 0);
    for (std::size_t i = 0; i < 4u * 4u; ++i) {
        src[i * 4 + 0] = 0;
        src[i * 4 + 1] = 255;
        src[i * 4 + 2] = 0;
        src[i * 4 + 3] = 255;
    }
    Image image;
    bool ok = expect(image.loadFromRGBA(*canvas, src, 4, 4, false), "loadFromRGBA should succeed");
    if (!ok) {
        return false;
    }

    canvas->beginFrame();
    Paint paint;
    paint.setColor(Color(255, 255, 255, 255)); // white tint = show the image unchanged
    canvas->drawImage(image, 4.0f, 4.0f, paint);
    canvas->flush();

    std::vector<unsigned char> pixels;
    if (!canvas->readPixelsRGBA(pixels) || pixels.size() != static_cast<std::size_t>(w) * h * 4u) {
        return expect(false, "readPixelsRGBA should succeed with the right size");
    }
    auto pixelAt = [&](int x, int y) { return &pixels[(static_cast<std::size_t>(y) * w + x) * 4u]; };

    const unsigned char *inside = pixelAt(6, 6);
    ok = expect(inside[1] > 240 && inside[0] < 20 && inside[3] > 240, "image interior should be opaque green") && ok;
    const unsigned char *outside = pixelAt(0, 0);
    ok = expect(outside[3] == 0, "outside the image should be transparent") && ok;
    return ok;
}

} // namespace

int main()
{
    bool ok = testCreateSoftwareCanvas();
    ok = testSolidFillRasterizes() && ok;
    ok = testSrcOverBlending() && ok;
    ok = testLinearGradient() && ok;
    ok = testDrawLine() && ok;
    ok = testDrawImage() && ok;
    return ok ? 0 : 1;
}

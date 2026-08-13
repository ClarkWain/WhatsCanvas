// Metal solid-geometry tests via Canvas::drawPath: non-rect polygons routed
// through the Metal backend. Complements MetalGeometryTests (which covers
// AA + matrix on rects/circles) and MetalDrawListTests (raw primitive seam)
// by driving arbitrary polygon paths through the tessellator and Solid
// pipeline. Mirrors VulkanSolidGeometryTests in intent (arbitrary triangle,
// full-target quad, small primitive) while using the portable Canvas API.

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

bool pixelWithin(const std::vector<unsigned char> &pixels, int width, int x, int y,
                 int r, int g, int b, int a, int tolerance, const char *label)
{
    const std::size_t idx = (static_cast<std::size_t>(y) * width + x) * 4u;
    auto near = [tolerance](int actual, int expected) {
        return std::abs(actual - expected) <= tolerance;
    };
    if (!near(pixels[idx], r) || !near(pixels[idx + 1], g) || !near(pixels[idx + 2], b)
        || !near(pixels[idx + 3], a)) {
        std::cerr << "  " << label << " = (" << int(pixels[idx]) << "," << int(pixels[idx + 1])
                  << "," << int(pixels[idx + 2]) << "," << int(pixels[idx + 3])
                  << "), expected ~(" << r << "," << g << "," << b << "," << a << ")." << std::endl;
        return false;
    }
    return true;
}

// Test 1: a filled triangle covers the centroid and leaves distant corners
// transparent. Exercises the tessellator + Solid pipeline through drawPath.
bool testMetalTrianglePath()
{
    if (!Canvas::isBackendAvailable(Canvas::Backend::Metal)) return true;
    const int w = 64;
    const int h = 64;
    auto canvas = Canvas::create(Canvas::Backend::Metal, w, h);
    if (!expect(canvas != nullptr, "create Metal canvas")) return false;
    canvas->initializeContext();
    canvas->beginFrame();

    Path triangle;
    triangle.moveTo(8.0f, 56.0f);
    triangle.lineTo(56.0f, 56.0f);
    triangle.lineTo(32.0f, 8.0f);
    triangle.close();

    Paint paint;
    paint.setStyle(Paint::Style::FILL);
    paint.setColor(Color(255, 0, 0, 255));
    paint.setAntiAlias(false);
    canvas->drawPath(triangle, paint);

    canvas->endFrame();
    std::vector<unsigned char> pixels;
    canvas->readPixelsRGBA(pixels);
    if (!expect(!pixels.empty(), "readback should succeed")) return false;

    bool ok = true;
    ok = pixelWithin(pixels, w, 32, 40, 255, 0, 0, 255, 4, "triangle centroid") && ok;
    ok = pixelWithin(pixels, w, 32, 20, 255, 0, 0, 255, 8, "triangle apex interior") && ok;
    ok = pixelWithin(pixels, w, 2, 2, 0, 0, 0, 0, 4, "top-left corner clear") && ok;
    ok = pixelWithin(pixels, w, 61, 2, 0, 0, 0, 0, 4, "top-right corner clear") && ok;
    return ok;
}

// Test 2: a five-pointed star. The self-crossing polygon exercises the fill
// rule / tessellator on more complex geometry.
bool testMetalStarPath()
{
    if (!Canvas::isBackendAvailable(Canvas::Backend::Metal)) return true;
    const int w = 64;
    const int h = 64;
    auto canvas = Canvas::create(Canvas::Backend::Metal, w, h);
    if (!expect(canvas != nullptr, "create Metal canvas")) return false;
    canvas->initializeContext();
    canvas->beginFrame();

    const float cx = 32.0f;
    const float cy = 32.0f;
    const float rOuter = 26.0f;
    const float rInner = 11.0f;
    Path star;
    for (int i = 0; i < 10; ++i) {
        const float t = static_cast<float>(i) / 10.0f * 2.0f * 3.14159265f
                        - 3.14159265f / 2.0f;
        const float r = (i % 2 == 0) ? rOuter : rInner;
        const float x = cx + std::cos(t) * r;
        const float y = cy + std::sin(t) * r;
        if (i == 0) {
            star.moveTo(x, y);
        } else {
            star.lineTo(x, y);
        }
    }
    star.close();

    Paint paint;
    paint.setStyle(Paint::Style::FILL);
    paint.setColor(Color(0, 200, 0, 255));
    paint.setAntiAlias(true);
    canvas->drawPath(star, paint);

    canvas->endFrame();
    std::vector<unsigned char> pixels;
    canvas->readPixelsRGBA(pixels);

    bool ok = true;
    // Star centre is well inside the inner pentagon -> filled green.
    ok = pixelWithin(pixels, w, 32, 32, 0, 200, 0, 255, 16, "star centre filled") && ok;
    // Corners of the canvas sit far outside the star -> transparent.
    ok = pixelWithin(pixels, w, 2, 2, 0, 0, 0, 0, 4, "star bg top-left clear") && ok;
    ok = pixelWithin(pixels, w, 61, 61, 0, 0, 0, 0, 4, "star bg bottom-right clear") && ok;
    return ok;
}

// Test 3: a full-canvas quad path (as two triangles via drawPath / drawRect).
// Validates that a covering solid fill actually covers.
bool testMetalFullCoverageFill()
{
    if (!Canvas::isBackendAvailable(Canvas::Backend::Metal)) return true;
    const int w = 48;
    const int h = 48;
    auto canvas = Canvas::create(Canvas::Backend::Metal, w, h);
    if (!expect(canvas != nullptr, "create Metal canvas")) return false;
    canvas->initializeContext();
    canvas->beginFrame();

    Paint paint;
    paint.setStyle(Paint::Style::FILL);
    paint.setColor(Color(0, 128, 255, 255));
    paint.setAntiAlias(false);
    canvas->drawRect(RectF(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)), paint);

    canvas->endFrame();
    std::vector<unsigned char> pixels;
    canvas->readPixelsRGBA(pixels);

    bool ok = true;
    ok = pixelWithin(pixels, w, 0, 0, 0, 128, 255, 255, 2, "quad top-left") && ok;
    ok = pixelWithin(pixels, w, w - 1, h - 1, 0, 128, 255, 255, 2, "quad bottom-right") && ok;
    ok = pixelWithin(pixels, w, w / 2, h / 2, 0, 128, 255, 255, 2, "quad centre") && ok;
    return ok;
}

} // namespace

int main()
{
    bool ok = true;
    ok = testMetalTrianglePath() && ok;
    ok = testMetalStarPath() && ok;
    ok = testMetalFullCoverageFill() && ok;
    return ok ? 0 : 1;
}

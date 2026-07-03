// Integration tests for the fill tessellation cache exposed through
// Canvas::getRenderStats(). drawPath only records commands (no GPU work), so
// these run headlessly: they exercise contour extraction, triangulation, and
// the cache without a live OpenGL context.

#include <iostream>
#include <string>

#include "wsc/wsc.h"

using namespace wsc;

namespace {

bool expect(bool condition, const std::string &message)
{
    if (condition) {
        return true;
    }
    std::cerr << "EXPECTATION FAILED: " << message << std::endl;
    return false;
}

Path makePentagon(float cx, float cy, float r)
{
    Path path;
    const float pi = 3.14159265358979323846f;
    for (int i = 0; i < 5; ++i) {
        const float a = -pi * 0.5f + static_cast<float>(i) * (2.0f * pi / 5.0f);
        const float x = cx + std::cos(a) * r;
        const float y = cy + std::sin(a) * r;
        if (i == 0) {
            path.moveTo(x, y);
        } else {
            path.lineTo(x, y);
        }
    }
    path.close();
    return path;
}

Path makeSquare(float x, float y, float s)
{
    Path path;
    path.moveTo(x, y);
    path.lineTo(x + s, y);
    path.lineTo(x + s, y + s);
    path.lineTo(x, y + s);
    path.close();
    return path;
}

bool testIdenticalFillReusesTessellation()
{
    Canvas canvas;
    canvas.setSize(256, 256);

    Paint fill;
    fill.setStyle(Paint::Style::FILL);
    fill.setColor(Color(200, 60, 60));

    const Path pentagon = makePentagon(120.0f, 120.0f, 80.0f);

    canvas.drawPath(pentagon, fill);
    Canvas::RenderStats s1 = canvas.getRenderStats();
    bool ok = expect(s1.tessellationCacheMisses == 1 && s1.tessellationCacheHits == 0
                         && s1.tessellationCacheSize == 1,
                     "first fill should be a cache miss and populate one entry");

    canvas.drawPath(pentagon, fill);
    Canvas::RenderStats s2 = canvas.getRenderStats();
    ok = expect(s2.tessellationCacheHits == 1 && s2.tessellationCacheMisses == 1
                    && s2.tessellationCacheSize == 1,
                "re-drawing the identical shape should hit the cache") && ok;
    return ok;
}

bool testTransformDoesNotInvalidateTessellation()
{
    Canvas canvas;
    canvas.setSize(256, 256);

    Paint fill;
    fill.setStyle(Paint::Style::FILL);
    fill.setColor(Color(60, 160, 220));

    const Path pentagon = makePentagon(120.0f, 120.0f, 80.0f);
    canvas.drawPath(pentagon, fill);

    // The same local geometry drawn under a different transform must still hit:
    // triangulation is computed in local path space and is transform independent.
    canvas.save();
    canvas.translate(40.0f, 25.0f);
    canvas.scale(1.5f, 1.5f);
    canvas.drawPath(pentagon, fill);
    canvas.restore();

    Canvas::RenderStats stats = canvas.getRenderStats();
    return expect(stats.tessellationCacheHits == 1 && stats.tessellationCacheSize == 1,
                  "transformed identical shape should reuse the cached tessellation");
}

bool testDistinctShapesUseDistinctEntries()
{
    Canvas canvas;
    canvas.setSize(256, 256);

    Paint fill;
    fill.setStyle(Paint::Style::FILL);
    fill.setColor(Color(120, 200, 120));

    canvas.drawPath(makePentagon(120.0f, 120.0f, 80.0f), fill);
    canvas.drawPath(makeSquare(20.0f, 20.0f, 60.0f), fill);

    Canvas::RenderStats stats = canvas.getRenderStats();
    return expect(stats.tessellationCacheMisses == 2 && stats.tessellationCacheSize == 2,
                  "distinct shapes should occupy distinct cache entries");
}

bool testStrokeOnlyDoesNotPopulateFillCache()
{
    Canvas canvas;
    canvas.setSize(256, 256);

    Paint stroke;
    stroke.setStyle(Paint::Style::STROKE);
    stroke.setStrokeWidth(3.0f);
    stroke.setStrokeColor(Color(240, 240, 240));

    canvas.drawPath(makeSquare(20.0f, 20.0f, 60.0f), stroke);

    Canvas::RenderStats stats = canvas.getRenderStats();
    return expect(stats.tessellationCacheSize == 0 && stats.tessellationCacheMisses == 0,
                  "a stroke-only draw should not populate the fill tessellation cache");
}

bool testIdenticalStrokeReusesMesh()
{
    Canvas canvas;
    canvas.setSize(256, 256);

    Paint stroke;
    stroke.setStyle(Paint::Style::STROKE);
    stroke.setStrokeWidth(4.0f);
    stroke.setStrokeColor(Color(240, 200, 60));

    const Path square = makeSquare(40.0f, 40.0f, 120.0f);

    canvas.drawPath(square, stroke);
    Canvas::RenderStats s1 = canvas.getRenderStats();
    bool ok = expect(s1.strokeCacheMisses == 1 && s1.strokeCacheHits == 0 && s1.strokeCacheSize == 1,
                     "first stroke should be a cache miss and populate one entry");

    // Same stroke under a different transform must still reuse the mesh, which
    // is built in local path space independent of the current matrix.
    canvas.save();
    canvas.translate(20.0f, 10.0f);
    canvas.drawPath(square, stroke);
    canvas.restore();
    Canvas::RenderStats s2 = canvas.getRenderStats();
    ok = expect(s2.strokeCacheHits == 1 && s2.strokeCacheSize == 1,
                "transformed identical stroke should reuse the cached mesh") && ok;

    // A different stroke width must not collide with the cached mesh.
    Paint thicker = stroke;
    thicker.setStrokeWidth(8.0f);
    canvas.drawPath(square, thicker);
    Canvas::RenderStats s3 = canvas.getRenderStats();
    ok = expect(s3.strokeCacheMisses == 2 && s3.strokeCacheSize == 2,
                "changing stroke width should produce a distinct cache entry") && ok;
    return ok;
}

bool testClipMaskSharesFillTessellationCache()
{
    Canvas canvas;
    canvas.setSize(256, 256);

    Paint fill;
    fill.setStyle(Paint::Style::FILL);
    fill.setColor(Color(200, 200, 200));

    const Path pentagon = makePentagon(120.0f, 120.0f, 80.0f);

    // Filling the shape triangulates and caches it.
    canvas.drawPath(pentagon, fill);
    Canvas::RenderStats s1 = canvas.getRenderStats();
    bool ok = expect(s1.tessellationCacheMisses == 1 && s1.tessellationCacheSize == 1,
                     "fill should populate the tessellation cache");

    // Clipping with the same path reuses that triangulation (the clip mask is
    // the fill triangulation of the path), so it hits the shared cache.
    canvas.save();
    canvas.clipPath(pentagon);
    canvas.restore();
    Canvas::RenderStats s2 = canvas.getRenderStats();
    ok = expect(s2.tessellationCacheHits == 1 && s2.tessellationCacheSize == 1,
                "clipping with a previously filled path should reuse the cached tessellation") && ok;
    return ok;
}

} // namespace

int main()
{
    bool ok = true;
    ok = testIdenticalFillReusesTessellation() && ok;
    ok = testTransformDoesNotInvalidateTessellation() && ok;
    ok = testDistinctShapesUseDistinctEntries() && ok;
    ok = testStrokeOnlyDoesNotPopulateFillCache() && ok;
    ok = testIdenticalStrokeReusesMesh() && ok;
    ok = testClipMaskSharesFillTessellationCache() && ok;
    return ok ? 0 : 1;
}

#include <wsc/CanvasStats.h>

// Integration tests for the fill tessellation cache exposed through
// Canvas::getRenderStats(). drawPath only records commands (no GPU work), so
// these run headlessly: they exercise contour extraction, triangulation, and
// the cache without a live OpenGL context.

#include <cmath>
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
    auto canvasOwner = Canvas::create(Canvas::Backend::OpenGL, 0, 0);
    Canvas &canvas = *canvasOwner;
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
                "a second observation should reuse the base tessellation") && ok;
    ok = expect(
             s2.aaCacheHits == 0 && s2.aaCacheMisses == 2
                 && s2.aaCacheSize == 1,
             "a second observation should admit final AA geometry") && ok;
    canvas.drawPath(pentagon, fill);
    Canvas::RenderStats s3 = canvas.getRenderStats();
    ok = expect(s3.aaCacheHits == 1 && s3.aaCacheSize == 1,
                "a stable fill should reuse its admitted AA geometry") && ok;
    return ok;
}

bool testTransformDoesNotInvalidateTessellation()
{
    auto canvasOwner = Canvas::create(Canvas::Backend::OpenGL, 0, 0);
    Canvas &canvas = *canvasOwner;
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
    return expect(
               stats.tessellationCacheHits == 1
                   && stats.tessellationCacheSize == 1,
               "a new physical AA fringe should reuse base tessellation")
        && expect(
               stats.aaCacheMisses == 2 && stats.aaCacheSize == 1,
               "only a repeatedly observed fill should enter the AA cache");
}

bool testDistinctShapesUseDistinctEntries()
{
    auto canvasOwner = Canvas::create(Canvas::Backend::OpenGL, 0, 0);
    Canvas &canvas = *canvasOwner;
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

bool testTranslatedPrimitivesReuseParameterizedMeshes()
{
    auto canvasOwner = Canvas::create(Canvas::Backend::OpenGL, 0, 0);
    Canvas &canvas = *canvasOwner;
    canvas.setSize(256, 256);

    Paint fill;
    fill.setStyle(Paint::Style::FILL);
    fill.setColor(Color(90, 170, 230));

    canvas.drawRect(RectF(10.0f, 12.0f, 40.0f, 24.0f), fill);
    canvas.drawRect(RectF(150.0f, 90.0f, 40.0f, 24.0f), fill);
    canvas.drawRoundRect(
        RectF(20.0f, 50.0f, 60.0f, 36.0f), 8.0f, fill);
    canvas.drawRoundRect(
        RectF(130.0f, 160.0f, 60.0f, 36.0f), 8.0f, fill);
    canvas.drawCircle(35.0f, 180.0f, 16.0f, fill);
    canvas.drawCircle(210.0f, 40.0f, 16.0f, fill);
    const Canvas::RenderStats stats = canvas.getRenderStats();
    return expect(
               stats.tessellationCacheMisses == 3
                   && stats.tessellationCacheHits == 3
                   && stats.tessellationCacheSize == 3,
               "translated primitives should reuse parameterized base meshes")
        && expect(
               stats.aaCacheMisses == 6
                   && stats.aaCacheHits == 0
                   && stats.aaCacheSize == 3,
               "second observations should admit parameterized AA meshes");
}

bool testOneShotFillsDoNotPolluteAaCache()
{
    auto canvasOwner = Canvas::create(Canvas::Backend::OpenGL, 0, 0);
    Canvas &canvas = *canvasOwner;
    canvas.setSize(512, 512);

    Paint fill;
    fill.setStyle(Paint::Style::FILL);
    fill.setColor(Color(70, 220, 190));
    for (int i = 0; i < 300; ++i) {
        canvas.drawRoundRect(
            RectF(8.0f, 8.0f, 20.0f + static_cast<float>(i), 18.0f),
            6.0f, fill);
    }

    const Canvas::RenderStats stats = canvas.getRenderStats();
    return expect(
        stats.aaCacheSize == 0,
        "one-shot animated fill geometry must not evict stable AA meshes");
}

bool testStrokeOnlyDoesNotPopulateFillCache()
{
    auto canvasOwner = Canvas::create(Canvas::Backend::OpenGL, 0, 0);
    Canvas &canvas = *canvasOwner;
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
    auto canvasOwner = Canvas::create(Canvas::Backend::OpenGL, 0, 0);
    Canvas &canvas = *canvasOwner;
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
    ok = expect(s2.strokeAaCacheMisses == 1
                    && s2.strokeAaCacheSize == 1,
                "a reused base stroke should admit its final AA geometry") && ok;

    canvas.drawPath(square, stroke);
    Canvas::RenderStats s3 = canvas.getRenderStats();
    ok = expect(s3.strokeAaCacheHits == 1
                    && s3.strokeAaCacheSize == 1,
                "a stable stroke should reuse its final AA geometry") && ok;

    // A different stroke width must not collide with the cached mesh.
    Paint thicker = stroke;
    thicker.setStrokeWidth(8.0f);
    canvas.drawPath(square, thicker);
    Canvas::RenderStats s4 = canvas.getRenderStats();
    ok = expect(s4.strokeCacheMisses == 2 && s4.strokeCacheSize == 2,
                "changing stroke width should produce a distinct cache entry") && ok;
    return ok;
}

bool testPeriodicDashPhaseReusesStrokeMesh()
{
    auto canvasOwner = Canvas::create(Canvas::Backend::OpenGL, 0, 0);
    Canvas &canvas = *canvasOwner;
    canvas.setSize(256, 256);

    Paint stroke;
    stroke.setStyle(Paint::Style::STROKE);
    stroke.setStrokeWidth(4.0f);
    stroke.setStrokeColor(Color(240, 200, 60));
    stroke.setDashPathEffect({9.0f, 6.0f}, 3.0f);
    const Path square = makeSquare(40.0f, 40.0f, 120.0f);

    canvas.drawPath(square, stroke);
    Paint nextCycle = stroke;
    nextCycle.setDashPathEffect({9.0f, 6.0f}, 18.0f);
    canvas.drawPath(square, nextCycle);

    const Canvas::RenderStats stats = canvas.getRenderStats();
    return expect(
               stats.strokeCacheMisses == 1
                   && stats.strokeCacheHits == 1,
               "dash phases separated by one pattern should share a mesh")
        && expect(
               stats.strokeAaCacheMisses == 1
                   && stats.strokeAaCacheSize == 1,
               "periodic dash reuse should admit final AA geometry");
}

bool testClipMaskSharesFillTessellationCache()
{
    auto canvasOwner = Canvas::create(Canvas::Backend::OpenGL, 0, 0);
    Canvas &canvas = *canvasOwner;
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
    ok = testTranslatedPrimitivesReuseParameterizedMeshes() && ok;
    ok = testOneShotFillsDoNotPolluteAaCache() && ok;
    ok = testStrokeOnlyDoesNotPopulateFillCache() && ok;
    ok = testIdenticalStrokeReusesMesh() && ok;
    ok = testPeriodicDashPhaseReusesStrokeMesh() && ok;
    ok = testClipMaskSharesFillTessellationCache() && ok;
    return ok ? 0 : 1;
}

#include <cmath>
#include <iostream>
#include <string>

#include "wsc/wsc.h"

namespace {

constexpr float kEpsilon = 0.001f;

bool nearlyEqual(float a, float b)
{
    return std::abs(a - b) <= kEpsilon;
}

bool expect(bool condition, const std::string &message)
{
    if (condition) {
        return true;
    }

    std::cerr << "EXPECTATION FAILED: " << message << std::endl;
    return false;
}

bool testMapAndInverseMap()
{
    auto canvasOwner = wsc::Canvas::create(wsc::Canvas::Backend::OpenGL, 0, 0);
    wsc::Canvas &canvas = *canvasOwner;
    canvas.setSize(400, 300);
    canvas.translate(10.0f, 20.0f);
    canvas.scale(2.0f, 3.0f);

    const wsc::PointF mapped = canvas.mapPoint(wsc::PointF(5.0f, 7.0f));
    wsc::PointF local;
    const bool inverseOk = canvas.inverseMapPoint(mapped, local);
    const wsc::RectF mappedRect = canvas.mapRect(wsc::RectF(0.0f, 0.0f, 20.0f, 10.0f));

    return expect(nearlyEqual(mapped.getX(), 20.0f), "mapPoint should apply translate and scale x")
        && expect(nearlyEqual(mapped.getY(), 41.0f), "mapPoint should apply translate and scale y")
        && expect(inverseOk, "inverseMapPoint should succeed for invertible matrix")
        && expect(nearlyEqual(local.getX(), 5.0f), "inverseMapPoint should recover local x")
        && expect(nearlyEqual(local.getY(), 7.0f), "inverseMapPoint should recover local y")
        && expect(nearlyEqual(mappedRect.getX(), 10.0f), "mapRect should map left")
        && expect(nearlyEqual(mappedRect.getY(), 20.0f), "mapRect should map top")
        && expect(nearlyEqual(mappedRect.getWidth(), 40.0f), "mapRect should scale width")
        && expect(nearlyEqual(mappedRect.getHeight(), 30.0f), "mapRect should scale height");
}

bool testClipBoundsAndQuickReject()
{
    auto canvasOwner = wsc::Canvas::create(wsc::Canvas::Backend::OpenGL, 0, 0);
    wsc::Canvas &canvas = *canvasOwner;
    canvas.setSize(320, 240);
    wsc::RectF clipBounds;
    const bool initialClip = canvas.getClipBounds(clipBounds);
    canvas.clipRect(wsc::RectF(20.0f, 30.0f, 100.0f, 80.0f));
    wsc::RectF clipped;
    const bool hasClip = canvas.getClipBounds(clipped);

    return expect(initialClip, "canvas-sized initial clip should exist after setSize")
        && expect(clipBounds.getWidth() == 320.0f, "initial clip should match canvas width")
        && expect(clipBounds.getHeight() == 240.0f, "initial clip should match canvas height")
        && expect(canvas.hasClip(), "clipRect should mark clip enabled")
        && expect(hasClip, "clipRect should keep clip bounds available")
        && expect(nearlyEqual(clipped.getX(), 20.0f), "clip bounds should keep x")
        && expect(nearlyEqual(clipped.getY(), 30.0f), "clip bounds should keep y")
        && expect(nearlyEqual(clipped.getWidth(), 100.0f), "clip bounds should keep width")
        && expect(nearlyEqual(clipped.getHeight(), 80.0f), "clip bounds should keep height")
        && expect(canvas.isPointInClip(wsc::PointF(50.0f, 50.0f)), "point inside clip should pass")
        && expect(!canvas.isPointInClip(wsc::PointF(10.0f, 50.0f)), "point outside clip should fail")
        && expect(!canvas.quickReject(wsc::RectF(40.0f, 40.0f, 12.0f, 12.0f)), "overlapping rect should not reject")
        && expect(canvas.quickReject(wsc::RectF(200.0f, 200.0f, 12.0f, 12.0f)), "outside rect should reject");
}

bool testHitTestingWithTransformAndClip()
{
    auto canvasOwner = wsc::Canvas::create(wsc::Canvas::Backend::OpenGL, 0, 0);
    wsc::Canvas &canvas = *canvasOwner;
    canvas.setSize(300, 220);
    canvas.clipRect(wsc::RectF(0.0f, 0.0f, 180.0f, 180.0f));
    canvas.translate(40.0f, 30.0f);

    wsc::Path rect;
    rect.addRect(wsc::RectF(0.0f, 0.0f, 60.0f, 50.0f));

    return expect(canvas.hitTestPathFill(rect, wsc::PointF(70.0f, 55.0f)), "transformed fill hit should pass")
        && expect(!canvas.hitTestPathFill(rect, wsc::PointF(20.0f, 55.0f)), "outside transformed fill should miss")
        && expect(!canvas.hitTestPathFill(rect, wsc::PointF(220.0f, 55.0f)), "outside clip should miss");
}

} // namespace

int main()
{
    const bool ok = testMapAndInverseMap()
        && testClipBoundsAndQuickReject()
        && testHitTestingWithTransformAndClip();
    return ok ? 0 : 1;
}

#include <iostream>
#include <string>

#include "wsc/wsc.h"

namespace {

bool expect(bool condition, const std::string &message)
{
    if (condition) {
        return true;
    }

    std::cerr << "EXPECTATION FAILED: " << message << std::endl;
    return false;
}

bool testStateSetters()
{
    wsc::Canvas canvas;
    wsc::CanvasAdapter adapter(canvas);
    adapter.setFillColor(wsc::Color(20, 40, 60, 200));
    adapter.setStrokeColor(wsc::Color(220, 200, 180, 160));
    adapter.setStrokeWidth(7.0f);
    adapter.setAlpha(0.5f);
    adapter.setFont("Inter");
    adapter.setTextSize(18.0f);

    return expect(adapter.fillPaint().getColor().getR() == 20, "fill color should update")
        && expect(adapter.strokePaint().getStrokeColor().getR() == 220, "stroke color should update")
        && expect(adapter.strokePaint().getStrokeWidth() == 7.0f, "stroke width should update")
        && expect(adapter.fillPaint().getAlpha() == adapter.strokePaint().getAlpha(), "alpha should update both paints")
        && expect(adapter.fillPaint().getFont() == "Inter", "font should update fill paint")
        && expect(adapter.strokePaint().getTextSize() == 18.0f, "text size should update stroke paint");
}

bool testCurrentPath()
{
    wsc::Canvas canvas;
    wsc::CanvasAdapter adapter(canvas);
    adapter.beginPath();
    adapter.moveTo(0.0f, 0.0f);
    adapter.lineTo(20.0f, 0.0f);
    adapter.lineTo(20.0f, 20.0f);
    adapter.closePath();

    return expect(adapter.currentPath().getContourCount() == 1, "adapter should build current path")
        && expect(adapter.currentPath().isClosed(), "adapter closePath should close current path");
}

bool testImageHandles()
{
    wsc::Canvas canvas;
    wsc::CanvasAdapter adapter(canvas);
    wsc::Image image;
    const std::uint32_t handle = adapter.registerImage(image);

    return expect(handle != 0, "image handle should be non-zero")
        && expect(adapter.image(handle) == &image, "registered image should resolve by handle")
        && expect(!adapter.drawImage(handle + 1, wsc::RectF(0.0f, 0.0f, 10.0f, 10.0f)), "unknown image handle should not draw")
        && expect(adapter.unregisterImage(handle), "registered image handle should unregister")
        && expect(adapter.image(handle) == nullptr, "unregistered image should no longer resolve");
}

} // namespace

int main()
{
    const bool ok = testStateSetters()
        && testCurrentPath()
        && testImageHandles();
    return ok ? 0 : 1;
}

// Minimal "60 seconds to draw the first frame" example, matching the README.
// The Software backend needs no window, GL context, or GPU resources, so this
// is the smallest possible WhatsCanvas program: it renders a rounded rectangle
// off-screen and writes the result to `first.ppm` in the current directory.

#include <wsc/wsc.h>

int main()
{
    auto canvas = wsc::Canvas::create(wsc::Canvas::Backend::Software, 256, 256);
    if (!canvas || !canvas->initializeContext()) {
        return 1;
    }

    canvas->beginFrame();

    wsc::Paint fill;
    fill.setColor(wsc::Color(40, 120, 240, 255));
    fill.setAntiAlias(true);
    canvas->drawRoundRect(wsc::RectF(40, 40, 176, 176), 24.0f, fill);

    canvas->endFrame();
    return canvas->savePixelsPPM("first.ppm") ? 0 : 1;
}

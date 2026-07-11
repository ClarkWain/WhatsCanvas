#include <wsc/wsc.h>

#include <cstdint>
#include <vector>

int main()
{
    wsc::Paint paint;
    paint.setColor(wsc::Color(32, 96, 192, 255));
    paint.setAntiAlias(true);
    paint.setTextSize(18.0f);
    paint.setFontFamily("Inter");

    wsc::Path path;
    path.moveTo(0.0f, 0.0f);
    path.lineTo(24.0f, 0.0f);
    path.lineTo(24.0f, 24.0f);
    path.close();

    wsc::FontFace face = wsc::FontFace::fromMemory(
        wsc::FontDescriptor("MemoryFont"),
        std::vector<std::uint8_t>{0, 1, 2, 3});
    if (!face.isValid()) {
        return 1;
    }

    wsc::FontFallbackChain chain("MemoryFont");
    chain.addFallbackFamily("sans-serif");
    if (chain.familiesInResolutionOrder().empty()) {
        return 2;
    }

    const auto backend =
#if defined(WHATSCANVAS_PACKAGE_USE_SOFTWARE)
        wsc::Canvas::Backend::Software;
#else
        wsc::Canvas::Backend::OpenGL;
#endif
    auto canvasOwner = wsc::Canvas::create(backend, 0, 0);
    if (!canvasOwner) {
        return 3;
    }
    wsc::Canvas &canvas = *canvasOwner;
    canvas.setSize(64, 64);
    (void)canvas.getWidth();
    (void)paint;
    (void)path;
    return 0;
}

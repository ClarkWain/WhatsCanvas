// Regression test for text pixel-grid snapping (crisp, non-blurry text).
//
// Glyphs are rasterized at an integer pixel size, so their texels only map 1:1
// to device pixels when the glyph quad lands on an integer boundary. Canvas now
// snaps glyph quads to the pixel grid under identity / translation transforms.
// This test renders the same black text on white at an integer x and at a
// fractional x and asserts the fractional placement stays just as crisp (its
// darkest covered pixel is not washed out relative to the integer placement).
//
// Runs on the pure-CPU software backend: deterministic, no GPU/context needed.

#include <algorithm>
#include <fstream>
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

std::string findSystemFont()
{
    const char *candidates[] = {
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
    };
    for (const char *p : candidates) {
        std::ifstream f(p, std::ios::binary);
        if (f.good()) {
            return p;
        }
    }
    return {};
}

// Render black text at `x` on a white canvas; return the darkest luma found in
// the text band (lower = crisper/more solid coverage), or 255 if nothing drawn.
int darkestTextLuma(float x, const std::string &fontPath)
{
    const int w = 96;
    const int h = 40;
    auto canvas = Canvas::create(Canvas::Backend::Software, w, h);
    if (!canvas) {
        return -1;
    }
    canvas->initializeContext();
    canvas->registerFontFace(FontFace::fromFile(FontDescriptor("AlignProbe"), fontPath));

    canvas->beginFrame();
    Paint bg;
    bg.setStyle(Paint::Style::FILL);
    bg.setColor(Color(255, 255, 255, 255));
    bg.setAntiAlias(false);
    canvas->drawRect(RectF(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)), bg);

    Paint text;
    text.setColor(Color(0, 0, 0, 255));
    text.setFontFamily("AlignProbe");
    text.setTextSize(16.0f);
    text.setAntiAlias(true);
    canvas->drawText("lll", x, 26.0f, text);
    canvas->endFrame();

    std::vector<unsigned char> px;
    if (!canvas->readPixelsRGBA(px) || px.size() != static_cast<std::size_t>(w) * h * 4u) {
        return -1;
    }

    int darkest = 255;
    for (int i = 0; i < w * h; ++i) {
        const int r = px[i * 4 + 0];
        const int g = px[i * 4 + 1];
        const int b = px[i * 4 + 2];
        const int luma = (r * 30 + g * 59 + b * 11) / 100;
        darkest = std::min(darkest, luma);
    }
    return darkest;
}

bool testFractionalTextStaysCrisp()
{
    const std::string fontPath = findSystemFont();
    if (fontPath.empty()) {
        std::cout << "Skipping text alignment test; no system font found." << std::endl;
        return true;
    }

    const int integerDarkest = darkestTextLuma(20.0f, fontPath);
    const int fractionalDarkest = darkestTextLuma(20.5f, fontPath);

    bool ok = expect(integerDarkest >= 0 && fractionalDarkest >= 0,
                     "software text rendering should produce readable pixels");
    if (!ok) {
        return false;
    }

    // Text must actually have rendered (dark coverage present, not a blank band).
    ok = expect(integerDarkest < 160, "integer-positioned text should render solid coverage") && ok;

    // The core assertion: fractional placement is NOT washed out relative to the
    // integer placement. Without pixel snapping, a fractional stem loses ~40-49%
    // of its coverage and its darkest pixel becomes markedly lighter. With
    // snapping, both land on the grid and stay equally crisp.
    ok = expect(fractionalDarkest <= integerDarkest + 24,
                "fractional-positioned text should stay as crisp as integer-positioned text")
         && ok;

    std::cout << "[TextPixelAlignmentTests] integer darkest=" << integerDarkest
              << " fractional darkest=" << fractionalDarkest << std::endl;
    return ok;
}

} // namespace

int main()
{
    const bool ok = testFractionalTextStaysCrisp();
    if (ok) {
        std::cout << "[TextPixelAlignmentTests] PASS" << std::endl;
        return 0;
    }
    std::cerr << "[TextPixelAlignmentTests] FAIL" << std::endl;
    return 1;
}

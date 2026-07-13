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

// Count solidly-inked (near-black) pixels produced by black text rendered with
// a given logical text size under a given uniform canvas scale, placed so the
// on-screen size is the same for every (size, scale) pair.
int nearBlackCount(float textSize, float scale, const std::string &fontPath)
{
    const int w = 320;
    const int h = 128;
    auto canvas = Canvas::create(Canvas::Backend::Software, w, h);
    if (!canvas) {
        return -1;
    }
    canvas->initializeContext();
    canvas->registerFontFace(FontFace::fromFile(FontDescriptor("ScaleProbe"), fontPath));

    canvas->beginFrame();
    Paint bg;
    bg.setStyle(Paint::Style::FILL);
    bg.setColor(Color(255, 255, 255, 255));
    bg.setAntiAlias(false);
    canvas->drawRect(RectF(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)), bg);

    Paint text;
    text.setColor(Color(0, 0, 0, 255));
    text.setFontFamily("ScaleProbe");
    text.setTextSize(textSize);
    text.setAntiAlias(true);

    // Same device-space origin (16, 72) and device text height regardless of the
    // (size, scale) split, so the two renders are directly comparable.
    canvas->save();
    canvas->scale(scale, scale);
    canvas->drawText("HHHH", 16.0f / scale, 72.0f / scale, text);
    canvas->restore();
    canvas->endFrame();

    std::vector<unsigned char> px;
    if (!canvas->readPixelsRGBA(px) || px.size() != static_cast<std::size_t>(w) * h * 4u) {
        return -1;
    }

    int count = 0;
    for (int i = 0; i < w * h; ++i) {
        const int r = px[i * 4 + 0];
        const int g = px[i * 4 + 1];
        const int b = px[i * 4 + 2];
        const int luma = (r * 30 + g * 59 + b * 11) / 100;
        if (luma < 30) {
            ++count;
        }
    }
    return count;
}

bool testScaledTextRasterizedAtDeviceResolution()
{
    const std::string fontPath = findSystemFont();
    if (fontPath.empty()) {
        std::cout << "Skipping scaled-text test; no system font found." << std::endl;
        return true;
    }

    // Reference: a natively large glyph at scale 1 (true device resolution).
    const int reference = nearBlackCount(64.0f, 1.0f, fontPath);
    // Under test: a small glyph magnified 4x to the same on-screen size. With
    // device-resolution rasterization this rasterizes at ~64px and matches the
    // reference. Without it, the 16px bitmap is bilinearly magnified into a
    // washed-out blur with far fewer solidly-inked pixels.
    const int scaledUp = nearBlackCount(16.0f, 4.0f, fontPath);

    bool ok = expect(reference > 0 && scaledUp > 0,
                     "both reference and scaled text should render solid ink");
    if (!ok) {
        return false;
    }

    ok = expect(scaledUp >= reference * 6 / 10,
                "4x-scaled text should keep >=60% of the native-resolution solid ink (crisp, not magnified blur)")
         && ok;

    std::cout << "[TextPixelAlignmentTests] reference nearBlack=" << reference
              << " scaledUp nearBlack=" << scaledUp << std::endl;
    return ok;
}

} // namespace

int main()
{
    bool ok = testFractionalTextStaysCrisp();
    ok = testScaledTextRasterizedAtDeviceResolution() && ok;
    if (ok) {
        std::cout << "[TextPixelAlignmentTests] PASS" << std::endl;
        return 0;
    }
    std::cerr << "[TextPixelAlignmentTests] FAIL" << std::endl;
    return 1;
}

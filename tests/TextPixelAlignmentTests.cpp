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

// Near-black ink count for text drawn at a device pixel ratio, placed so the
// device-space origin is fixed regardless of dpr.
int nearBlackCountDpr(float textSize, float dpr, const std::string &fontPath)
{
    const int w = 320;
    const int h = 128;
    auto canvas = Canvas::create(Canvas::Backend::Software, w, h);
    if (!canvas) {
        return -1;
    }
    canvas->initializeContext();
    canvas->registerFontFace(FontFace::fromFile(FontDescriptor("DprProbe"), fontPath));
    canvas->setDevicePixelRatio(dpr);

    canvas->beginFrame();
    Paint bg;
    bg.setStyle(Paint::Style::FILL);
    bg.setColor(Color(255, 255, 255, 255));
    bg.setAntiAlias(false);
    // Background is drawn in logical space too, so cover the whole logical area.
    canvas->drawRect(RectF(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)), bg);

    Paint text;
    text.setColor(Color(0, 0, 0, 255));
    text.setFontFamily("DprProbe");
    text.setTextSize(textSize);
    text.setAntiAlias(true);
    canvas->drawText("HHHH", 16.0f / dpr, 72.0f / dpr, text);
    canvas->endFrame();

    std::vector<unsigned char> px;
    if (!canvas->readPixelsRGBA(px) || px.size() != static_cast<std::size_t>(w) * h * 4u) {
        return -1;
    }
    int count = 0;
    for (int i = 0; i < w * h; ++i) {
        const int luma = (px[i * 4 + 0] * 30 + px[i * 4 + 1] * 59 + px[i * 4 + 2] * 11) / 100;
        if (luma < 30) {
            ++count;
        }
    }
    return count;
}

bool testDevicePixelRatioScalesAndStaysCrisp()
{
    const std::string fontPath = findSystemFont();
    if (fontPath.empty()) {
        std::cout << "Skipping devicePixelRatio test; no system font found." << std::endl;
        return true;
    }

    {
        auto canvas = Canvas::create(Canvas::Backend::Software, 32, 24);
        canvas->initializeContext();
        canvas->setDevicePixelRatio(2.0f);
        if (!expect(canvas->devicePixelRatio() == 2.0f, "devicePixelRatio getter should reflect the setter")) {
            return false;
        }
    }

    const int dpr1 = nearBlackCountDpr(16.0f, 1.0f, fontPath);      // 16px logical @ 1x
    const int dpr2 = nearBlackCountDpr(16.0f, 2.0f, fontPath);      // 16px logical @ 2x -> 32px device
    const int native32 = nearBlackCountDpr(32.0f, 1.0f, fontPath);  // native 32px reference

    bool ok = expect(dpr1 > 0 && dpr2 > 0 && native32 > 0, "all dpr renders should produce ink");
    if (!ok) {
        return false;
    }

    // DPR=2 renders the same logical text at ~2x each dimension -> markedly more ink.
    ok = expect(dpr2 >= dpr1 * 5 / 2, "devicePixelRatio=2 should render text noticeably larger than 1x") && ok;

    // And it must be crisp: a 16px glyph at 2x should carry the solid ink of a
    // native 32px glyph (device-resolution rasterization), not a magnified blur.
    ok = expect(dpr2 >= native32 * 6 / 10,
                "devicePixelRatio-scaled text should stay crisp at device resolution")
         && ok;

    std::cout << "[TextPixelAlignmentTests] dpr1=" << dpr1 << " dpr2=" << dpr2
              << " native32=" << native32 << std::endl;
    return ok;
}

// setMatrix must not drop the device pixel ratio: it is a device-space factor,
// and getMatrix must round-trip the logical matrix the caller set.
bool testSetMatrixPreservesDevicePixelRatio()
{
    auto canvas = Canvas::create(Canvas::Backend::Software, 64, 64);
    if (!canvas) {
        return expect(false, "software canvas should be created");
    }
    canvas->initializeContext();
    canvas->setDevicePixelRatio(2.0f);

    // Set an absolute (logical) transform via setMatrix.
    Matrix4 logical = Matrix4::identity();
    logical.set(3, 0, 10.0f); // column 3, row 0 = translate x
    logical.set(3, 1, 4.0f);  // column 3, row 1 = translate y
    canvas->setMatrix(logical);

    // getMatrix returns the logical transform, not the device-scaled one.
    const Matrix4 readback = canvas->getMatrix();
    bool ok = expect(std::abs(readback.at(3, 0) - 10.0f) < 0.01f
                         && std::abs(readback.at(3, 1) - 4.0f) < 0.01f,
                     "getMatrix should round-trip the logical translation set via setMatrix");

    // mapPoint applies the device pixel ratio: logical (0,0) -> device (20, 8).
    const PointF mapped = canvas->mapPoint(PointF(0.0f, 0.0f));
    ok = expect(std::abs(mapped.getX() - 20.0f) < 0.01f && std::abs(mapped.getY() - 8.0f) < 0.01f,
                "setMatrix should keep the 2x device pixel ratio (logical 10,4 -> device 20,8)")
         && ok;

    std::cout << "[TextPixelAlignmentTests] setMatrix readback=(" << readback.at(3, 0) << ","
              << readback.at(3, 1) << ") mapped=(" << mapped.getX() << "," << mapped.getY() << ")"
              << std::endl;
    return ok;
}

} // namespace

int main()
{
    bool ok = testFractionalTextStaysCrisp();
    ok = testScaledTextRasterizedAtDeviceResolution() && ok;
    ok = testDevicePixelRatioScalesAndStaysCrisp() && ok;
    ok = testSetMatrixPreservesDevicePixelRatio() && ok;
    if (ok) {
        std::cout << "[TextPixelAlignmentTests] PASS" << std::endl;
        return 0;
    }
    std::cerr << "[TextPixelAlignmentTests] FAIL" << std::endl;
    return 1;
}

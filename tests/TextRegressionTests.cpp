#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "canvas/Paint.h"
#include "text/BasicTextBackend.h"
#include "text/ITextBackend.h"
#include "wsc/Font.h"

namespace {

bool expect(bool condition, const std::string &message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << std::endl;
        return false;
    }
    return true;
}

Paint makeFallbackPaint()
{
    Paint paint;
    paint.setTextSize(16.0f);
    paint.setLetterSpacing(1.0f);
    return paint;
}

bool testAsciiRegression()
{
    std::unique_ptr<wsc::text::ITextBackend> backend = wsc::text::createPortableTextBackend();
    Paint paint = makeFallbackPaint();
    const std::string text = "Hello Canvas";
    const float width = backend->measureTextWidth(text, paint);
    const RectF bounds = backend->measureTextBounds(text, paint);
    const wsc::text::TextRenderResult render = backend->renderText(text, 12.0f, 24.0f, paint);

    return expect(width > 0.0f, "ASCII text should measure to a positive width")
        && expect(bounds.getWidth() == width, "ASCII bounds should match measured width")
        && expect(bounds.getHeight() > 0.0f, "ASCII bounds should report positive height")
        && expect(render.kind == wsc::text::TextRenderKind::Geometry, "ASCII fallback should render geometry")
        && expect(!render.vertices.empty(), "ASCII fallback should emit vertices")
        && expect(backend->hasGlyphForCodepoint('H', paint), "ASCII glyph should be available");
}

bool testChineseFallbackRegression()
{
    std::unique_ptr<wsc::text::ITextBackend> backend = wsc::text::createPortableTextBackend();
    Paint paint = makeFallbackPaint();
    const std::string text = u8"中文测试";
    const float width = backend->measureTextWidth(text, paint);
    const RectF bounds = backend->measureTextBounds(text, paint);
    const wsc::text::TextRenderResult render = backend->renderText(text, 0.0f, 0.0f, paint);

    return expect(width > 0.0f, "Chinese fallback text should measure to a positive width")
        && expect(bounds.getWidth() == width, "Chinese fallback bounds should match measured width")
        && expect(render.kind == wsc::text::TextRenderKind::Geometry, "Chinese fallback should render geometry")
        && expect(!render.vertices.empty(), "Chinese fallback should emit replacement glyph geometry")
        && expect(!backend->hasGlyphForCodepoint(0x4E2D, paint), "Chinese glyph should report missing without a font family");
}

bool testMixedLatinCjkRegression()
{
    std::unique_ptr<wsc::text::ITextBackend> backend = wsc::text::createPortableTextBackend();
    Paint paint = makeFallbackPaint();
    const std::string text = u8"Hello 中文 mix";
    const std::vector<wsc::text::TextLineBreak> wideLines = backend->breakLines(text, 400.0f, paint);
    const std::vector<wsc::text::TextLineBreak> narrowLines = backend->breakLines(text, 72.0f, paint);

    return expect(!wideLines.empty(), "mixed text should produce at least one line")
        && expect(wideLines.front().sourceStart == 0, "mixed text first line should start at zero")
        && expect(wideLines.front().sourceLength > 0, "mixed text line should map back to source")
        && expect(wideLines.front().width > 0.0f, "mixed text line should report width")
        && expect(narrowLines.size() >= 2, "narrow mixed text should wrap into multiple lines");
}

bool testUncoveredFallbackRegression()
{
    std::unique_ptr<wsc::text::ITextBackend> backend = wsc::text::createPortableTextBackend();
    Paint paint = makeFallbackPaint();
    const std::string text = std::string("range ") + "\xF0\x9F\x98\x80" + " fallback";
    const float width = backend->measureTextWidth(text, paint);
    const wsc::text::TextRenderResult render = backend->renderText(text, 0.0f, 0.0f, paint);

    return expect(width > 0.0f, "uncovered fallback text should measure to a positive width")
        && expect(render.kind == wsc::text::TextRenderKind::Geometry, "uncovered fallback should render geometry")
        && expect(!render.vertices.empty(), "uncovered fallback should emit replacement glyph geometry")
        && expect(render.missingGlyphs.size() == 1, "uncovered fallback should report one missing glyph hook")
        && expect(render.missingGlyphs.front().codepoint == 0x1F600, "missing glyph hook should report codepoint")
        && expect(!backend->hasGlyphForCodepoint(0x1F600, paint), "codepoint should report missing without a font family");
}

bool testFallbackRangeSuppressesMissingGlyphHook()
{
    std::unique_ptr<wsc::text::ITextBackend> backend = wsc::text::createPortableTextBackend();

    wsc::FontFace primary = wsc::FontFace::fromFile(wsc::FontDescriptor("Primary"), "primary.ttf");
    primary.addCodepointRange(32, 126);
    wsc::FontFace fallback = wsc::FontFace::fromFile(wsc::FontDescriptor("Fallback"), "fallback.ttf");
    fallback.addCodepointRange(0x1F300, 0x1FAFF);
    backend->registerFontFace(primary);
    backend->registerFontFace(fallback);

    wsc::FontFallbackChain chain("Primary");
    chain.addFallbackFamily("Fallback");
    backend->setFontFallbackChain(chain);

    Paint paint = makeFallbackPaint();
    paint.setFontFamily("Primary");
    const wsc::text::TextRenderResult render =
        backend->renderText(std::string("range ") + "\xF0\x9F\x98\x80", 0.0f, 0.0f, paint);

    return expect(render.kind == wsc::text::TextRenderKind::Geometry,
                  "declared fallback range text should still render geometry when font data is unavailable")
        && expect(render.missingGlyphs.empty(),
                  "covered fallback range should suppress missing glyph hooks");
}

} // namespace

int main()
{
    bool ok = true;
    ok = testAsciiRegression() && ok;
    ok = testChineseFallbackRegression() && ok;
    ok = testMixedLatinCjkRegression() && ok;
    ok = testUncoveredFallbackRegression() && ok;
    ok = testFallbackRangeSuppressesMissingGlyphHook() && ok;
    return ok ? 0 : 1;
}

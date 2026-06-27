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
    if (condition) {
        return true;
    }

    std::cerr << "EXPECTATION FAILED: " << message << std::endl;
    return false;
}

Paint makeTextPaint()
{
    Paint paint;
    paint.setTextSize(12.0f);
    paint.setFontFamily("Primary");
    return paint;
}

bool testFontRegistrationAndFallback()
{
    std::unique_ptr<wsc::text::ITextBackend> backend = wsc::text::createBasicTextBackend();
    const bool registeredPrimary =
        backend->registerFontFace(wsc::FontFace::fromFile(wsc::FontDescriptor("Primary"), "primary.ttf"));
    const bool registeredFallback =
        backend->registerFontFace(wsc::FontFace::fromMemory(wsc::FontDescriptor("Fallback"),
                                                            std::vector<std::uint8_t>{1, 2, 3, 4}));

    wsc::FontFallbackChain chain("Primary");
    chain.addFallbackFamily("Fallback");
    const bool chainSet = backend->setFontFallbackChain(chain);
    const std::vector<std::string> resolved = backend->resolveFontFamilies("Primary");

    return expect(registeredPrimary, "file font face should register")
        && expect(registeredFallback, "memory font face should register")
        && expect(chainSet, "fallback chain should register for known families")
        && expect(resolved.size() == 2, "fallback resolution should include primary and fallback")
        && expect(resolved[0] == "Primary", "primary family should resolve first")
        && expect(resolved[1] == "Fallback", "fallback family should resolve second");
}

bool testLineBreakAndGlyphQuery()
{
    std::unique_ptr<wsc::text::ITextBackend> backend = wsc::text::createBasicTextBackend();
    Paint paint = makeTextPaint();
    const std::vector<wsc::text::TextLineBreak> lines =
        backend->breakLines("alpha beta gamma", 56.0f, paint);

    return expect(!lines.empty(), "line break query should return rows")
        && expect(lines.front().sourceStart == 0, "first line should start at source zero")
        && expect(lines.front().sourceLength > 0, "first line should map to source text")
        && expect(lines.front().width > 0.0f, "line break should report measured width")
        && expect(backend->hasGlyphForCodepoint('A', paint), "ASCII glyph should be available");
}

bool testDiagnosticsForRejectedFallback()
{
    std::unique_ptr<wsc::text::ITextBackend> backend = wsc::text::createBasicTextBackend();
    wsc::FontFallbackChain chain("Missing");
    chain.addFallbackFamily("Fallback");
    const bool chainSet = backend->setFontFallbackChain(chain);
    const std::vector<wsc::text::TextBackendDiagnostic> diagnostics = backend->diagnostics();

    return expect(!chainSet, "unknown fallback chain should be rejected")
        && expect(!diagnostics.empty(), "rejected fallback chain should add diagnostics");
}

bool testPortableBackendUsesGeometryPath()
{
    std::unique_ptr<wsc::text::ITextBackend> backend = wsc::text::createPortableTextBackend();
    Paint paint = makeTextPaint();
    paint.setFontFamily("Arial");

    const wsc::text::TextRenderResult rendered = backend->renderText("Portable text", 4.0f, 8.0f, paint);

    return expect(rendered.kind == wsc::text::TextRenderKind::Geometry,
                  "portable backend should use geometry text even when a font family is set")
        && expect(!rendered.vertices.empty(), "portable geometry text should produce vertices")
        && expect(backend->hasGlyphForCodepoint('A', paint), "portable backend should expose ASCII glyphs")
        && expect(!backend->hasGlyphForCodepoint(0x4F60, paint),
                  "portable backend should not claim native CJK glyph coverage");
}

} // namespace

int main()
{
    const bool ok = testFontRegistrationAndFallback()
        && testLineBreakAndGlyphQuery()
        && testDiagnosticsForRejectedFallback()
        && testPortableBackendUsesGeometryPath();
    return ok ? 0 : 1;
}

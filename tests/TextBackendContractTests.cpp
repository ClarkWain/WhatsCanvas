#include <iostream>
#include <fstream>
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

std::string findSystemFontPath()
{
    const std::vector<std::string> candidates = {
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/System/Library/Fonts/SFNS.ttf"
    };

    for (const std::string &path : candidates) {
        std::ifstream input(path, std::ios::binary);
        if (input.good()) {
            return path;
        }
    }
    return {};
}

bool testPortableBackendUsesGlyphAtlasForRegisteredFont()
{
    const std::string fontPath = findSystemFontPath();
    if (fontPath.empty()) {
        std::cout << "Skipping glyph atlas registered-font test; no known system font path found." << std::endl;
        return true;
    }

    std::unique_ptr<wsc::text::ITextBackend> backend = wsc::text::createPortableTextBackend();
    bool ok = expect(backend->registerFontFace(wsc::FontFace::fromFile(wsc::FontDescriptor("AtlasPrimary"),
                                                                       fontPath)),
                     "registered system font should be accepted") && true;

    Paint paint;
    paint.setTextSize(24.0f);
    paint.setFontFamily("AtlasPrimary");
    const wsc::text::TextRenderResult rendered = backend->renderText("Atlas", 4.0f, 8.0f, paint);

    ok = expect(rendered.kind == wsc::text::TextRenderKind::GlyphAtlas,
                "registered portable font should render through glyph atlas") && ok;
    ok = expect(rendered.atlasWidth > 0 && rendered.atlasHeight > 0,
                "glyph atlas render should expose atlas dimensions") && ok;
    ok = expect(!rendered.atlasAlphaPixels.empty(),
                "glyph atlas render should expose atlas alpha pixels") && ok;
    ok = expect(!rendered.glyphAtlasQuads.empty(),
                "glyph atlas render should emit glyph quads") && ok;
    ok = expect(!rendered.atlasDirtyRects.empty(),
                "first glyph atlas render should expose dirty atlas rectangles") && ok;

    const wsc::text::TextRenderResult cached = backend->renderText("Atlas", 4.0f, 8.0f, paint);
    ok = expect(cached.kind == wsc::text::TextRenderKind::GlyphAtlas,
                "cached registered font render should still use glyph atlas") && ok;
    ok = expect(cached.atlasDirtyRects.empty(),
                "cached glyph atlas render should not dirty atlas rectangles") && ok;
    return ok;
}

bool testPortableBackendResolvesFallbackGlyphRange()
{
    std::unique_ptr<wsc::text::ITextBackend> backend = wsc::text::createPortableTextBackend();

    wsc::FontFace primary = wsc::FontFace::fromFile(wsc::FontDescriptor("Primary"), "primary.ttf");
    primary.addCodepointRange(32, 126);
    wsc::FontFace fallback = wsc::FontFace::fromFile(wsc::FontDescriptor("Fallback"), "fallback.ttf");
    fallback.addCodepointRange(0x1F300, 0x1FAFF);

    bool ok = expect(backend->registerFontFace(primary), "primary face with ASCII range should register");
    ok = expect(backend->registerFontFace(fallback), "fallback face with declared range should register") && ok;

    wsc::FontFallbackChain chain("Primary");
    chain.addFallbackFamily("Fallback");
    ok = expect(backend->setFontFallbackChain(chain), "fallback chain should register") && ok;

    Paint paint = makeTextPaint();
    ok = expect(backend->hasGlyphForCodepoint(0x1F600, paint),
                "covered codepoint should resolve through fallback range") && ok;
    ok = expect(!backend->hasGlyphForCodepoint(0x4E2D, paint),
                "uncovered codepoint should report missing glyph") && ok;
    ok = expect(!backend->hasGlyphForCodepoint(0x4E2D, paint),
                "duplicate missing glyph query should remain missing") && ok;
    const std::vector<wsc::text::TextBackendDiagnostic> diagnostics = backend->diagnostics();
    ok = expect(!diagnostics.empty(), "missing glyph query should add diagnostics") && ok;
    ok = expect(diagnostics.back().codepoint == 0x4E2D, "missing glyph diagnostic should include codepoint") && ok;
    ok = expect(diagnostics.back().fontFamily == "Primary", "missing glyph diagnostic should include requested family") && ok;
    ok = expect(diagnostics.size() == 1, "duplicate missing glyph diagnostics should be coalesced") && ok;
    return ok;
}

bool testPortableBackendShapesFallbackFontSegments()
{
    const std::string fontPath = findSystemFontPath();
    if (fontPath.empty()) {
        std::cout << "Skipping fallback font segment shaping test; no known system font path found." << std::endl;
        return true;
    }

    std::unique_ptr<wsc::text::ITextBackend> backend = wsc::text::createPortableTextBackend();

    wsc::FontFace primary = wsc::FontFace::fromFile(wsc::FontDescriptor("SegmentPrimary"), fontPath);
    primary.addCodepointRange('A', 'A');
    wsc::FontFace fallback = wsc::FontFace::fromFile(wsc::FontDescriptor("SegmentFallback"), fontPath);
    fallback.addCodepointRange('B', 'B');

    bool ok = expect(backend->registerFontFace(primary), "segment primary font should register");
    ok = expect(backend->registerFontFace(fallback), "segment fallback font should register") && ok;

    wsc::FontFallbackChain chain("SegmentPrimary");
    chain.addFallbackFamily("SegmentFallback");
    ok = expect(backend->setFontFallbackChain(chain), "segment fallback chain should register") && ok;

    Paint paint;
    paint.setTextSize(24.0f);
    paint.setFontFamily("SegmentPrimary");
    const wsc::text::TextRenderResult rendered = backend->renderText("AB", 0.0f, 0.0f, paint);

    ok = expect(rendered.kind == wsc::text::TextRenderKind::GlyphAtlas,
                "segmented fallback text should render through glyph atlas") && ok;
    ok = expect(rendered.glyphAtlasQuads.size() == 2,
                "segmented fallback text should emit both glyph quads") && ok;
    ok = expect(rendered.width > 0.0f, "segmented fallback text should measure positive width") && ok;
    return ok;
}

bool testOpenTypeShapingRequestFallsBackWithDiagnostic()
{
    wsc::text::BasicTextBackendOptions options;
    options.enableNativeText = false;
    options.shapingBackend = wsc::text::TextShapingBackend::OpenType;
    std::unique_ptr<wsc::text::ITextBackend> backend = wsc::text::createBasicTextBackend(options);
    const std::vector<wsc::text::TextBackendDiagnostic> diagnostics = backend->diagnostics();
    const bool openTypeAvailable = wsc::text::isOpenTypeShapingAvailable();

    bool hasUnavailableDiagnostic = false;
    for (const auto &diagnostic : diagnostics) {
        hasUnavailableDiagnostic = hasUnavailableDiagnostic
            || diagnostic.message.find("OpenType shaping backend is unavailable") != std::string::npos;
    }

    bool ok = true;
    if (openTypeAvailable) {
        ok = expect(!hasUnavailableDiagnostic,
                    "available OpenType shaping request should not report unavailable fallback") && ok;
    } else {
        ok = expect(!diagnostics.empty(),
                    "unavailable OpenType shaping request should add a diagnostic") && ok;
        ok = expect(diagnostics.front().severity == wsc::text::TextBackendDiagnostic::Severity::Warning,
                    "unavailable OpenType shaping diagnostic should be a warning") && ok;
        ok = expect(hasUnavailableDiagnostic,
                    "unavailable OpenType shaping diagnostic should name the fallback") && ok;
    }
    return ok;
}

} // namespace

int main()
{
    const bool ok = testFontRegistrationAndFallback()
        && testLineBreakAndGlyphQuery()
        && testDiagnosticsForRejectedFallback()
        && testPortableBackendUsesGeometryPath()
        && testPortableBackendUsesGlyphAtlasForRegisteredFont()
        && testPortableBackendResolvesFallbackGlyphRange()
        && testPortableBackendShapesFallbackFontSegments()
        && testOpenTypeShapingRequestFallsBackWithDiagnostic();
    return ok ? 0 : 1;
}

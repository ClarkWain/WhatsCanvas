#include <iostream>
#include <fstream>
#include <atomic>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "canvas/Paint.h"
#include "text/BasicTextBackend.h"
#include "text/FontRasterizer.h"
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
        && expect(backend->hasGlyphForCodepoint('A', paint), "ASCII glyph should be available")
        && expect(backend->hasGlyphForCodepoint(0x200F, paint), "bidi controls should not require font glyphs");
}

bool testBasicBackendUsesSystemFontFallbackWhenAvailable()
{
    bool hasDefaultPrimary = false;
    for (const wsc::FontFace &face : wsc::FontSystem::defaultSystemFontFaces()) {
        hasDefaultPrimary = hasDefaultPrimary || face.family() == wsc::FontSystem::kDefaultPrimaryFamily;
    }
    if (!hasDefaultPrimary) {
        return true;
    }

    std::unique_ptr<wsc::text::ITextBackend> backend = wsc::text::createBasicTextBackend();
    Paint paint;
    paint.setTextSize(18.0f);
    const std::vector<std::string> families = backend->resolveFontFamilies(std::string());
    const wsc::text::TextRenderResult rendered = backend->renderText("System text", 4.0f, 8.0f, paint);
    const wsc::text::TextMetrics metrics = backend->measureTextMetrics("System text", paint);

    return expect(!families.empty(), "basic backend should expose default system fallback families")
        && expect(families.front() == wsc::FontSystem::kDefaultPrimaryFamily,
                  "default system fallback should resolve primary first")
        && expect(rendered.kind == wsc::text::TextRenderKind::GlyphAtlas,
                  "basic backend should render default system text through glyph atlas when available")
        && expect(!rendered.glyphAtlasQuads.empty(),
                  "system glyph atlas text should emit glyph quads")
        && expect(metrics.lineHeight > 0.0f,
                  "system font metrics should expose a positive line height");
}

bool testCrLfLineBreakQuery()
{
    std::unique_ptr<wsc::text::ITextBackend> backend = wsc::text::createBasicTextBackend();
    Paint paint = makeTextPaint();
    const std::vector<wsc::text::TextLineBreak> lines =
        backend->breakLines("alpha\r\nbeta", 200.0f, paint);

    return expect(lines.size() == 2, "CRLF line break query should return two rows")
        && expect(lines[0].sourceStart == 0 && lines[0].sourceLength == 5,
                  "first CRLF backend row should map to alpha only")
        && expect(lines[1].sourceStart == 7 && lines[1].sourceLength == 4,
                  "second CRLF backend row should skip both CR and LF");
}

bool testCjkLineBreakQuery()
{
    std::unique_ptr<wsc::text::ITextBackend> backend = wsc::text::createPortableTextBackend();
    Paint paint;
    paint.setTextSize(12.0f);
    const std::vector<wsc::text::TextLineBreak> lines =
        backend->breakLines("\xe4\xbd\xa0\xe5\xa5\xbd\xe4\xb8\x96\xe7\x95\x8c", 12.0f, paint);

    return expect(lines.size() >= 2, "CJK line break query should wrap text without spaces")
        && expect(lines[0].sourceStart == 0, "first CJK line should start at source zero")
        && expect(lines[0].sourceLength > 0 && lines[0].sourceLength < 12,
                  "first CJK line should expose a partial UTF-8 span");
}

bool testLongWordLineBreakQuery()
{
    std::unique_ptr<wsc::text::ITextBackend> backend = wsc::text::createPortableTextBackend();
    Paint paint;
    paint.setTextSize(12.0f);
    const std::string text = "supercalifragilistic";
    const std::vector<wsc::text::TextLineBreak> lines = backend->breakLines(text, 18.0f, paint);

    return expect(lines.size() >= 2, "long unspaced words should wrap across lines")
        && expect(lines[0].sourceStart == 0, "first long-word line should start at source zero")
        && expect(lines[0].sourceLength > 0 && lines[0].sourceLength < text.size(),
                  "first long-word line should expose a partial source span");
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

bool testPortableBackendSkipsZeroWidthBreak()
{
    std::unique_ptr<wsc::text::ITextBackend> backend = wsc::text::createPortableTextBackend();
    Paint paint;
    paint.setTextSize(12.0f);
    const float withBreak = backend->measureTextWidth("A\xE2\x80\x8B" "B", paint);
    const float withoutBreak = backend->measureTextWidth("AB", paint);
    const wsc::text::TextRenderResult rendered =
        backend->renderText("A\xE2\x80\x8B" "B", 0.0f, 0.0f, paint);

    return expect(withBreak == withoutBreak, "zero-width break should not affect text measurement")
        && expect(backend->hasGlyphForCodepoint(0x200B, paint),
                  "zero-width break should not require a font glyph")
        && expect(rendered.missingGlyphs.empty(),
                  "zero-width break should not report missing glyphs");
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

std::string findColorSystemFontPath()
{
    const std::vector<std::string> candidates = {
        "C:/Windows/Fonts/seguiemj.ttf",
        "/System/Library/Fonts/Apple Color Emoji.ttc",
        "/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf",
        "/usr/share/fonts/google-noto-color-emoji/NotoColorEmoji.ttf"
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
    const wsc::text::TextMetrics metrics = backend->measureTextMetrics("Atlas", paint);

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
    ok = expect(metrics.width > 0.0f, "registered font metrics should report positive width") && ok;
    ok = expect(metrics.ascent < 0.0f, "registered font metrics should expose real negative ascent") && ok;
    ok = expect(metrics.descent > 0.0f, "registered font metrics should expose real positive descent") && ok;
    ok = expect(metrics.lineHeight >= metrics.descent - metrics.ascent,
                "registered font metrics should expose a usable line height") && ok;

    const wsc::text::TextRenderResult cached = backend->renderText("Atlas", 4.0f, 8.0f, paint);
    ok = expect(cached.kind == wsc::text::TextRenderKind::GlyphAtlas,
                "cached registered font render should still use glyph atlas") && ok;
    ok = expect(cached.atlasDirtyRects.empty(),
                "cached glyph atlas render should not dirty atlas rectangles") && ok;
    return ok;
}

bool testPortableBackendUsesRgbaAtlasForColorGlyphs()
{
    const std::string fontPath = findColorSystemFontPath();
    if (fontPath.empty()) {
        std::cout << "Skipping color glyph atlas test; no known color system font path found." << std::endl;
        return true;
    }

    wsc::FontFace face = wsc::FontFace::fromFile(wsc::FontDescriptor("ColorPrimary"), fontPath);
    wsc::text::FontRasterizer rasterizer;
    const auto tables = rasterizer.colorFontTables(face);
    if (!tables || !tables->colr || !tables->cpal || !rasterizer.hasGlyph(face, 0x1F600u)) {
        std::cout << "Skipping color glyph atlas test; color font does not expose COLR/CPAL grinning face." << std::endl;
        return true;
    }
    const auto colorGlyph = rasterizer.rasterizeGlyph(face, 0x1F600u, 40.0f);
    if (!colorGlyph || colorGlyph->bitmap.format != wsc::text::GlyphBitmapFormat::RGBA) {
        std::cout << "Skipping color glyph atlas test; color font is present but portable RGBA rasterization is unavailable." << std::endl;
        return true;
    }

    std::unique_ptr<wsc::text::ITextBackend> backend = wsc::text::createPortableTextBackend();
    bool ok = expect(backend->registerFontFace(face), "color system font should register");

    Paint paint;
    paint.setTextSize(40.0f);
    paint.setFontFamily("ColorPrimary");
    const wsc::text::TextRenderResult rendered = backend->renderText("\xF0\x9F\x98\x80", 0.0f, 0.0f, paint);

    ok = expect(rendered.kind == wsc::text::TextRenderKind::GlyphAtlas,
                "color glyph text should render through glyph atlas") && ok;
    ok = expect(rendered.atlasPixelFormat == wsc::text::GlyphAtlasPixelFormat::RGBA,
                "color glyph text should expose an RGBA atlas") && ok;
    ok = expect(!rendered.atlasRgbaPixels.empty(),
                "color glyph text should expose atlas RGBA pixels") && ok;
    ok = expect(!rendered.glyphAtlasQuads.empty(),
                "color glyph text should emit atlas quads") && ok;
    return ok;
}

bool testFontRasterizerCachePolicy()
{
    wsc::text::FontRasterizer rasterizer;
    rasterizer.clearCache();
    rasterizer.setCacheCapacity(2);

    const std::vector<std::uint8_t> invalidBytes = {1, 2, 3, 4};
    wsc::FontFace first = wsc::FontFace::fromMemory(wsc::FontDescriptor("CacheOne"), invalidBytes);
    wsc::FontFace second = wsc::FontFace::fromMemory(wsc::FontDescriptor("CacheTwo"), invalidBytes);
    wsc::FontFace third = wsc::FontFace::fromMemory(wsc::FontDescriptor("CacheThree"), invalidBytes);

    const bool firstLoad = rasterizer.hasGlyph(first, 'A');
    const bool firstCachedLoad = rasterizer.hasGlyph(first, 'B');
    const bool secondLoad = rasterizer.hasGlyph(second, 'A');
    const bool thirdLoad = rasterizer.hasGlyph(third, 'A');
    const wsc::text::FontRasterizerCacheStats stats = rasterizer.cacheStats();

    bool ok = expect(!firstLoad && !firstCachedLoad && !secondLoad && !thirdLoad,
                     "invalid cached font faces should not claim glyph coverage");
    ok = expect(stats.capacity == 2, "font rasterizer cache should expose configured capacity") && ok;
    ok = expect(stats.faceCount == 2, "font rasterizer cache should enforce face capacity") && ok;
    ok = expect(stats.hitCount >= 1, "font rasterizer cache should count repeated face hits") && ok;
    ok = expect(stats.missCount >= 3, "font rasterizer cache should count new face misses") && ok;
    ok = expect(stats.evictionCount >= 1, "font rasterizer cache should evict least recently used faces") && ok;

    rasterizer.clearCache();
    rasterizer.setCacheCapacity(64);
    const wsc::text::FontRasterizerCacheStats cleared = rasterizer.cacheStats();
    ok = expect(cleared.faceCount == 0, "font rasterizer cache clear should release loaded faces") && ok;
    ok = expect(cleared.hitCount == 0 && cleared.missCount == 0 && cleared.evictionCount == 0,
                "font rasterizer cache clear should reset counters") && ok;
    return ok;
}

bool testFontRasterizerCacheThreadSafety()
{
    wsc::text::FontRasterizer rasterizer;
    rasterizer.clearCache();
    rasterizer.setCacheCapacity(4);

    const std::vector<std::uint8_t> invalidBytes = {1, 2, 3, 4};
    wsc::FontFace first = wsc::FontFace::fromMemory(wsc::FontDescriptor("ThreadCacheOne"), invalidBytes);
    wsc::FontFace second = wsc::FontFace::fromMemory(wsc::FontDescriptor("ThreadCacheTwo"), invalidBytes);
    std::atomic<int> completed{0};

    auto query = [&](const wsc::FontFace &face, std::uint32_t baseCodepoint) {
        for (int i = 0; i < 128; ++i) {
            (void)rasterizer.hasGlyph(face, baseCodepoint + static_cast<std::uint32_t>(i % 8));
            (void)rasterizer.glyphMetrics(face, baseCodepoint + static_cast<std::uint32_t>(i % 8), 16.0f);
            (void)rasterizer.cacheStats();
        }
        ++completed;
    };

    std::thread firstThread(query, std::cref(first), static_cast<std::uint32_t>('A'));
    std::thread secondThread(query, std::cref(second), static_cast<std::uint32_t>('a'));
    std::thread capacityThread([&]() {
        for (int i = 0; i < 64; ++i) {
            rasterizer.setCacheCapacity((i % 3) + 1);
            (void)rasterizer.cacheStats();
        }
        ++completed;
    });

    firstThread.join();
    secondThread.join();
    capacityThread.join();

    const wsc::text::FontRasterizerCacheStats stats = rasterizer.cacheStats();
    bool ok = expect(completed == 3, "font rasterizer cache threads should complete");
    ok = expect(stats.faceCount <= stats.capacity,
                "threaded font rasterizer cache access should preserve capacity invariant") && ok;
    rasterizer.clearCache();
    rasterizer.setCacheCapacity(64);
    return ok;
}

bool testPortableBackendAppliesSimpleKerning()
{
    const std::string fontPath = findSystemFontPath();
    if (fontPath.empty()) {
        std::cout << "Skipping simple kerning test; no known system font path found." << std::endl;
        return true;
    }

    wsc::FontFace face = wsc::FontFace::fromFile(wsc::FontDescriptor("KerningPrimary"), fontPath);
    wsc::text::FontRasterizer rasterizer;
    const auto a = rasterizer.glyphMetrics(face, 'A', 48.0f);
    const auto v = rasterizer.glyphMetrics(face, 'V', 48.0f);
    if (!a || !v) {
        std::cout << "Skipping simple kerning test; font does not expose A/V glyph metrics." << std::endl;
        return true;
    }

    const auto kerning = rasterizer.glyphKerning(face, a->glyphIndex, v->glyphIndex, 48.0f);
    if (!kerning || *kerning == 0.0f) {
        std::cout << "Skipping simple kerning test; font has no A/V kern pair." << std::endl;
        return true;
    }

    std::unique_ptr<wsc::text::ITextBackend> backend = wsc::text::createPortableTextBackend();
    bool ok = expect(backend->registerFontFace(face), "kerning test font should register");

    Paint paint;
    paint.setTextSize(48.0f);
    paint.setFontFamily("KerningPrimary");
    const float measured = backend->measureTextWidth("AV", paint);
    const float expected = std::max(0.0f, a->advanceX + *kerning) + v->advanceX;

    ok = expect(std::abs(measured - expected) < 0.01f,
                "portable simple shaping should apply registered-font glyph kerning") && ok;
    return ok;
}

bool testRasterTextFailureAddsDiagnostic()
{
    std::unique_ptr<wsc::text::ITextBackend> backend = wsc::text::createPortableTextBackend();
    wsc::FontFace broken = wsc::FontFace::fromMemory(wsc::FontDescriptor("BrokenRaster"),
                                                     std::vector<std::uint8_t>{1, 2, 3, 4});
    bool ok = expect(backend->registerFontFace(broken), "broken memory font model should register for diagnostics");

    Paint paint;
    paint.setTextSize(18.0f);
    paint.setFontFamily("BrokenRaster");
    const wsc::text::TextRenderResult rendered = backend->renderText("A", 0.0f, 0.0f, paint);
    const std::vector<wsc::text::TextBackendDiagnostic> diagnostics = backend->diagnostics();

    bool sawRasterDiagnostic = false;
    for (const wsc::text::TextBackendDiagnostic &diagnostic : diagnostics) {
        sawRasterDiagnostic = sawRasterDiagnostic
            || diagnostic.message.find("Raster text shaping failed") != std::string::npos;
    }

    ok = expect(rendered.kind == wsc::text::TextRenderKind::Geometry,
                "failed raster text should fall back to geometry rendering") && ok;
    ok = expect(sawRasterDiagnostic,
                "failed raster text render should add a backend diagnostic") && ok;
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

bool testTextBackendCapabilityMatrix()
{
    const std::vector<wsc::text::TextBackendCapability> capabilities =
        wsc::text::queryTextBackendCapabilities();

    bool sawPortable = false;
    bool sawDirectWrite = false;
    bool sawCoreText = false;
    for (const wsc::text::TextBackendCapability &capability : capabilities) {
        if (capability.kind == wsc::text::TextBackendKind::Portable) {
            sawPortable = capability.available
                && capability.supportsFontRegistration
                && capability.supportsGlyphAtlas
                && capability.supportsColorGlyphAtlas;
        } else if (capability.kind == wsc::text::TextBackendKind::DirectWrite) {
            sawDirectWrite = capability.nativePlatformAdapter && !capability.available;
        } else if (capability.kind == wsc::text::TextBackendKind::CoreText) {
            sawCoreText = capability.nativePlatformAdapter && !capability.available;
        }
    }

    return expect(sawPortable, "portable text backend capability should be advertised")
        && expect(sawDirectWrite, "DirectWrite adapter slot should be advertised as unavailable")
        && expect(sawCoreText, "CoreText adapter slot should be advertised as unavailable");
}

bool testUnavailableNativeTextAdaptersFallback()
{
    std::unique_ptr<wsc::text::ITextBackend> directWrite =
        wsc::text::createTextBackend(wsc::text::TextBackendKind::DirectWrite);
    std::unique_ptr<wsc::text::ITextBackend> coreText =
        wsc::text::createTextBackend(wsc::text::TextBackendKind::CoreText);

    const std::vector<wsc::text::TextBackendDiagnostic> directWriteDiagnostics = directWrite->diagnostics();
    const std::vector<wsc::text::TextBackendDiagnostic> coreTextDiagnostics = coreText->diagnostics();

    return expect(!directWriteDiagnostics.empty(),
                  "DirectWrite backend request should add an unavailable-adapter diagnostic")
        && expect(directWriteDiagnostics.front().message.find("directwrite") != std::string::npos,
                  "DirectWrite diagnostic should name the adapter")
        && expect(!coreTextDiagnostics.empty(),
                  "CoreText backend request should add an unavailable-adapter diagnostic")
        && expect(coreTextDiagnostics.front().message.find("coretext") != std::string::npos,
                  "CoreText diagnostic should name the adapter");
}

} // namespace

int main()
{
    const bool ok = testFontRegistrationAndFallback()
        && testLineBreakAndGlyphQuery()
        && testBasicBackendUsesSystemFontFallbackWhenAvailable()
        && testCrLfLineBreakQuery()
        && testCjkLineBreakQuery()
        && testLongWordLineBreakQuery()
        && testDiagnosticsForRejectedFallback()
        && testPortableBackendUsesGeometryPath()
        && testPortableBackendSkipsZeroWidthBreak()
        && testPortableBackendUsesGlyphAtlasForRegisteredFont()
        && testPortableBackendUsesRgbaAtlasForColorGlyphs()
        && testFontRasterizerCachePolicy()
        && testFontRasterizerCacheThreadSafety()
        && testPortableBackendAppliesSimpleKerning()
        && testRasterTextFailureAddsDiagnostic()
        && testPortableBackendResolvesFallbackGlyphRange()
        && testPortableBackendShapesFallbackFontSegments()
        && testOpenTypeShapingRequestFallsBackWithDiagnostic()
        && testTextBackendCapabilityMatrix()
        && testUnavailableNativeTextAdaptersFallback();
    return ok ? 0 : 1;
}

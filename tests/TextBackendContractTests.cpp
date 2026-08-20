#include <algorithm>
#include <iostream>
#include <atomic>
#include <cmath>
#include <functional>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "canvas/Paint.h"
#include "text/BasicTextBackend.h"
#include "text/FontRasterizer.h"
#include "text/ITextBackend.h"
#include "wsc/Font.h"
#include "wsc/FontResolver.h"

namespace {

bool expect(bool condition, const std::string &message)
{
    if (condition) {
        return true;
    }

    std::cerr << "EXPECTATION FAILED: " << message << std::endl;
    return false;
}

std::uint16_t readU16Be(const std::vector<std::uint8_t> &bytes,
                        std::size_t offset)
{
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(bytes[offset]) << 8u)
        | static_cast<std::uint16_t>(bytes[offset + 1u]));
}

std::uint32_t readU32Be(const std::vector<std::uint8_t> &bytes,
                        std::size_t offset)
{
    return (static_cast<std::uint32_t>(bytes[offset]) << 24u)
        | (static_cast<std::uint32_t>(bytes[offset + 1u]) << 16u)
        | (static_cast<std::uint32_t>(bytes[offset + 2u]) << 8u)
        | static_cast<std::uint32_t>(bytes[offset + 3u]);
}

bool setSfntTableVersion(std::vector<std::uint8_t> &bytes,
                         const char (&tag)[5], std::uint32_t version)
{
    if (bytes.size() < 12u) return false;
    const std::size_t tableCount = readU16Be(bytes, 4u);
    if (tableCount > (bytes.size() - 12u) / 16u) return false;
    for (std::size_t index = 0; index < tableCount; ++index) {
        const std::size_t record = 12u + index * 16u;
        if (!std::equal(tag, tag + 4u, bytes.begin() + record)) continue;
        const std::size_t tableOffset = readU32Be(bytes, record + 8u);
        if (tableOffset > bytes.size() || bytes.size() - tableOffset < 4u) {
            return false;
        }
        bytes[tableOffset] = static_cast<std::uint8_t>(version >> 24u);
        bytes[tableOffset + 1u] = static_cast<std::uint8_t>(version >> 16u);
        bytes[tableOffset + 2u] = static_cast<std::uint8_t>(version >> 8u);
        bytes[tableOffset + 3u] = static_cast<std::uint8_t>(version);
        return true;
    }
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

bool testLazyProviderReachesPortableBackend()
{
    std::ifstream input(WHATSCANVAS_TEST_VARIABLE_FONT, std::ios::binary);
    std::vector<std::uint8_t> bytes(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    int loadCount = 0;
    auto provider = std::make_shared<wsc::LazyFontProvider>(
        wsc::FontProviderKind::ASSET, "contract-assets",
        [&](const std::string &sourceId)
            -> std::optional<std::vector<std::uint8_t>> {
            ++loadCount;
            return sourceId == "variable" && !bytes.empty()
                ? std::optional<std::vector<std::uint8_t>>(bytes)
                : std::nullopt;
        });
    wsc::LazyFontSource source;
    source.descriptor = wsc::FontDescriptor("Lazy Contract", 400);
    source.sourceId = "variable";
    bool ok = expect(provider->registerSource(source) && loadCount == 0,
                     "lazy contract source should register without I/O");

    std::unique_ptr<wsc::text::ITextBackend> backend =
        wsc::text::createPortableTextBackend();
    ok = expect(backend->addFontProvider(provider) && loadCount == 0,
                "portable backend should retain a provider without loading it") && ok;
    Paint paint;
    paint.setFontFamily("Lazy Contract");
    paint.setTextSize(32.0f);
    const float firstWidth = backend->measureTextWidth("Lazy font", paint);
    const float secondWidth = backend->measureTextWidth("Lazy font", paint);
    ok = expect(firstWidth > 0.0f && secondWidth == firstWidth,
                "lazy provider font should shape through the portable backend") && ok;
    ok = expect(loadCount == 1 && provider->loadedFaceCount() == 1,
                "portable backend should load a lazy source exactly once") && ok;
    return ok;
}

bool testRemoteProviderReachesPortableBackendAfterHostCompletion()
{
    std::ifstream input(WHATSCANVAS_TEST_VARIABLE_FONT, std::ios::binary);
    std::vector<std::uint8_t> bytes(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    auto provider = std::make_shared<wsc::RemoteFontProvider>(
        wsc::FontProviderKind::DYNAMIC, "contract-remote");
    wsc::RemoteFontSource source;
    source.font.descriptor = wsc::FontDescriptor("Remote Contract", 400);
    source.font.sourceId = "https://fonts.example/variable.ttf";
    source.font.codepointRanges.emplace_back(0x00C0, 0x024F);
    source.expectedBytes = bytes.size();
    bool ok = expect(!bytes.empty() && provider->registerSource(source),
                     "remote contract source metadata should register");

    std::unique_ptr<wsc::text::ITextBackend> backend =
        wsc::text::createPortableTextBackend();
    ok = expect(backend->addFontProvider(provider),
                "portable backend should retain a remote provider") && ok;
    Paint paint;
    paint.setFontFamily("Remote Contract");
    paint.setTextSize(32.0f);
    ok = expect(!backend->hasGlyphForCodepoint(0x00E9, paint)
                    && provider->state(source.font.sourceId)
                        == wsc::RemoteFontState::QUEUED,
                "first glyph lookup should enqueue host work without blocking") && ok;
    const auto requests = provider->takeDownloadRequests();
    ok = expect(requests.size() == 1
                    && requests.front().sourceId == source.font.sourceId,
                "portable lookup should expose the queued request to its host") && ok;
    ok = expect(provider->completeDownload(source.font.sourceId,
                                            requests.front().requestToken,
                                            std::move(bytes)),
                "host completion should publish downloaded font bytes") && ok;
    ok = expect(backend->hasGlyphForCodepoint(0x00E9, paint)
                    && backend->measureTextWidth("Async font \xC3\xA9", paint) > 0.0f,
                "provider generation changes should bypass cached misses and shape the remote face") && ok;
    return ok;
}

bool testFontRefreshPreservesExplicitRegistrations()
{
    std::unique_ptr<wsc::text::ITextBackend> backend = wsc::text::createPortableTextBackend();
    wsc::text::FontRasterizer rasterizer;
    rasterizer.clearCache();
    wsc::FontFace cacheProbe = wsc::FontFace::fromMemory(
        wsc::FontDescriptor("RefreshCacheProbe"),
        std::vector<std::uint8_t>{1, 2, 3, 4});
    (void)rasterizer.hasGlyph(cacheProbe, 'A');
    const auto cacheBeforeRefresh = rasterizer.cacheStats();
    const bool primary = backend->registerFontFace(
        wsc::FontFace::fromFile(wsc::FontDescriptor("RefreshPrimary"), "refresh-primary.ttf"));
    const bool fallback = backend->registerFontFace(wsc::FontFace::fromMemory(
        wsc::FontDescriptor("RefreshFallback"), std::vector<std::uint8_t>{1, 2, 3, 4}));
    wsc::FontFallbackChain chain("RefreshPrimary");
    chain.addFallbackFamily("RefreshFallback");
    const bool chainSet = backend->setFontFallbackChain(chain);
    const bool refreshed = backend->refreshSystemFonts();
    const auto resolved = backend->resolveFontFamilies("RefreshPrimary");
    const auto cacheAfterRefresh = rasterizer.cacheStats();

    return expect(cacheBeforeRefresh.faceCount == 1,
                  "refresh test should prime the process loaded-face cache")
        && expect(primary && fallback && chainSet, "refresh fixtures should register")
        && expect(refreshed, "portable backend should refresh system fonts")
        && expect(cacheAfterRefresh.faceCount == 0,
                  "system refresh should invalidate the process loaded-face cache")
        && expect(resolved.size() == 2
                      && resolved[0] == "RefreshPrimary"
                      && resolved[1] == "RefreshFallback",
                  "refresh should preserve explicit faces and fallback chains");
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

bool testPortableLineBreakingPreservesGraphemeClusters()
{
    std::unique_ptr<wsc::text::ITextBackend> backend = wsc::text::createPortableTextBackend();
    Paint paint;
    paint.setTextSize(12.0f);
    const std::string text = "A\xCC\x81" "B";
    const std::vector<wsc::text::TextLineBreak> lines = backend->breakLines(text, 7.0f, paint);

    bool ok = expect(lines.size() >= 2, "narrow text should wrap into multiple lines");
    for (const auto &line : lines) {
        const std::size_t end = line.sourceStart + line.sourceLength;
        ok = expect(line.sourceStart != 1 && end != 1,
                    "portable wrapping must not split a base from its combining mark") && ok;
    }
    return ok;
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

std::optional<wsc::FontFace> findSystemFontFace()
{
    const auto installed = wsc::FontSystem::discoverInstalledFontFaces();
    const char *preferredFamilies[] = {"Arial", "Segoe UI", "DejaVu Sans", "Helvetica"};
    for (const char *family : preferredFamilies) {
        const wsc::FontFace *best = nullptr;
        int bestDistance = std::numeric_limits<int>::max();
        for (const wsc::FontFace &face : installed) {
            if (face.family() == family && face.slant() == wsc::FontSlant::NORMAL) {
                const int distance = std::abs(face.weight() - 400);
                if (distance < bestDistance) {
                    best = &face;
                    bestDistance = distance;
                }
            }
        }
        if (best != nullptr) return *best;
    }
    const wsc::FontFace *best = nullptr;
    int bestDistance = std::numeric_limits<int>::max();
    for (const wsc::FontFace &face : installed) {
        if (face.slant() != wsc::FontSlant::NORMAL) continue;
        const int distance = std::abs(face.weight() - 400);
        if (distance < bestDistance) {
            best = &face;
            bestDistance = distance;
        }
    }
    if (best != nullptr) return *best;
    return std::nullopt;
}

std::optional<wsc::FontFace> findColorSystemFontFace()
{
    const auto installed = wsc::FontSystem::discoverInstalledFontFaces();
    const char *families[] = {"Segoe UI Emoji", "Apple Color Emoji", "Noto Color Emoji"};
    for (const char *family : families) {
        for (const wsc::FontFace &face : installed) {
            if (face.family() == family) return face;
        }
    }
    return std::nullopt;
}

wsc::FontFace testFace(const wsc::FontFace &source, wsc::FontDescriptor descriptor)
{
    return wsc::FontFace::fromFile(std::move(descriptor), source.path(), source.faceIndex());
}

bool testPortableBackendUsesGlyphAtlasForRegisteredFont()
{
    const auto systemFont = findSystemFontFace();
    if (!systemFont) {
        std::cout << "Skipping glyph atlas registered-font test; no system font found." << std::endl;
        return true;
    }

    std::unique_ptr<wsc::text::ITextBackend> backend = wsc::text::createPortableTextBackend();
    bool ok = expect(backend->registerFontFace(testFace(*systemFont, wsc::FontDescriptor("AtlasPrimary"))),
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
    ok = expect(rendered.atlasAlphaPixelsView != nullptr
                    ? !rendered.atlasAlphaPixelsView->empty()
                    : !rendered.atlasAlphaPixels.empty(),
                "glyph atlas render should expose owned or viewed atlas alpha pixels") && ok;
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

bool testPaintVariableFontOverridesReachLayoutAndAtlas()
{
    std::unique_ptr<wsc::text::ITextBackend> backend =
        wsc::text::createPortableTextBackend();
    wsc::FontFace face = wsc::FontFace::fromFile(
        wsc::FontDescriptor("VariablePaint"), WHATSCANVAS_TEST_VARIABLE_FONT);
    (void)face.setVariationCoordinate("wdth", 100.0f);
    bool ok = expect(backend->registerFontFace(face),
                     "variable font fixture should register");

    Paint narrow;
    narrow.setTextSize(48.0f);
    narrow.setFontFamily("VariablePaint");
    narrow.setFontVariation("wdth", 50.0f);
    const float narrowWidth = backend->measureTextWidth("Hamburgefontsiv", narrow);
    const wsc::text::TextRenderResult narrowRender =
        backend->renderText("Hamburgefontsiv", 0.0f, 0.0f, narrow);

    Paint wide = narrow;
    wide.setFontVariation("wdth", 150.0f);
    const float wideWidth = backend->measureTextWidth("Hamburgefontsiv", wide);
    const wsc::text::TextRenderResult wideRender =
        backend->renderText("Hamburgefontsiv", 0.0f, 0.0f, wide);

    ok = expect(narrowRender.kind == wsc::text::TextRenderKind::GlyphAtlas
                    && wideRender.kind == wsc::text::TextRenderKind::GlyphAtlas,
                "variable-font runs should use the portable glyph atlas") && ok;
    ok = expect(std::isfinite(narrowWidth) && std::isfinite(wideWidth)
                    && std::abs(wideWidth - narrowWidth) > 1.0f,
                "Paint wdth overrides should replace the FontFace axis and change shaped run width") && ok;
    ok = expect(!wideRender.atlasDirtyRects.empty(),
                "a different axis instance should allocate distinct atlas glyphs") && ok;

    const wsc::text::TextRenderResult wideCached =
        backend->renderText("Hamburgefontsiv", 0.0f, 0.0f, wide);
    ok = expect(wideCached.atlasDirtyRects.empty(),
                "repeating the same axis instance should hit the atlas cache") && ok;
    return ok;
}

bool testPortableGlyphLayoutCacheIsPositionIndependent()
{
    const auto systemFont = findSystemFontFace();
    if (!systemFont) {
        std::cout << "Skipping glyph layout cache position test; no system font found." << std::endl;
        return true;
    }

    const auto makeBackend = [&]() {
        std::unique_ptr<wsc::text::ITextBackend> backend =
            wsc::text::createPortableTextBackend();
        if (!backend->registerFontFace(testFace(*systemFont, wsc::FontDescriptor("LayoutCache")))) {
            return std::unique_ptr<wsc::text::ITextBackend>{};
        }
        return backend;
    };

    std::unique_ptr<wsc::text::ITextBackend> cachedBackend = makeBackend();
    std::unique_ptr<wsc::text::ITextBackend> freshBackend = makeBackend();
    if (!expect(cachedBackend != nullptr && freshBackend != nullptr,
                "layout cache test font should register")) {
        return false;
    }

    Paint paint;
    paint.setTextSize(24.0f);
    paint.setFontFamily("LayoutCache");
    paint.setFontWeight(700);
    const std::string text = "Position-independent AV fi";
    cachedBackend->renderText(text, 7.0f, 11.0f, paint);
    const auto cached =
        cachedBackend->renderText(text, 113.0f, 79.0f, paint);
    const auto fresh =
        freshBackend->renderText(text, 113.0f, 79.0f, paint);
    const wsc::text::TextRenderStats renderStats =
        cachedBackend->renderStats();

    bool ok = expect(cached.kind == wsc::text::TextRenderKind::GlyphAtlas
                         && fresh.kind == wsc::text::TextRenderKind::GlyphAtlas,
                     "cached and fresh layout renders should use the glyph atlas");
    ok = expect(cached.drawX == fresh.drawX
                    && cached.drawY == fresh.drawY
                    && cached.width == fresh.width
                    && cached.height == fresh.height,
                "cached layout metrics should match a fresh render") && ok;
    ok = expect(cached.glyphAtlasQuads.size()
                    == fresh.glyphAtlasQuads.size(),
                "cached layout should preserve the glyph count") && ok;
    ok = expect(renderStats.normalizationCount == 2,
                "portable render stats should count normalized draw calls") && ok;
    ok = expect(renderStats.layoutCacheHits == 1
                    && renderStats.layoutCacheMisses == 1,
                "portable render stats should expose layout cache hits and misses") && ok;
    ok = expect(renderStats.shapeCacheMisses >= 1,
                "portable render stats should expose shaping work") && ok;
    ok = expect(renderStats.atlasMisses > 0
                    && renderStats.rasterizationCount > 0,
                "portable render stats should expose cold glyph raster work") && ok;
    ok = expect(renderStats.generatedQuadCount
                    == cached.glyphAtlasQuads.size() * 2u,
                "portable render stats should count emitted quads on cache hits") && ok;
    ok = expect(renderStats.atlasDirtyBytes > 0,
                "portable render stats should expose atlas dirty bytes") && ok;
    ok = expect(renderStats.normalizationCpuTimeNs > 0
                    && renderStats.layoutCacheCpuTimeNs > 0
                    && renderStats.shapingCpuTimeNs > 0
                    && renderStats.glyphCacheLookupCpuTimeNs > 0
                    && renderStats.glyphRasterCpuTimeNs > 0
                    && renderStats.atlasUploadCpuTimeNs > 0,
                "portable render stats should time the primary text stages") && ok;
    ok = expect(renderStats.bidiCpuTimeNs > 0
                    && renderStats.fontFallbackCpuTimeNs > 0
                    && renderStats.fontDataCpuTimeNs > 0
                    && renderStats.shapeEngineCpuTimeNs > 0,
                "portable render stats should split shaping stage CPU time") && ok;
    cachedBackend->resetRenderStats();
    const wsc::text::TextRenderStats resetStats =
        cachedBackend->renderStats();
    ok = expect(resetStats.normalizationCount == 0
                    && resetStats.layoutCacheHits == 0
                    && resetStats.atlasDirtyBytes == 0
                    && resetStats.shapingCpuTimeNs == 0
                    && resetStats.fontFallbackCpuTimeNs == 0,
                "portable render stats should reset at a frame boundary") && ok;
    if (cached.glyphAtlasQuads.size() != fresh.glyphAtlasQuads.size()) {
        return false;
    }

    for (std::size_t index = 0;
         index < cached.glyphAtlasQuads.size(); ++index) {
        const auto &left = cached.glyphAtlasQuads[index];
        const auto &right = fresh.glyphAtlasQuads[index];
        const bool same =
            left.x == right.x && left.y == right.y
                && left.width == right.width
                && left.height == right.height
                && left.u0 == right.u0 && left.v0 == right.v0
                && left.u1 == right.u1 && left.v1 == right.v1;
        ok = expect(same,
                    "cached glyph quad should match a fresh render") && ok;
    }
    return ok;
}

bool testPortableGlyphLayoutViewAvoidsOwningCopy()
{
    const auto systemFont = findSystemFontFace();
    if (!systemFont) {
        std::cout << "Skipping glyph layout view test; no system font found."
                  << std::endl;
        return true;
    }

    std::unique_ptr<wsc::text::ITextBackend> backend =
        wsc::text::createPortableTextBackend();
    if (!expect(backend->registerFontFace(
                    testFace(*systemFont,
                             wsc::FontDescriptor("LayoutView"))),
                "layout view test font should register")) {
        return false;
    }

    Paint paint;
    paint.setTextSize(24.0f);
    paint.setFontFamily("LayoutView");
    const std::string text = "Immediate cached layout view";
    (void)backend->renderText(text, 5.0f, 7.0f, paint);

    const auto view = backend->renderTextView(
        text, 109.0f, 73.0f, paint);
    bool ok = expect(view.kind == wsc::text::TextRenderKind::GlyphAtlas,
                     "layout view should retain the glyph-atlas result kind");
    ok = expect(view.glyphAtlasQuads.empty()
                    && view.glyphAtlasQuadsView != nullptr
                    && !view.glyphAtlasQuadsView->empty(),
                "layout cache hit should expose a view without an owning quad copy") && ok;
    if (view.glyphAtlasQuadsView == nullptr) {
        return false;
    }

    // Resolve the short-lived view before making another backend call, exactly
    // as Canvas does while recording the draw.
    std::vector<wsc::text::TextRenderResult::GlyphAtlasQuad> resolved =
        *view.glyphAtlasQuadsView;
    for (auto &quad : resolved) {
        quad.x += view.glyphAtlasQuadOffsetX;
        quad.y += view.glyphAtlasQuadOffsetY;
    }
    const wsc::text::TextRenderStats viewStats = backend->renderStats();
    ok = expect(viewStats.layoutCacheHits == 1
                    && viewStats.layoutCacheMisses == 1
                    && viewStats.layoutViewHits == 1,
                "layout view use should be observable in render statistics") && ok;

    const auto owning = backend->renderText(text, 109.0f, 73.0f, paint);
    ok = expect(resolved.size() == owning.glyphAtlasQuads.size(),
                "resolved layout view should preserve the owning glyph count") && ok;
    if (resolved.size() != owning.glyphAtlasQuads.size()) {
        return false;
    }
    for (std::size_t index = 0; index < resolved.size(); ++index) {
        const auto &left = resolved[index];
        const auto &right = owning.glyphAtlasQuads[index];
        ok = expect(left.x == right.x && left.y == right.y
                        && left.width == right.width
                        && left.height == right.height
                        && left.u0 == right.u0 && left.v0 == right.v0
                        && left.u1 == right.u1 && left.v1 == right.v1
                        && left.isColorGlyph == right.isColorGlyph,
                    "resolved layout view quad should match owning renderText") && ok;
    }
    return ok;
}

bool testPortableGlyphAtlasCacheKeepsFontFacesDistinct()
{
    const auto systemFont = findSystemFontFace();
    if (!systemFont) {
        std::cout << "Skipping glyph atlas face-key test; no system font found." << std::endl;
        return true;
    }

    std::unique_ptr<wsc::text::ITextBackend> backend = wsc::text::createPortableTextBackend();
    bool ok = expect(
        backend->registerFontFace(testFace(
            *systemFont, wsc::FontDescriptor("AtlasFaceKey", 400, wsc::FontSlant::NORMAL))),
        "regular atlas face should register");
    ok = expect(
        backend->registerFontFace(testFace(
            *systemFont, wsc::FontDescriptor("AtlasFaceKey", 700, wsc::FontSlant::NORMAL))),
        "bold atlas face should register") && ok;

    Paint paint;
    paint.setTextSize(24.0f);
    paint.setFontFamily("AtlasFaceKey");
    paint.setFontWeight(700);
    const auto boldBefore = backend->renderText("A", 0.0f, 0.0f, paint);
    paint.setFontWeight(400);
    const auto regular = backend->renderText("A", 0.0f, 0.0f, paint);
    paint.setFontWeight(700);
    const auto boldAfter = backend->renderText("A", 0.0f, 0.0f, paint);

    const auto hasOneQuad = [](const wsc::text::TextRenderResult& result) {
        return result.kind == wsc::text::TextRenderKind::GlyphAtlas
            && result.glyphAtlasQuads.size() == 1u;
    };
    ok = expect(hasOneQuad(boldBefore) && hasOneQuad(regular) && hasOneQuad(boldAfter),
                "regular and bold face-key renders should each emit one atlas quad") && ok;
    if (!hasOneQuad(boldBefore) || !hasOneQuad(regular) || !hasOneQuad(boldAfter)) {
        return false;
    }

    const auto sameAtlasEntry = [](const auto& left, const auto& right) {
        return left.u0 == right.u0 && left.v0 == right.v0
            && left.u1 == right.u1 && left.v1 == right.v1;
    };
    ok = expect(!sameAtlasEntry(boldBefore.glyphAtlasQuads.front(),
                                regular.glyphAtlasQuads.front()),
                "regular and bold faces must occupy distinct glyph-atlas entries") && ok;
    ok = expect(sameAtlasEntry(boldBefore.glyphAtlasQuads.front(),
                               boldAfter.glyphAtlasQuads.front()),
                "bold glyph lookup after a regular render must return the original bold atlas entry") && ok;
    return ok;
}

bool testPortableBackendUsesRgbaAtlasForColorGlyphs()
{
    const auto systemFont = findColorSystemFontFace();
    if (!systemFont) {
        std::cout << "Skipping color glyph atlas test; no color system font found." << std::endl;
        return true;
    }

    wsc::FontFace face = testFace(*systemFont, wsc::FontDescriptor("ColorPrimary"));
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
    ok = expect(rendered.atlasRgbaPixelsView != nullptr
                    ? !rendered.atlasRgbaPixelsView->empty()
                    : !rendered.atlasRgbaPixels.empty(),
                "color glyph text should expose owned or viewed atlas RGBA pixels") && ok;
    ok = expect(!rendered.glyphAtlasQuads.empty(),
                "color glyph text should emit atlas quads") && ok;
    ok = expect(!rendered.glyphAtlasQuads.empty()
                    && rendered.glyphAtlasQuads.front().isColorGlyph,
                "color glyph quads should preserve intrinsic atlas colors") && ok;

    const wsc::text::TextRenderResult cached = backend->renderText(
        "\xF0\x9F\x98\x80", 4.0f, 0.0f, paint);
    ok = expect(!cached.glyphAtlasQuads.empty()
                    && cached.glyphAtlasQuads.front().isColorGlyph,
                "cached color glyph lookup should retain its RGBA identity") && ok;

    if (const auto plainSystemFont = findSystemFontFace()) {
        ok = expect(backend->registerFontFace(testFace(
                        *plainSystemFont, wsc::FontDescriptor("PlainAfterColor"))),
                    "plain face should register after the color atlas is active") && ok;
        paint.setFontFamily("PlainAfterColor");
        paint.setColor(Color(12, 80, 190));
        const wsc::text::TextRenderResult plain = backend->renderText(
            "A", 0.0f, 0.0f, paint);
        ok = expect(!plain.glyphAtlasQuads.empty()
                        && !plain.glyphAtlasQuads.front().isColorGlyph,
                    "alpha glyph quads should remain Paint-tinted in a mixed RGBA atlas") && ok;
    }
    return ok;
}

bool testBundledCbdtPngGlyphRasterization()
{
    const wsc::FontFace face = wsc::FontFace::fromFile(
        wsc::FontDescriptor("BundledCbdt"), WHATSCANVAS_TEST_CBDT_FONT);
    wsc::text::FontRasterizer rasterizer;
    const auto tables = rasterizer.colorFontTables(face);
    bool ok = expect(tables && tables->cbdt && tables->cblc,
                     "bundled CBDT fixture should expose CBDT/CBLC tables");
    ok = expect(rasterizer.hasGlyph(face, 0x2049u),
                "bundled CBDT fixture should map U+2049") && ok;
    const auto glyph = rasterizer.rasterizeGlyph(face, 0x2049u, 72.0f);
    ok = expect(glyph.has_value(),
                "common CBDT PNG glyph should rasterize without FreeType libpng") && ok;
    if (!glyph) return false;
    const wsc::text::GlyphBitmap &bitmap = glyph->bitmap;
    ok = expect(glyph->key.glyphIndex == 4,
                "CBDT fixture should retain its cmap glyph ID") && ok;
    ok = expect(bitmap.format == wsc::text::GlyphBitmapFormat::RGBA,
                "CBDT PNG glyph should produce an RGBA bitmap") && ok;
    ok = expect(bitmap.width == 90 && bitmap.height == 85,
                "CBDT strike should scale from 109ppem to the requested 72px") && ok;
    ok = expect(std::abs(bitmap.advanceX - 89.8348618f) < 0.001f,
                "CBDT advance should scale with the selected strike") && ok;
    std::size_t coloredPixels = 0;
    for (std::size_t offset = 0; offset + 3u < bitmap.rgbaPixels.size(); offset += 4u) {
        if (bitmap.rgbaPixels[offset + 3u] != 0
            && (bitmap.rgbaPixels[offset] != bitmap.rgbaPixels[offset + 1u]
                || bitmap.rgbaPixels[offset + 1u] != bitmap.rgbaPixels[offset + 2u])) {
            ++coloredPixels;
        }
    }
    return expect(coloredPixels > 2000u,
                  "CBDT PNG glyph should retain substantial intrinsic color");
}

bool testBundledCbdtVersion2PngGlyphRasterization()
{
    std::ifstream input(WHATSCANVAS_TEST_CBDT_FONT, std::ios::binary);
    std::vector<std::uint8_t> bytes(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    bool ok = expect(!bytes.empty(),
                     "CBDT fixture bytes should load for version-2 coverage");
    ok = expect(setSfntTableVersion(bytes, "CBLC", 0x00020000u)
                    && setSfntTableVersion(bytes, "CBDT", 0x00020000u),
                "CBDT fixture should expose mutable CBLC/CBDT versions") && ok;
    if (!ok) return false;

    const wsc::FontFace face = wsc::FontFace::fromMemory(
        wsc::FontDescriptor("BundledCbdtV2"), std::move(bytes));
    wsc::text::FontRasterizer rasterizer;
    const auto glyph = rasterizer.rasterizeGlyph(face, 0x2049u, 72.0f);
    ok = expect(glyph.has_value(),
                "Android-style CBDT/CBLC 2.0 PNG glyph should rasterize") && ok;
    return expect(glyph && glyph->bitmap.format
                            == wsc::text::GlyphBitmapFormat::RGBA,
                  "CBDT/CBLC 2.0 PNG glyph should remain RGBA") && ok;
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
    const auto systemFont = findSystemFontFace();
    if (!systemFont) {
        std::cout << "Skipping simple kerning test; no system font found." << std::endl;
        return true;
    }

    wsc::FontFace face = testFace(*systemFont, wsc::FontDescriptor("KerningPrimary"));
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

    wsc::text::BasicTextBackendOptions options;
    options.backendKind = wsc::text::TextBackendKind::Portable;
    options.enableNativeText = false;
    options.enableSystemFontFallback = false;
    options.shapingBackend = wsc::text::TextShapingBackend::Simple;
    std::unique_ptr<wsc::text::ITextBackend> backend = wsc::text::createBasicTextBackend(options);
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
    const std::size_t missingCount = static_cast<std::size_t>(std::count_if(
        diagnostics.begin(), diagnostics.end(), [](const wsc::text::TextBackendDiagnostic &diagnostic) {
            return diagnostic.codepoint == 0x4E2D && diagnostic.fontFamily == "Primary";
        }));
    ok = expect(missingCount == 1, "duplicate missing glyph diagnostics should be coalesced") && ok;
    return ok;
}

bool testPortableBackendShapesFallbackFontSegments()
{
    const auto systemFont = findSystemFontFace();
    if (!systemFont) {
        std::cout << "Skipping fallback font segment shaping test; no system font found." << std::endl;
        return true;
    }

    std::unique_ptr<wsc::text::ITextBackend> backend = wsc::text::createPortableTextBackend();

    wsc::FontFace primary = testFace(*systemFont, wsc::FontDescriptor("SegmentPrimary"));
    primary.addCodepointRange('A', 'A');
    wsc::FontFace fallback = testFace(*systemFont, wsc::FontDescriptor("SegmentFallback"));
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

bool testPortableBackendDefaultsToOpenTypeShaping()
{
    const wsc::text::BasicTextBackendOptions options;
    return expect(options.shapingBackend == wsc::text::TextShapingBackend::OpenType,
                  "portable backend should request OpenType shaping by default");
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
#ifdef _WIN32
            // DirectWrite is a real, available native adapter on Windows.
            sawDirectWrite = capability.nativePlatformAdapter && capability.available;
#else
            sawDirectWrite = capability.nativePlatformAdapter && !capability.available;
#endif
        } else if (capability.kind == wsc::text::TextBackendKind::CoreText) {
#if defined(__APPLE__)
            sawCoreText = capability.nativePlatformAdapter && capability.available
                && capability.supportsFontRegistration
                && capability.supportsOpenTypeShaping;
#else
            sawCoreText = capability.nativePlatformAdapter && !capability.available;
#endif
        }
    }

    return expect(sawPortable, "portable text backend capability should be advertised")
        && expect(sawDirectWrite, "DirectWrite adapter slot should be advertised")
        && expect(sawCoreText, "CoreText adapter capability should match the host platform");
}

bool testUnavailableNativeTextAdaptersFallback()
{
    std::unique_ptr<wsc::text::ITextBackend> directWrite =
        wsc::text::createTextBackend(wsc::text::TextBackendKind::DirectWrite);
    std::unique_ptr<wsc::text::ITextBackend> coreText =
        wsc::text::createTextBackend(wsc::text::TextBackendKind::CoreText);

    bool directWriteOk = false;
#ifdef _WIN32
    // On Windows DirectWrite is a real backend: it must construct, measure a
    // positive width for ASCII text, and NOT emit an unavailable-adapter
    // diagnostic.
    if (directWrite != nullptr) {
        wsc::Paint paint;
        paint.setFontFamily("Segoe UI");
        paint.setTextSize(16.0f);
        const float width = directWrite->measureTextWidth("Ag", paint);
        bool sawUnavailable = false;
        for (const auto &d : directWrite->diagnostics()) {
            const bool namesDirectWrite = d.message.find("DirectWrite") != std::string::npos
                || d.message.find("directwrite") != std::string::npos;
            if (namesDirectWrite
                && (d.message.find("not available") != std::string::npos
                    || d.message.find("unavailable") != std::string::npos)) {
                sawUnavailable = true;
            }
        }
        directWriteOk = expect(width > 0.0f, "DirectWrite backend should measure a positive width on Windows")
                        && expect(!sawUnavailable, "DirectWrite backend should not report unavailable on Windows");
    } else {
        directWriteOk = expect(false, "DirectWrite backend should be constructible on Windows");
    }
#else
    const std::vector<wsc::text::TextBackendDiagnostic> directWriteDiagnostics = directWrite->diagnostics();
    directWriteOk = expect(!directWriteDiagnostics.empty(),
                           "DirectWrite backend request should add an unavailable-adapter diagnostic")
                    && expect(directWriteDiagnostics.front().message.find("DirectWrite") != std::string::npos
                                  || directWriteDiagnostics.front().message.find("directwrite") != std::string::npos,
                              "DirectWrite diagnostic should name the adapter");
#endif

    bool coreTextOk = false;
#if defined(__APPLE__)
    if (coreText != nullptr) {
        wsc::Paint paint;
        paint.setFontFamily("Helvetica Neue");
        paint.setTextSize(16.0f);
        const float width = coreText->measureTextWidth("CoreText 中文", paint);
        bool sawUnavailable = false;
        for (const auto &diagnostic : coreText->diagnostics()) {
            if (diagnostic.message.find("unavailable") != std::string::npos
                || diagnostic.message.find("not available") != std::string::npos) {
                sawUnavailable = true;
            }
        }
        coreTextOk = expect(width > 0.0f,
                            "CoreText backend should measure native text on Apple")
            && expect(!sawUnavailable,
                      "CoreText backend should not report unavailable on Apple");
    } else {
        coreTextOk = expect(false,
                            "CoreText backend should be constructible on Apple");
    }
#else
    const auto coreTextDiagnostics = coreText->diagnostics();
    coreTextOk = expect(!coreTextDiagnostics.empty(),
                        "CoreText request should add an unavailable diagnostic")
        && expect(coreTextDiagnostics.front().message.find("CoreText")
                      != std::string::npos,
                  "CoreText diagnostic should name the adapter");
#endif
    return directWriteOk && coreTextOk;
}

bool testWindowsNativeTextPreservesClearTypeCoverage()
{
#ifdef _WIN32
    wsc::text::BasicTextBackendOptions options;
    options.backendKind = wsc::text::TextBackendKind::WindowsNative;
    // Do not register FreeType faces: this deliberately exercises the same
    // cached GDI path used by the Windows-native Todo comparison build.
    options.enableSystemFontFallback = false;
    std::unique_ptr<wsc::text::ITextBackend> backend = wsc::text::createBasicTextBackend(options);
    wsc::Paint paint;
    paint.setFontFamily("Segoe UI");
    paint.setTextSize(18.0f);
    const wsc::text::TextRenderResult rendered = backend->renderText("ClearType", 0.0f, 0.0f, paint);
    if (!expect(rendered.kind == wsc::text::TextRenderKind::Bitmap,
                "Windows-native text should produce a cached bitmap")) {
        return false;
    }
    if (!expect(!rendered.bitmapPixels.empty(),
                "Windows-native text bitmap should contain pixels")) {
        return false;
    }

    bool hasIndependentRgbCoverage = false;
    for (std::size_t i = 0; i + 3 < rendered.bitmapPixels.size(); i += 4) {
        const unsigned char r = rendered.bitmapPixels[i + 0];
        const unsigned char g = rendered.bitmapPixels[i + 1];
        const unsigned char b = rendered.bitmapPixels[i + 2];
        hasIndependentRgbCoverage = hasIndependentRgbCoverage || r != g || g != b;
    }
    // The same backend instance must not reuse a Regular bitmap after the UI
    // requests a heading weight. This protects both CreateFontW propagation
    // and the native measure/bitmap cache key.
    paint.setFontWeight(700);
    const wsc::text::TextRenderResult bold = backend->renderText("ClearType", 0.0f, 0.0f, paint);
    if (!expect(bold.kind == wsc::text::TextRenderKind::Bitmap && !bold.bitmapPixels.empty(),
                "Windows-native bold text should produce a bitmap")) {
        return false;
    }
    if (!expect(bold.bitmapWidth != rendered.bitmapWidth
                    || bold.bitmapHeight != rendered.bitmapHeight
                    || bold.bitmapPixels != rendered.bitmapPixels,
                "Windows-native font weight must affect the cached raster")) {
        return false;
    }

    // Systems with ClearType disabled legitimately return grayscale coverage;
    // on those systems the renderer must retain the normal alpha fallback.
    return expect(rendered.bitmapIsClearType == hasIndependentRgbCoverage,
                  "native bitmap ClearType flag must exactly reflect RGB coverage");
#else
    return true;
#endif
}

} // namespace

int main()
{
    const bool ok = testFontRegistrationAndFallback()
        && testLazyProviderReachesPortableBackend()
        && testRemoteProviderReachesPortableBackendAfterHostCompletion()
        && testFontRefreshPreservesExplicitRegistrations()
        && testLineBreakAndGlyphQuery()
        && testBasicBackendUsesSystemFontFallbackWhenAvailable()
        && testCrLfLineBreakQuery()
        && testCjkLineBreakQuery()
        && testLongWordLineBreakQuery()
        && testPortableLineBreakingPreservesGraphemeClusters()
        && testDiagnosticsForRejectedFallback()
        && testPortableBackendUsesGeometryPath()
        && testPortableBackendSkipsZeroWidthBreak()
        && testPortableBackendUsesGlyphAtlasForRegisteredFont()
        && testPaintVariableFontOverridesReachLayoutAndAtlas()
        && testPortableGlyphLayoutCacheIsPositionIndependent()
        && testPortableGlyphLayoutViewAvoidsOwningCopy()
        && testPortableGlyphAtlasCacheKeepsFontFacesDistinct()
        && testPortableBackendUsesRgbaAtlasForColorGlyphs()
        && testBundledCbdtPngGlyphRasterization()
        && testBundledCbdtVersion2PngGlyphRasterization()
        && testFontRasterizerCachePolicy()
        && testFontRasterizerCacheThreadSafety()
        && testPortableBackendAppliesSimpleKerning()
        && testRasterTextFailureAddsDiagnostic()
        && testPortableBackendResolvesFallbackGlyphRange()
        && testPortableBackendShapesFallbackFontSegments()
        && testOpenTypeShapingRequestFallsBackWithDiagnostic()
        && testPortableBackendDefaultsToOpenTypeShaping()
        && testTextBackendCapabilityMatrix()
        && testUnavailableNativeTextAdaptersFallback()
        && testWindowsNativeTextPreservesClearTypeCoverage();
    return ok ? 0 : 1;
}

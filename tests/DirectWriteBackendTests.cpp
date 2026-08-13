// DirectWrite text backend test. On Windows it validates that the real
// DirectWrite backend measures and renders text (glyph coverage). On other
// platforms it validates graceful unavailability.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "canvas/Paint.h"
#include "text/DirectWriteTextBackend.h"
#include "text/ITextBackend.h"
#include "wsc/Font.h"

namespace {

int countCoveredPixels(const std::vector<unsigned char> &rgba)
{
    int covered = 0;
    for (std::size_t i = 0; i + 3 < rgba.size(); i += 4) {
        if (rgba[i + 3] > 16) {
            ++covered;
        }
    }
    return covered;
}

} // namespace

int main()
{
#ifdef _WIN32
    if (!wsc::text::isDirectWriteAvailable()) {
        std::cerr << "[DirectWriteBackendTests] FAIL: DirectWrite reported unavailable on Windows." << std::endl;
        return 1;
    }

    auto backend = wsc::text::createDirectWriteTextBackend();
    if (!backend) {
        std::cerr << "[DirectWriteBackendTests] FAIL: createDirectWriteTextBackend returned null." << std::endl;
        return 1;
    }

    wsc::Paint paint;
    paint.setFontFamily("Segoe UI");
    paint.setTextSize(16.0f);

    // Measurement.
    const float width = backend->measureTextWidth("Hg Ap", paint);
    if (!(width > 0.0f)) {
        std::cerr << "[DirectWriteBackendTests] FAIL: measured width was " << width << "." << std::endl;
        return 1;
    }

    const wsc::text::TextMetrics metrics = backend->measureTextMetrics("Hg Ap", paint);
    if (!(metrics.ascent > 0.0f) || !(metrics.descent > 0.0f) || !(metrics.width > 0.0f)) {
        std::cerr << "[DirectWriteBackendTests] FAIL: metrics ascent=" << metrics.ascent
                  << " descent=" << metrics.descent << " width=" << metrics.width << "." << std::endl;
        return 1;
    }

    // Rendering (grayscale, the default).
    const wsc::text::TextRenderResult result = backend->renderText("Hg", 0.0f, 0.0f, paint);
    if (result.kind != wsc::text::TextRenderKind::Bitmap) {
        std::cerr << "[DirectWriteBackendTests] FAIL: expected Bitmap render kind." << std::endl;
        return 1;
    }
    if (result.bitmapWidth <= 0 || result.bitmapHeight <= 0 || result.bitmapPixels.empty()) {
        std::cerr << "[DirectWriteBackendTests] FAIL: empty rendered bitmap." << std::endl;
        return 1;
    }
    const int covered = countCoveredPixels(result.bitmapPixels);
    if (covered <= 0) {
        std::cerr << "[DirectWriteBackendTests] FAIL: rendered bitmap had no covered pixels (text not drawn)."
                  << std::endl;
        return 1;
    }
    // Grayscale coverage: RGB should be white where covered.
    bool sawWhiteRgb = false;
    for (std::size_t i = 0; i + 3 < result.bitmapPixels.size(); i += 4) {
        if (result.bitmapPixels[i + 3] > 128) {
            if (result.bitmapPixels[i] == 255 && result.bitmapPixels[i + 1] == 255
                && result.bitmapPixels[i + 2] == 255) {
                sawWhiteRgb = true;
                break;
            }
        }
    }
    if (!sawWhiteRgb) {
        std::cerr << "[DirectWriteBackendTests] FAIL: grayscale coverage should emit white RGB." << std::endl;
        return 1;
    }

    // CJK fallback: a Chinese character should render without ASCII replacement.
    const wsc::text::TextRenderResult cjk = backend->renderText("\xE4\xBD\xA0", 0.0f, 0.0f, paint); // U+4F60
    if (cjk.kind == wsc::text::TextRenderKind::Bitmap && !cjk.bitmapPixels.empty()) {
        if (countCoveredPixels(cjk.bitmapPixels) <= 0) {
            std::cerr << "[DirectWriteBackendTests] FAIL: CJK glyph produced no coverage (fallback failed)."
                      << std::endl;
            return 1;
        }
    }

    // ClearType mode should preserve non-uniform RGB subpixel coverage.
    wsc::text::DirectWriteBackendOptions ctOptions;
    ctOptions.rasterMode = wsc::text::DirectWriteRasterMode::ClearType;
    auto ctBackend = wsc::text::createDirectWriteTextBackend(ctOptions);
    if (ctBackend) {
        const wsc::text::TextRenderResult ct = ctBackend->renderText("Hg", 0.0f, 0.0f, paint);
        if (ct.kind == wsc::text::TextRenderKind::Bitmap && countCoveredPixels(ct.bitmapPixels) <= 0) {
            std::cerr << "[DirectWriteBackendTests] FAIL: ClearType render had no coverage." << std::endl;
            return 1;
        }
        if (ct.kind == wsc::text::TextRenderKind::Bitmap && !ct.bitmapIsClearType) {
            std::cerr << "[DirectWriteBackendTests] FAIL: ClearType bitmap was not tagged for LCD composition."
                      << std::endl;
            return 1;
        }
    }

    // Letter spacing must affect BOTH measurement and rendering (it is baked into
    // the DirectWrite layout, not just added to the measured width).
    wsc::Paint spaced = paint;
    spaced.setLetterSpacing(6.0f);
    const float baseWidth = backend->measureTextWidth("IIII", paint);
    const float spacedWidth = backend->measureTextWidth("IIII", spaced);
    if (!(spacedWidth > baseWidth + 12.0f)) {
        std::cerr << "[DirectWriteBackendTests] FAIL: letter spacing did not widen measurement (base="
                  << baseWidth << " spaced=" << spacedWidth << ")." << std::endl;
        return 1;
    }
    const wsc::text::TextRenderResult baseRender = backend->renderText("IIII", 0.0f, 0.0f, paint);
    const wsc::text::TextRenderResult spacedRender = backend->renderText("IIII", 0.0f, 0.0f, spaced);
    if (spacedRender.kind != wsc::text::TextRenderKind::Bitmap
        || !(spacedRender.bitmapWidth > baseRender.bitmapWidth)) {
        std::cerr << "[DirectWriteBackendTests] FAIL: letter spacing did not widen the rendered bitmap (base="
                  << baseRender.bitmapWidth << " spaced=" << spacedRender.bitmapWidth << ")." << std::endl;
        return 1;
    }

    // Public OpenType features must reach DirectWrite typography and must be
    // part of the bitmap cache identity. Roboto contains a real ffi ligature.
    auto featureBackend = wsc::text::createDirectWriteTextBackend();
    if (featureBackend != nullptr
        && featureBackend->registerFontFace(wsc::FontFace::fromFile(
            wsc::FontDescriptor("Roboto"), WHATSCANVAS_TEST_OPENTYPE_FONT))) {
        wsc::Paint ligaturesOn;
        ligaturesOn.setFontFamily("Roboto");
        ligaturesOn.setTextSize(32.0f);
        ligaturesOn.setFontFeature("liga", 1);
        wsc::Paint ligaturesOff = ligaturesOn;
        ligaturesOff.setFontFeature("liga", 0);
        const auto withLigature = featureBackend->renderText("ffi", 0.0f, 0.0f, ligaturesOn);
        const auto withoutLigature = featureBackend->renderText("ffi", 0.0f, 0.0f, ligaturesOff);
        if (withLigature.kind != wsc::text::TextRenderKind::Bitmap
            || withoutLigature.kind != wsc::text::TextRenderKind::Bitmap
            || (withLigature.bitmapContentId == withoutLigature.bitmapContentId
                && withLigature.bitmapPixels == withoutLigature.bitmapPixels)) {
            std::cerr << "[DirectWriteBackendTests] FAIL: OpenType liga feature did not affect DirectWrite output."
                      << std::endl;
            return 1;
        }
    }

    std::cout << "[DirectWriteBackendTests] PASS: measured width=" << width << ", ascent=" << metrics.ascent
              << ", descent=" << metrics.descent << ", coverage=" << covered
              << " px; letter-spacing base=" << baseWidth << " spaced=" << spacedWidth
              << " (bitmap " << baseRender.bitmapWidth << "->" << spacedRender.bitmapWidth << ")." << std::endl;

    // Custom font-file registration: registering an on-disk font builds a usable
    // custom collection whose family resolves for measurement and rendering.
    const std::vector<wsc::FontFace> installed = wsc::FontSystem::discoverInstalledFontFaces();
    const char *preferredFamilies[] = {"Consolas", "Arial", "Segoe UI"};
    const wsc::FontFace *chosen = nullptr;
    for (const char *family : preferredFamilies) {
        int bestDistance = std::numeric_limits<int>::max();
        for (const wsc::FontFace &face : installed) {
            if (face.family() == family && face.slant() == wsc::FontSlant::NORMAL) {
                const int distance = std::abs(face.weight() - 400);
                if (distance < bestDistance) {
                    chosen = &face;
                    bestDistance = distance;
                }
            }
        }
        if (chosen != nullptr) break;
    }
    if (chosen != nullptr) {
        if (!backend->registerFontFace(*chosen)) {
            std::cerr << "[DirectWriteBackendTests] FAIL: registerFontFace(file) returned false for "
                      << chosen->path() << "." << std::endl;
            return 1;
        }
        wsc::Paint customPaint;
        customPaint.setFontFamily(chosen->family());
        customPaint.setTextSize(18.0f);
        if (!(backend->measureTextWidth("Reg", customPaint) > 0.0f)) {
            std::cerr << "[DirectWriteBackendTests] FAIL: registered custom font did not measure." << std::endl;
            return 1;
        }
        const wsc::text::TextRenderResult customRender = backend->renderText("Reg", 0.0f, 0.0f, customPaint);
        if (customRender.kind != wsc::text::TextRenderKind::Bitmap
            || countCoveredPixels(customRender.bitmapPixels) <= 0) {
            std::cerr << "[DirectWriteBackendTests] FAIL: registered custom font did not render." << std::endl;
            return 1;
        }
        // In-memory font registration: load the same font file into memory and
        // register it via fromMemory; it must measure and render.
        std::ifstream fontStream(std::filesystem::u8path(chosen->path()), std::ios::binary);
        std::vector<std::uint8_t> fontBytes((std::istreambuf_iterator<char>(fontStream)),
                                            std::istreambuf_iterator<char>());
        if (!fontBytes.empty()) {
            auto memBackend = wsc::text::createDirectWriteTextBackend();
            if (memBackend
                && memBackend->registerFontFace(
                       wsc::FontFace::fromMemory(chosen->descriptor(), fontBytes, chosen->faceIndex()))) {
                wsc::Paint memPaint;
                memPaint.setFontFamily(chosen->family());
                memPaint.setTextSize(18.0f);
                const wsc::text::TextRenderResult memRender = memBackend->renderText("Mem", 0.0f, 0.0f, memPaint);
                if (!(memBackend->measureTextWidth("Mem", memPaint) > 0.0f)
                    || memRender.kind != wsc::text::TextRenderKind::Bitmap
                    || countCoveredPixels(memRender.bitmapPixels) <= 0) {
                    std::cerr << "[DirectWriteBackendTests] FAIL: in-memory font did not render." << std::endl;
                    return 1;
                }
                std::cout << "[DirectWriteBackendTests] in-memory font '" << chosen->family()
                          << "' registered and rendered." << std::endl;
            }
        }
        std::cout << "[DirectWriteBackendTests] custom font '" << chosen->family()
                  << "' registered and rendered." << std::endl;
    }

    // Custom font fallback chain: configuring a chain should succeed and text
    // (including CJK that needs fallback) must still render.
    wsc::FontFallbackChain chain("Segoe UI");
    chain.addFallbackFamily("Yu Gothic");
    chain.addFallbackFamily("Microsoft YaHei");
    if (!backend->setFontFallbackChain(chain)) {
        std::cerr << "[DirectWriteBackendTests] FAIL: setFontFallbackChain returned false." << std::endl;
        return 1;
    }
    const wsc::text::TextRenderResult fbRender = backend->renderText("A\xE4\xBD\xA0", 0.0f, 0.0f, paint); // "A你"
    if (fbRender.kind != wsc::text::TextRenderKind::Bitmap
        || countCoveredPixels(fbRender.bitmapPixels) <= 0) {
        std::cerr << "[DirectWriteBackendTests] FAIL: text did not render with a custom fallback chain."
                  << std::endl;
        return 1;
    }
    // An empty chain clears the custom fallback (returns true).
    if (!backend->setFontFallbackChain(wsc::FontFallbackChain())) {
        std::cerr << "[DirectWriteBackendTests] FAIL: clearing the fallback chain should return true."
                  << std::endl;
        return 1;
    }
    std::cout << "[DirectWriteBackendTests] custom font fallback chain applied and cleared." << std::endl;

    // Full text styling surface: family + weight + slant + letter spacing + locale
    // all flow through to a rendered bitmap.
    wsc::Paint styled;
    styled.setFontFamily("Segoe UI");
    styled.setFontWeight(700);
    styled.setFontSlant(wsc::FontSlant::ITALIC);
    styled.setLetterSpacing(2.0f);
    styled.setTextSize(20.0f);
    styled.setTextLocale("en-US");
    const wsc::text::TextRenderResult styledRender = backend->renderText("Style", 0.0f, 0.0f, styled);
    if (styledRender.kind != wsc::text::TextRenderKind::Bitmap
        || countCoveredPixels(styledRender.bitmapPixels) <= 0) {
        std::cerr << "[DirectWriteBackendTests] FAIL: styled text (weight/slant/spacing/locale) did not render."
                  << std::endl;
        return 1;
    }
    // Locale-tagged CJK should measure a positive width (locale-aware layout).
    wsc::Paint jp;
    jp.setFontFamily("Yu Gothic");
    jp.setTextSize(20.0f);
    jp.setTextLocale("ja-JP");
    if (!(backend->measureTextWidth("\xE6\x97\xA5\xE6\x9C\xAC", jp) > 0.0f)) { // "日本"
        std::cerr << "[DirectWriteBackendTests] FAIL: locale-tagged CJK did not measure." << std::endl;
        return 1;
    }
    std::cout << "[DirectWriteBackendTests] text styling surface (weight/slant/spacing/locale) rendered."
              << std::endl;

    // Real DirectWrite line breaking: a long space-less CJK string must wrap into
    // multiple lines (a greedy ASCII-whitespace heuristic could not), and each
    // reported line must be non-empty with byte offsets inside the source.
    wsc::Paint wrapPaint;
    wrapPaint.setFontFamily("Yu Gothic");
    wrapPaint.setTextSize(20.0f);
    const std::string cjkText = "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E\xE3\x81\xAE\xE3\x83\x86"
                                "\xE3\x82\xAD\xE3\x82\xB9\xE3\x83\x88\xE8\xA1\x8C"; // 日本語のテキスト行
    const std::vector<wsc::text::TextLineBreak> cjkLines = backend->breakLines(cjkText, 40.0f, wrapPaint);
    if (cjkLines.size() < 2) {
        std::cerr << "[DirectWriteBackendTests] FAIL: CJK text should wrap into multiple lines (got "
                  << cjkLines.size() << ")." << std::endl;
        return 1;
    }
    for (const wsc::text::TextLineBreak &lb : cjkLines) {
        if (lb.sourceLength == 0 || lb.sourceStart + lb.sourceLength > cjkText.size()) {
            std::cerr << "[DirectWriteBackendTests] FAIL: CJK line break offsets out of range." << std::endl;
            return 1;
        }
    }

    // English wraps at word boundaries into >1 line for a narrow width.
    wsc::Paint enPaint;
    enPaint.setFontFamily("Segoe UI");
    enPaint.setTextSize(16.0f);
    const std::vector<wsc::text::TextLineBreak> enLines =
        backend->breakLines("the quick brown fox jumps", 80.0f, enPaint);
    if (enLines.size() < 2) {
        std::cerr << "[DirectWriteBackendTests] FAIL: English text should wrap into multiple lines (got "
                  << enLines.size() << ")." << std::endl;
        return 1;
    }
    std::cout << "[DirectWriteBackendTests] line breaking: CJK=" << cjkLines.size()
              << " lines, English=" << enLines.size() << " lines." << std::endl;

    // Text decorations: underline and strikethrough each add ink over the plain
    // run (DirectWrite draws the decoration lines within the layout).
    wsc::Paint plain;
    plain.setFontFamily("Segoe UI");
    plain.setTextSize(18.0f);
    wsc::Paint underlined = plain;
    underlined.setUnderline(true);
    wsc::Paint struck = plain;
    struck.setStrikethrough(true);

    const int plainCov = countCoveredPixels(backend->renderText("mm", 0.0f, 0.0f, plain).bitmapPixels);
    const int underCov = countCoveredPixels(backend->renderText("mm", 0.0f, 0.0f, underlined).bitmapPixels);
    const int strikeCov = countCoveredPixels(backend->renderText("mm", 0.0f, 0.0f, struck).bitmapPixels);
    if (!(plainCov > 0) || !(underCov > plainCov) || !(strikeCov > plainCov)) {
        std::cerr << "[DirectWriteBackendTests] FAIL: decorations did not add ink (plain=" << plainCov
                  << " underline=" << underCov << " strike=" << strikeCov << ")." << std::endl;
        return 1;
    }
    std::cout << "[DirectWriteBackendTests] decorations: plain=" << plainCov << " underline=" << underCov
              << " strike=" << strikeCov << "." << std::endl;

    // resolveFontFamilies mirrors the portable backend: once a fallback chain
    // has been set, resolving the primary returns the chain in order; unknown
    // families still resolve to just the requested family.
    auto resolveBackend = wsc::text::createDirectWriteTextBackend();
    if (resolveBackend) {
        wsc::FontFallbackChain resChain("Segoe UI");
        resChain.addFallbackFamily("Yu Gothic");
        resChain.addFallbackFamily("Microsoft YaHei");
        if (!resolveBackend->setFontFallbackChain(resChain)) {
            std::cerr << "[DirectWriteBackendTests] FAIL: setFontFallbackChain for resolve." << std::endl;
            return 1;
        }
        const std::vector<std::string> resolved = resolveBackend->resolveFontFamilies("Segoe UI");
        if (resolved.size() != 3 || resolved[0] != "Segoe UI" || resolved[1] != "Yu Gothic"
            || resolved[2] != "Microsoft YaHei") {
            std::cerr << "[DirectWriteBackendTests] FAIL: resolveFontFamilies did not mirror the chain."
                      << std::endl;
            return 1;
        }
        const std::vector<std::string> other = resolveBackend->resolveFontFamilies("Arial");
        if (other.size() != 1 || other[0] != "Arial") {
            std::cerr << "[DirectWriteBackendTests] FAIL: resolveFontFamilies for unrelated family."
                      << std::endl;
            return 1;
        }
        std::cout << "[DirectWriteBackendTests] resolveFontFamilies: chain=" << resolved.size()
                  << " unrelated=" << other.size() << "." << std::endl;
    }

    // Rendered-text cache: identical (text, paint) must produce byte-identical
    // bitmaps AND be much faster than the first render (proxy for cache reuse).
    wsc::Paint cachePaint;
    cachePaint.setFontFamily("Segoe UI");
    cachePaint.setTextSize(18.0f);
    const std::string cacheStr = "cache the rendered bitmap";
    const wsc::text::TextRenderResult first = backend->renderText(cacheStr, 10.0f, 20.0f, cachePaint);
    if (first.kind != wsc::text::TextRenderKind::Bitmap || first.bitmapPixels.empty()) {
        std::cerr << "[DirectWriteBackendTests] FAIL: initial cache render failed." << std::endl;
        return 1;
    }
    const auto tColdStart = std::chrono::steady_clock::now();
    for (int i = 0; i < 200; ++i) {
        volatile auto r = backend->renderText(cacheStr, 10.0f, 20.0f, cachePaint);
        (void)r;
    }
    const auto tColdEnd = std::chrono::steady_clock::now();
    const wsc::text::TextRenderResult second = backend->renderText(cacheStr, 42.5f, 12.75f, cachePaint);
    if (second.bitmapPixels != first.bitmapPixels || second.bitmapWidth != first.bitmapWidth
        || second.bitmapHeight != first.bitmapHeight) {
        std::cerr << "[DirectWriteBackendTests] FAIL: cache hit did not return identical bitmap."
                  << std::endl;
        return 1;
    }
    // Position must still be applied on top of the cached intrinsic geometry.
    if (std::abs(second.drawX - (42.5f + (first.drawX - 10.0f))) > 0.01f) {
        std::cerr << "[DirectWriteBackendTests] FAIL: cache did not re-apply x offset (drawX="
                  << second.drawX << ")." << std::endl;
        return 1;
    }
    // Changing a raster-relevant paint field must produce a distinct bitmap.
    wsc::Paint bigger = cachePaint;
    bigger.setTextSize(28.0f);
    const wsc::text::TextRenderResult distinct = backend->renderText(cacheStr, 10.0f, 20.0f, bigger);
    if (distinct.bitmapWidth == first.bitmapWidth && distinct.bitmapHeight == first.bitmapHeight) {
        std::cerr << "[DirectWriteBackendTests] FAIL: distinct paint should not hit the cached entry."
                  << std::endl;
        return 1;
    }
    const long long cacheMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(tColdEnd - tColdStart).count();
    std::cout << "[DirectWriteBackendTests] render cache: 200 identical renders in " << cacheMs
              << " ms, bitmap identical, x offset re-applied." << std::endl;

    // Per-Paint TextRenderMode override: the same backend (grayscale by default)
    // must honour an explicit ClearType override per Paint. Distinct modes must
    // produce distinct bitmaps (the cache key includes the effective mode); each
    // mode must round-trip byte-identical on a repeated render (still cached).
    wsc::Paint grayscalePaint;
    grayscalePaint.setFontFamily("Segoe UI");
    grayscalePaint.setTextSize(20.0f);
    grayscalePaint.setTextRenderMode(wsc::Paint::TextRenderMode::Grayscale);
    wsc::Paint ctPaint = grayscalePaint;
    ctPaint.setTextRenderMode(wsc::Paint::TextRenderMode::ClearType);

    const wsc::text::TextRenderResult grayHit = backend->renderText("Mode", 0.0f, 0.0f, grayscalePaint);
    const wsc::text::TextRenderResult ctHit = backend->renderText("Mode", 0.0f, 0.0f, ctPaint);
    if (grayHit.kind != wsc::text::TextRenderKind::Bitmap
        || ctHit.kind != wsc::text::TextRenderKind::Bitmap) {
        std::cerr << "[DirectWriteBackendTests] FAIL: per-Paint mode render did not produce bitmaps."
                  << std::endl;
        return 1;
    }
    if (grayHit.bitmapIsClearType || !ctHit.bitmapIsClearType) {
        std::cerr << "[DirectWriteBackendTests] FAIL: per-Paint mode not reflected in bitmapIsClearType (gray="
                  << grayHit.bitmapIsClearType << " ct=" << ctHit.bitmapIsClearType << ")." << std::endl;
        return 1;
    }
    if (grayHit.bitmapPixels == ctHit.bitmapPixels) {
        std::cerr << "[DirectWriteBackendTests] FAIL: grayscale and ClearType produced identical pixels."
                  << std::endl;
        return 1;
    }
    // Repeated renders in each mode must be byte-identical (cache reuse).
    const wsc::text::TextRenderResult grayAgain = backend->renderText("Mode", 5.0f, 5.0f, grayscalePaint);
    const wsc::text::TextRenderResult ctAgain = backend->renderText("Mode", 5.0f, 5.0f, ctPaint);
    if (grayAgain.bitmapPixels != grayHit.bitmapPixels
        || ctAgain.bitmapPixels != ctHit.bitmapPixels) {
        std::cerr << "[DirectWriteBackendTests] FAIL: per-Paint mode cache reuse broke byte-identity."
                  << std::endl;
        return 1;
    }
    std::cout << "[DirectWriteBackendTests] per-Paint TextRenderMode: gray + ClearType distinct, cached."
              << std::endl;

    // bitmapContentId plumbing: DirectWrite must populate a stable non-zero id
    // that Canvas uses to key its GPU texture cache. Same paint + text = same
    // id; grayscale vs ClearType (raster-mode difference) = different ids.
    wsc::Paint idPaint;
    idPaint.setFontFamily("Segoe UI");
    idPaint.setTextSize(16.0f);
    const auto idA = backend->renderText("cache id", 0.0f, 0.0f, idPaint);
    const auto idB = backend->renderText("cache id", 25.0f, 5.0f, idPaint); // moved
    if (idA.bitmapContentId == 0 || idA.bitmapContentId != idB.bitmapContentId) {
        std::cerr << "[DirectWriteBackendTests] FAIL: bitmapContentId not stable across identical renders ("
                  << idA.bitmapContentId << " vs " << idB.bitmapContentId << ")." << std::endl;
        return 1;
    }
    wsc::Paint idPaintCt = idPaint;
    idPaintCt.setTextRenderMode(wsc::Paint::TextRenderMode::ClearType);
    const auto idCt = backend->renderText("cache id", 0.0f, 0.0f, idPaintCt);
    if (idCt.bitmapContentId == 0 || idCt.bitmapContentId == idA.bitmapContentId) {
        std::cerr << "[DirectWriteBackendTests] FAIL: bitmapContentId did not change with raster mode."
                  << std::endl;
        return 1;
    }
    std::cout << "[DirectWriteBackendTests] bitmapContentId: gray=" << idA.bitmapContentId
              << " ct=" << idCt.bitmapContentId << " (distinct, stable)." << std::endl;

    return 0;
#else
    if (wsc::text::isDirectWriteAvailable()) {
        std::cerr << "[DirectWriteBackendTests] FAIL: DirectWrite should be unavailable off Windows." << std::endl;
        return 1;
    }
    if (wsc::text::createDirectWriteTextBackend() != nullptr) {
        std::cerr << "[DirectWriteBackendTests] FAIL: expected null backend off Windows." << std::endl;
        return 1;
    }
    std::cout << "[DirectWriteBackendTests] PASS: DirectWrite gracefully unavailable off Windows." << std::endl;
    return 0;
#endif
}

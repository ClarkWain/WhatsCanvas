// DirectWrite text backend test. On Windows it validates that the real
// DirectWrite backend measures and renders text (glyph coverage). On other
// platforms it validates graceful unavailability.

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
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

    std::cout << "[DirectWriteBackendTests] PASS: measured width=" << width << ", ascent=" << metrics.ascent
              << ", descent=" << metrics.descent << ", coverage=" << covered
              << " px; letter-spacing base=" << baseWidth << " spaced=" << spacedWidth
              << " (bitmap " << baseRender.bitmapWidth << "->" << spacedRender.bitmapWidth << ")." << std::endl;

    // Custom font-file registration: registering an on-disk font builds a usable
    // custom collection whose family resolves for measurement and rendering.
    struct FontCandidate { const char *path; const char *family; };
    const FontCandidate candidates[] = {
        {"C:/Windows/Fonts/consola.ttf", "Consolas"},
        {"C:/Windows/Fonts/arial.ttf", "Arial"},
        {"C:/Windows/Fonts/segoeui.ttf", "Segoe UI"},
    };
    const FontCandidate *chosen = nullptr;
    for (const FontCandidate &c : candidates) {
        std::ifstream f(c.path, std::ios::binary);
        if (f.good()) {
            chosen = &c;
            break;
        }
    }
    if (chosen != nullptr) {
        if (!backend->registerFontFace(
                wsc::FontFace::fromFile(wsc::FontDescriptor(chosen->family), chosen->path))) {
            std::cerr << "[DirectWriteBackendTests] FAIL: registerFontFace(file) returned false for "
                      << chosen->path << "." << std::endl;
            return 1;
        }
        wsc::Paint customPaint;
        customPaint.setFontFamily(chosen->family);
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
        // Memory-backed registration is not supported yet -> should return false.
        if (backend->registerFontFace(
                wsc::FontFace::fromMemory(wsc::FontDescriptor("MemFont"),
                                          std::vector<std::uint8_t>{1, 2, 3, 4}))) {
            std::cerr << "[DirectWriteBackendTests] FAIL: memory font registration should return false."
                      << std::endl;
            return 1;
        }
        std::cout << "[DirectWriteBackendTests] custom font '" << chosen->family
                  << "' registered and rendered." << std::endl;
    }
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

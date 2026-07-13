// DirectWrite text backend test. On Windows it validates that the real
// DirectWrite backend measures and renders text (glyph coverage). On other
// platforms it validates graceful unavailability.

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

#include "canvas/Paint.h"
#include "text/DirectWriteTextBackend.h"
#include "text/ITextBackend.h"

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

    std::cout << "[DirectWriteBackendTests] PASS: measured width=" << width << ", ascent=" << metrics.ascent
              << ", descent=" << metrics.descent << ", coverage=" << covered << " px." << std::endl;
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

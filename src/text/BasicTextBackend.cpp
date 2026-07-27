#include "text/BasicTextBackend.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "../../include/wsc/Font.h"
#include "canvas/Paint.h"
#include "text/DirectWriteTextBackend.h"
#include "text/FontRasterizer.h"
#include "text/GlyphAtlas.h"
#include "text/ITextBackend.h"
#include "text/NativeText.h"
#include "text/TextShaper.h"
#include "text/TextUtils.h"

namespace {

using wsc::text::TextRenderKind;
using wsc::text::TextRenderResult;

constexpr size_t kMaxNativeTextCacheEntries = 128;
constexpr size_t kMaxRasterShapeCacheEntries = 512;
constexpr size_t kMaxRasterLayoutCacheEntries = 512;
// Windows application UI commonly combines several text sizes, weights and
// structural branches in one deferred Software frame.  Starting at 1024 can
// force an atlas resize while that frame still owns commands referencing the
// first texture.  A 2048 atlas keeps a normal desktop scene stable and avoids
// replacing the shared resource during command recording.
constexpr int kDefaultGlyphAtlasSize = 2048;

const char *backendName(wsc::text::TextBackendKind kind)
{
    switch (kind) {
    case wsc::text::TextBackendKind::Auto: return "auto";
    case wsc::text::TextBackendKind::Portable: return "portable";
    case wsc::text::TextBackendKind::WindowsNative: return "windows-native";
    case wsc::text::TextBackendKind::DirectWrite: return "directwrite";
    case wsc::text::TextBackendKind::CoreText: return "coretext";
    }
    return "unknown";
}

class BasicTextBackend final : public wsc::text::ITextBackend
{
public:
    explicit BasicTextBackend(wsc::text::BasicTextBackendOptions options)
        : options_(options),
          shaper_(wsc::text::createTextShapingEngine(options_.shapingBackend))
    {
        // Try to construct a real DirectWrite backend when requested.
        if (options_.backendKind == wsc::text::TextBackendKind::DirectWrite) {
            wsc::text::DirectWriteBackendOptions dwOptions;
            dwOptions.enableSystemFontFallback = options_.enableSystemFontFallback;
            dwOptions.rasterMode = options_.preferClearType
                                       ? wsc::text::DirectWriteRasterMode::ClearType
                                       : wsc::text::DirectWriteRasterMode::Grayscale;
            directWriteBackend_ = wsc::text::createDirectWriteTextBackend(dwOptions);
            if (directWriteBackend_ != nullptr) {
                options_.enableNativeText = false; // DirectWrite handles native text itself
            } else {
                diagnostics_.push_back({wsc::text::TextBackendDiagnostic::Severity::Warning,
                                        "DirectWrite text adapter is not available on this platform; using portable glyph-atlas backend."});
                options_.enableNativeText = false;
            }
        } else if (options_.backendKind == wsc::text::TextBackendKind::CoreText) {
            diagnostics_.push_back({wsc::text::TextBackendDiagnostic::Severity::Warning,
                                    "coretext text adapter is not available yet; using portable glyph-atlas backend."});
            options_.enableNativeText = false;
        }
        if (options_.shapingBackend == wsc::text::TextShapingBackend::OpenType
            && (shaper_ == nullptr || !shaper_->supportsOpenTypeFeatures())) {
            diagnostics_.push_back({wsc::text::TextBackendDiagnostic::Severity::Warning,
                                    "OpenType shaping backend is unavailable; using simple shaping."});
        }
        if (shaper_ == nullptr) {
            shaper_ = wsc::text::createSimpleTextShapingEngine();
        }
        if (options_.enableSystemFontFallback) {
            registerSystemFontFallbacks();
        }
    }

    /// True when this backend actually constructed a native DirectWrite adapter
    /// (i.e. DirectWrite was requested AND createDirectWriteTextBackend()
    /// succeeded). False when it fell back to the portable glyph-atlas backend,
    /// including a runtime COM/factory failure on Windows.
    bool hasNativeDirectWriteBackend() const
    {
        return directWriteBackend_ != nullptr;
    }

    bool registerFontFace(const wsc::FontFace &face) override
    {
        if (directWriteBackend_ != nullptr) {
            return directWriteBackend_->registerFontFace(face);
        }
        clearRasterCaches();
        const bool registered = fontManager_.registerFace(face);
        if (!registered) {
            diagnostics_.push_back({wsc::text::TextBackendDiagnostic::Severity::Warning,
                                    "Rejected invalid font face registration."});
        }
        return registered;
    }

    bool setFontFallbackChain(const wsc::FontFallbackChain &chain) override
    {
        if (directWriteBackend_ != nullptr) {
            return directWriteBackend_->setFontFallbackChain(chain);
        }
        clearRasterCaches();
        if (chain.primaryFamily().empty() || !fontManager_.hasFamily(chain.primaryFamily())) {
            diagnostics_.push_back({wsc::text::TextBackendDiagnostic::Severity::Warning,
                                    "Rejected fallback chain for an unknown primary family."});
            return false;
        }

        bool ok = true;
        for (const std::string &family : chain.fallbackFamilies()) {
            ok = fontManager_.addFallbackFamily(chain.primaryFamily(), family) && ok;
        }
        if (!ok) {
            diagnostics_.push_back({wsc::text::TextBackendDiagnostic::Severity::Warning,
                                    "Skipped one or more unknown fallback families."});
        }
        return ok;
    }

    std::vector<std::string> resolveFontFamilies(const std::string &preferredFamily) const override
    {
        if (directWriteBackend_ != nullptr) {
            return directWriteBackend_->resolveFontFamilies(preferredFamily);
        }
        if (fontManager_.hasFamily(preferredFamily)) {
            return fontManager_.resolveFamilies(preferredFamily);
        }
        if (preferredFamily.empty() && fontManager_.hasFamily(wsc::FontSystem::kDefaultPrimaryFamily)) {
            return fontManager_.resolveFamilies(wsc::FontSystem::kDefaultPrimaryFamily);
        }
        return preferredFamily.empty() ? std::vector<std::string>() : std::vector<std::string>{preferredFamily};
    }

    std::vector<wsc::text::TextLineBreak> breakLines(const std::string &text, float maxWidth,
                                                     const Paint &paint) const override
    {
        if (directWriteBackend_ != nullptr) {
            return directWriteBackend_->breakLines(text, maxWidth, paint);
        }
        std::vector<wsc::text::TextLineBreak> result;
        const std::string normalizedText = wsc::text::normalizeUtf8ForText(text);
        if (normalizedText.empty() || maxWidth <= 0.0f || paint.getTextSize() <= 0.0f) {
            return result;
        }

        auto appendParagraph = [&](std::size_t paragraphStart, std::size_t paragraphEnd) {
            if (paragraphEnd <= paragraphStart) {
                result.push_back({paragraphStart, 0, 0.0f});
                return;
            }

            std::string currentLine;
            std::size_t currentStart = paragraphStart;
            std::size_t currentEnd = paragraphStart;
            auto pushCurrentLine = [&]() {
                if (!currentLine.empty()) {
                    result.push_back({currentStart, currentEnd - currentStart,
                                      measureTextWidth(currentLine, paint)});
                    currentLine.clear();
                }
            };
            auto appendScalarToken = [&](const std::string &tokenText,
                                         std::size_t tokenStart,
                                         std::size_t tokenEnd) {
                const std::vector<wsc::text::Utf8Codepoint> codepoints = wsc::text::decodeUtf8(tokenText);
                for (const wsc::text::Utf8Codepoint &codepoint : codepoints) {
                    const std::string scalarText = tokenText.substr(codepoint.offset, codepoint.length);
                    const std::size_t scalarStart = tokenStart + codepoint.offset;
                    const std::size_t scalarEnd = std::min(tokenStart + codepoint.offset + codepoint.length,
                                                           tokenEnd);
                    const std::string candidate = currentLine.empty() ? scalarText : currentLine + scalarText;
                    if (!currentLine.empty() && measureTextWidth(candidate, paint) > maxWidth) {
                        pushCurrentLine();
                    }
                    if (currentLine.empty()) {
                        currentStart = scalarStart;
                    }
                    currentLine += scalarText;
                    currentEnd = scalarEnd;
                }
            };
            const std::vector<wsc::text::TextBreakToken> tokens =
                wsc::text::buildTextBreakTokens(normalizedText, paragraphStart, paragraphEnd);
            for (const wsc::text::TextBreakToken &token : tokens) {
                const std::string tokenText = normalizedText.substr(token.sourceStart, token.sourceEnd - token.sourceStart);
                const std::string candidate =
                    currentLine.empty() ? tokenText : currentLine + (token.prefixSpace ? " " : "") + tokenText;
                if (currentLine.empty() && measureTextWidth(tokenText, paint) > maxWidth) {
                    appendScalarToken(tokenText, token.sourceStart, token.sourceEnd);
                } else if (currentLine.empty() || measureTextWidth(candidate, paint) <= maxWidth) {
                    if (currentLine.empty()) {
                        currentStart = token.sourceStart;
                    }
                    currentLine = candidate;
                    currentEnd = token.sourceEnd;
                } else {
                    pushCurrentLine();
                    if (measureTextWidth(tokenText, paint) > maxWidth) {
                        appendScalarToken(tokenText, token.sourceStart, token.sourceEnd);
                    } else {
                        currentLine = tokenText;
                        currentStart = token.sourceStart;
                        currentEnd = token.sourceEnd;
                    }
                }
            }

            if (!currentLine.empty()) {
                result.push_back({currentStart, currentEnd - currentStart,
                                  measureTextWidth(currentLine, paint)});
            }
        };

        std::size_t paragraphStart = 0;
        for (std::size_t i = 0; i <= normalizedText.size(); ++i) {
            if (i == normalizedText.size() || normalizedText[i] == '\n' || normalizedText[i] == '\r') {
                appendParagraph(paragraphStart, i);
                if (i < normalizedText.size() && normalizedText[i] == '\r'
                    && i + 1 < normalizedText.size() && normalizedText[i + 1] == '\n') {
                    ++i;
                }
                paragraphStart = i + 1;
            }
        }
        return result;
    }

    bool hasGlyphForCodepoint(std::uint32_t codepoint, const Paint &paint) const override
    {
        if (directWriteBackend_ != nullptr) {
            return directWriteBackend_->hasGlyphForCodepoint(codepoint, paint);
        }
        if (codepoint == '\n' || codepoint == '\t' || wsc::text::isBidiControlCodepoint(codepoint)
            || wsc::text::isZeroWidthBreakCodepoint(codepoint)
            || (codepoint >= 32 && codepoint <= 126)) {
            return true;
        }

        const std::vector<std::string> families = paint.hasFontFamily()
            ? resolveFontFamilies(paint.getFontFamily())
            : resolveFontFamilies(std::string());
        for (const std::string &family : families) {
            for (const wsc::FontFace *face : fontManager_.findFaces(family)) {
                if (face != nullptr
                    && ((face->hasCodepointRanges() && face->supportsCodepoint(codepoint))
                        || rasterizer_.hasGlyph(*face, codepoint))) {
                    return true;
                }
            }
        }

#ifdef _WIN32
        if (options_.enableNativeText && paint.hasFontFamily()) {
            return true;
        }
#endif
        addMissingGlyphDiagnostic(codepoint, paint.hasFontFamily() ? paint.getFontFamily()
                                                                   : wsc::FontSystem::kDefaultPrimaryFamily);
        return false;
    }

    std::vector<wsc::text::TextBackendDiagnostic> diagnostics() const override
    {
        if (directWriteBackend_ != nullptr) {
            std::vector<wsc::text::TextBackendDiagnostic> combined = diagnostics_;
            const auto dwDiags = directWriteBackend_->diagnostics();
            combined.insert(combined.end(), dwDiags.begin(), dwDiags.end());
            return combined;
        }
        return diagnostics_;
    }

    float measureTextWidth(const std::string &text, const Paint &paint) const override
    {
        if (directWriteBackend_ != nullptr) {
            return directWriteBackend_->measureTextWidth(text, paint);
        }
        const std::string normalizedText = wsc::text::normalizeUtf8ForText(text);
        if (normalizedText.empty() || paint.getTextSize() <= 0.0f) {
            return 0.0f;
        }

        if (const auto rasterWidth = measureRasterizedTextWidth(normalizedText, paint)) {
            return *rasterWidth;
        }

#ifdef _WIN32
        if (options_.enableNativeText && paint.hasFontFamily()) {
            const auto nativeMeasure = getNativeMeasure(normalizedText, paint);
            if (nativeMeasure.valid) {
                return nativeMeasure.width;
            }
        }
#endif

        const std::string asciiText = wsc::text::makeAsciiFallbackText(normalizedText);
        constexpr float kTextBaseSize = 8.0f;
        const float textScale = std::max(0.01f, paint.getTextSize() / kTextBaseSize);
        return wsc::text::measureAsciiTextWidth(asciiText, textScale, paint.getLetterSpacing());
    }

    RectF measureTextBounds(const std::string &text, const Paint &paint) const override
    {
        if (directWriteBackend_ != nullptr) {
            return directWriteBackend_->measureTextBounds(text, paint);
        }
        const std::string normalizedText = wsc::text::normalizeUtf8ForText(text);
        if (normalizedText.empty() || paint.getTextSize() <= 0.0f) {
            return RectF();
        }

        if (const auto rasterWidth = measureRasterizedTextWidth(normalizedText, paint)) {
            const float height = paint.getTextSize();
            float left = 0.0f;
            if (paint.getTextAlign() == Paint::TextAlign::CENTER) {
                left = -*rasterWidth * 0.5f;
            } else if (paint.getTextAlign() == Paint::TextAlign::RIGHT) {
                left = -*rasterWidth;
            }
            return RectF(left,
                         wsc::text::textBaselineOffset(paint.getTextBaseline(), height),
                         *rasterWidth,
                         height);
        }

#ifdef _WIN32
        if (options_.enableNativeText && paint.hasFontFamily()) {
            const auto nativeMeasure = getNativeMeasure(normalizedText, paint);
            if (nativeMeasure.valid) {
                float left = 0.0f;
                if (paint.getTextAlign() == Paint::TextAlign::CENTER) {
                    left = -nativeMeasure.width * 0.5f;
                } else if (paint.getTextAlign() == Paint::TextAlign::RIGHT) {
                    left = -nativeMeasure.width;
                }
                return RectF(left,
                             wsc::text::textBaselineOffset(paint.getTextBaseline(), nativeMeasure.height),
                             nativeMeasure.width,
                             nativeMeasure.height);
            }
        }
#endif

        const std::string asciiText = wsc::text::makeAsciiFallbackText(normalizedText);
        constexpr float kTextBaseSize = 8.0f;
        const float textScale = std::max(0.01f, paint.getTextSize() / kTextBaseSize);
        const float width = measureTextWidth(asciiText, paint);
        const float height = wsc::text::measureAsciiTextHeight(asciiText, textScale);

        float left = 0.0f;
        if (paint.getTextAlign() == Paint::TextAlign::CENTER) {
            left = -width * 0.5f;
        } else if (paint.getTextAlign() == Paint::TextAlign::RIGHT) {
            left = -width;
        }

        return RectF(left,
                     wsc::text::textBaselineOffset(paint.getTextBaseline(), height),
                     width,
                     height);
    }

    wsc::text::TextMetrics measureTextMetrics(const std::string &text, const Paint &paint) const override
    {
        if (directWriteBackend_ != nullptr) {
            return directWriteBackend_->measureTextMetrics(text, paint);
        }
        wsc::text::TextMetrics metrics;
        metrics.bounds = measureTextBounds(text, paint);
        metrics.width = metrics.bounds.getWidth();
        metrics.height = metrics.bounds.getHeight();
        metrics.top = metrics.bounds.getY();
        metrics.bottom = metrics.bounds.getY() + metrics.bounds.getHeight();
        metrics.ascent = std::min(0.0f, metrics.top);
        metrics.descent = std::max(0.0f, metrics.bottom);
        metrics.lineHeight = metrics.height;

        const std::string normalizedText = wsc::text::normalizeUtf8ForText(text);
        if (normalizedText.empty() || paint.getTextSize() <= 0.0f
            || (!paint.hasFontFamily() && !fontManager_.hasFamily(wsc::FontSystem::kDefaultPrimaryFamily))) {
            return metrics;
        }

        for (const wsc::text::BidiRun &bidiRun : wsc::text::segmentBidiRuns(normalizedText)) {
            const auto segments = buildRasterTextSegments(normalizedText, paint, bidiRun.sourceStart, bidiRun.sourceEnd);
            if (!segments) {
                continue;
            }
            for (const RasterTextSegment &segment : *segments) {
                if (segment.face == nullptr) {
                    continue;
                }
                const auto vertical = rasterizer_.verticalMetrics(*segment.face, paint.getTextSize());
                if (!vertical) {
                    continue;
                }
                metrics.ascent = -std::max(0.0f, vertical->ascent);
                metrics.descent = std::max(0.0f, -vertical->descent);
                metrics.lineGap = std::max(0.0f, vertical->lineGap);
                metrics.lineHeight = std::max(0.0f, vertical->lineHeight);
                const float left = metrics.bounds.getX();
                metrics.top = metrics.ascent;
                metrics.bottom = metrics.descent;
                metrics.height = metrics.bottom - metrics.top;
                metrics.bounds = RectF(left, metrics.top, metrics.width, metrics.height);
                return metrics;
            }
        }

        return metrics;
    }

    TextRenderResult renderText(const std::string &text, float x, float y, const Paint &paint) const override
    {
        if (directWriteBackend_ != nullptr) {
            return directWriteBackend_->renderText(text, x, y, paint);
        }
        TextRenderResult result;
        const std::string normalizedText = wsc::text::normalizeUtf8ForText(text);
        if (normalizedText.empty() || paint.getTextSize() <= 0.0f) {
            return result;
        }

        if (auto atlasResult = renderRasterizedText(normalizedText, x, y, paint)) {
            return *atlasResult;
        }

#ifdef _WIN32
        if (options_.enableNativeText && paint.hasFontFamily()) {
            const auto nativeMeasure = getNativeMeasure(normalizedText, paint);
            if (nativeMeasure.valid) {
                const auto bitmap = getNativeBitmap(normalizedText, paint, nativeMeasure);
                if (!bitmap.pixels.empty()) {
                    float alignedX = x;
                    if (paint.getTextAlign() == Paint::TextAlign::CENTER) {
                        alignedX -= nativeMeasure.width * 0.5f;
                    } else if (paint.getTextAlign() == Paint::TextAlign::RIGHT) {
                        alignedX -= nativeMeasure.width;
                    }

                    result.kind = TextRenderKind::Bitmap;
                    result.drawX = alignedX - static_cast<float>(bitmap.leftPadding);
                    result.drawY = y + wsc::text::textBaselineOffset(paint.getTextBaseline(), nativeMeasure.height);
                    result.width = nativeMeasure.width + static_cast<float>(bitmap.leftPadding + bitmap.rightPadding);
                    result.height = nativeMeasure.height;
                    result.bitmapWidth = bitmap.width;
                    result.bitmapHeight = bitmap.height;
                    result.bitmapIsClearType = bitmap.isClearType;
                    result.bitmapPixels = bitmap.pixels;
                    return result;
                }
            }
        }
#endif

        const std::string asciiText = wsc::text::makeAsciiFallbackText(normalizedText);
        constexpr float kTextBaseSize = 8.0f;
        const float textScale = std::max(0.01f, paint.getTextSize() / kTextBaseSize);
        const float textHeight = wsc::text::measureAsciiTextHeight(asciiText, textScale);
        const float textWidth = measureTextWidth(asciiText, paint);
        float alignedX = x;
        if (paint.getTextAlign() == Paint::TextAlign::CENTER) {
            alignedX -= textWidth * 0.5f;
        } else if (paint.getTextAlign() == Paint::TextAlign::RIGHT) {
            alignedX -= textWidth;
        }

        result.kind = TextRenderKind::Geometry;
        result.drawX = alignedX;
        result.drawY = y + wsc::text::textBaselineOffset(paint.getTextBaseline(), textHeight);
        result.width = textWidth;
        result.height = textHeight;
        result.vertices = wsc::text::buildTextVertices(asciiText, alignedX, result.drawY,
                                                               textScale, paint.getLetterSpacing());
        for (const wsc::text::Utf8Codepoint &codepoint : wsc::text::decodeUtf8(normalizedText)) {
            if (codepoint.value == '\n' || codepoint.value == '\t' || codepoint.value < 32
                || wsc::text::isBidiControlCodepoint(codepoint.value)
                || wsc::text::isZeroWidthBreakCodepoint(codepoint.value)
                || (codepoint.value >= 32 && codepoint.value <= 126)) {
                continue;
            }
            if (!hasGlyphForCodepoint(codepoint.value, paint)) {
                TextRenderResult::MissingGlyph missing;
                missing.codepoint = codepoint.value;
                missing.sourceStart = codepoint.offset;
                missing.sourceLength = codepoint.length;
                result.missingGlyphs.push_back(missing);
            }
        }
        if (result.vertices.empty()) {
            result.kind = TextRenderKind::None;
        }
        return result;
    }

private:
    void registerSystemFontFallbacks()
    {
        const std::vector<wsc::FontFace> faces = wsc::FontSystem::defaultSystemFontFaces();
        if (faces.empty()) {
            diagnostics_.push_back({wsc::text::TextBackendDiagnostic::Severity::Info,
                                    "No default system font files were discovered."});
            return;
        }

        for (const wsc::FontFace &face : faces) {
            fontManager_.registerFace(face);
        }

        const wsc::FontFallbackChain defaultChain = wsc::FontSystem::defaultFallbackChain();
        for (const std::string &family : defaultChain.fallbackFamilies()) {
            fontManager_.addFallbackFamily(defaultChain.primaryFamily(), family);
        }
    }

    const wsc::FontFace *findRasterFaceForCodepoint(std::uint32_t codepoint, const Paint &paint) const
    {
        const std::string cacheKey = rasterFaceCacheKey(codepoint, paint);
        if (const auto cached = rasterFaceCache_.find(cacheKey); cached != rasterFaceCache_.end()) {
            return cached->second;
        }
        const std::vector<std::string> families = paint.hasFontFamily()
            ? resolveFontFamilies(paint.getFontFamily())
            : resolveFontFamilies(wsc::FontSystem::kDefaultPrimaryFamily);
        if (families.empty()) {
            return nullptr;
        }

        for (const std::string &family : families) {
            if (const wsc::FontFace *face = findBestRasterFaceForCodepoint(family, codepoint, paint)) {
                rasterFaceCache_.emplace(cacheKey, face);
                return face;
            }
        }
        return nullptr;
    }

    const wsc::FontFace *findBestRasterFaceForCodepoint(const std::string &family, std::uint32_t codepoint,
                                                        const Paint &paint) const
    {
        const wsc::FontFace *bestFace = nullptr;
        int bestScore = 0;
        for (const wsc::FontFace *face : fontManager_.findFaces(family)) {
            if (face == nullptr || !rasterizer_.hasGlyph(*face, codepoint)) {
                continue;
            }

            const int slantPenalty = face->slant() == paint.getFontSlant() ? 0 : 1000;
            const int weightPenalty = std::abs(face->weight() - paint.getFontWeight());
            const int score = slantPenalty + weightPenalty;
            if (bestFace == nullptr || score < bestScore) {
                bestFace = face;
                bestScore = score;
            }
        }
        return bestFace;
    }

    struct RasterTextSegment
    {
        const wsc::FontFace *face = nullptr;
        std::size_t sourceStart = 0;
        std::size_t sourceEnd = 0;
    };

    struct RasterGlyphLayout
    {
        std::uint64_t atlasGeneration = 0;
        float drawXOffset = 0.0f;
        float drawYOffset = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        std::vector<TextRenderResult::GlyphAtlasQuad> quads;
    };

    std::optional<std::vector<RasterTextSegment>> buildRasterTextSegments(const std::string &normalizedText,
                                                                          const Paint &paint,
                                                                          std::size_t sourceStart,
                                                                          std::size_t sourceEnd) const
    {
        std::vector<RasterTextSegment> segments;
        const wsc::FontFace *currentFace = nullptr;
        std::size_t currentStart = 0;
        std::size_t currentEnd = 0;

        const auto finishCurrent = [&]() {
            if (currentFace != nullptr && currentEnd > currentStart) {
                segments.push_back({currentFace, currentStart, currentEnd});
            }
        };

        for (const wsc::text::Utf8Codepoint &codepoint : wsc::text::decodeUtf8(normalizedText)) {
            if (codepoint.offset < sourceStart) {
                continue;
            }
            if (codepoint.offset >= sourceEnd) {
                break;
            }
            if (codepoint.value == '\n') {
                break;
            }
            if (codepoint.value < 32 || wsc::text::isBidiControlCodepoint(codepoint.value)
                || wsc::text::isZeroWidthBreakCodepoint(codepoint.value)) {
                continue;
            }

            const wsc::FontFace *face = findRasterFaceForCodepoint(codepoint.value, paint);
            if (face == nullptr) {
                return std::nullopt;
            }

            if (face != currentFace) {
                finishCurrent();
                currentFace = face;
                currentStart = codepoint.offset;
            }
            currentEnd = codepoint.offset + codepoint.length;
        }

        finishCurrent();
        if (segments.empty()) {
            return std::nullopt;
        }
        return segments;
    }

    std::optional<float> measureRasterizedTextWidth(const std::string &normalizedText, const Paint &paint) const
    {
        const auto run = shapeRasterizedText(normalizedText, paint);
        return run ? std::optional<float>(run->width) : std::nullopt;
    }

    std::optional<TextRenderResult> renderRasterizedText(const std::string &normalizedText, float x, float y,
                                                         const Paint &paint) const
    {
        const std::string layoutCacheKey =
            rasterLayoutCacheKey(normalizedText, paint);
        const std::uint64_t atlasGeneration =
            glyphAtlas_.stats().generation;
        if (const auto cached = rasterLayoutCache_.find(layoutCacheKey);
            cached != rasterLayoutCache_.end()
            && cached->second.atlasGeneration == atlasGeneration) {
            touchCacheEntry(
                layoutCacheKey, rasterLayoutCache_,
                rasterLayoutCacheOrder_, kMaxRasterLayoutCacheEntries);
            TextRenderResult result;
            result.kind = TextRenderKind::GlyphAtlas;
            result.drawX = x + cached->second.drawXOffset;
            result.drawY = y + cached->second.drawYOffset;
            result.width = cached->second.width;
            result.height = cached->second.height;
            result.glyphAtlasQuads = cached->second.quads;
            for (TextRenderResult::GlyphAtlasQuad &quad :
                 result.glyphAtlasQuads) {
                quad.x += x;
                quad.y += y;
            }
            populateAtlasResult(result);
            return result;
        }

        const auto shapedRun = shapeRasterizedText(normalizedText, paint);
        if (!shapedRun) {
            addDiagnosticOnce(wsc::text::TextBackendDiagnostic::Severity::Warning,
                              "raster-shape#" + diagnosticFontFamily(paint),
                              "Raster text shaping failed; falling back to alternate text path.",
                              0u,
                              diagnosticFontFamily(paint));
            return std::nullopt;
        }

        float alignedX = x;
        if (paint.getTextAlign() == Paint::TextAlign::CENTER) {
            alignedX -= shapedRun->width * 0.5f;
        } else if (paint.getTextAlign() == Paint::TextAlign::RIGHT) {
            alignedX -= shapedRun->width;
        }

        TextRenderResult result;
        result.kind = TextRenderKind::GlyphAtlas;
        result.drawX = alignedX;
        result.drawY = y + wsc::text::textBaselineOffset(paint.getTextBaseline(), paint.getTextSize());
        result.width = shapedRun->width;
        result.height = paint.getTextSize();

        struct PendingGlyphDraw
        {
            wsc::text::ShapedGlyph glyph;
            wsc::text::GlyphKey key;
            std::optional<wsc::text::GlyphBitmap> bitmap;
            std::optional<wsc::text::GlyphAtlasEntry> cachedEntry;
        };

        std::vector<PendingGlyphDraw> pendingGlyphs;
        pendingGlyphs.reserve(shapedRun->glyphs.size());
        for (const wsc::text::ShapedGlyph &glyph : shapedRun->glyphs) {
            const wsc::FontFace *face = findRasterFaceForCodepoint(glyph.codepoint, paint);
            if (face == nullptr) {
                addDiagnosticOnce(wsc::text::TextBackendDiagnostic::Severity::Warning,
                                  "raster-face#" + diagnosticFontFamily(paint) + "#"
                                      + std::to_string(glyph.codepoint),
                                  "No raster font face resolved for shaped glyph.",
                                  glyph.codepoint,
                                  diagnosticFontFamily(paint));
                return std::nullopt;
            }

            // Most UI text is stable across frames. Consult the atlas before
            // asking FreeType to rasterize a glyph again; the atlas already
            // owns both the bitmap and its metrics for the common alpha case.
            wsc::text::GlyphKey cachedKey;
            cachedKey.fontFamily = face->family();
            cachedKey.codepoint = glyph.codepoint;
            cachedKey.glyphIndex = glyph.glyphIndex;
            cachedKey.pixelSize = paint.getTextSize();
            cachedKey.format = wsc::text::GlyphBitmapFormat::Alpha;
            cachedKey.weight = face->weight();
            cachedKey.slant = face->slant();
            if (const auto *cached = glyphAtlas_.find(cachedKey)) {
                pendingGlyphs.push_back(
                    {glyph, std::move(cachedKey), std::nullopt, *cached});
                continue;
            }

            auto rasterized = glyph.glyphIndex > 0
                ? rasterizer_.rasterizeGlyphIndex(*face, glyph.glyphIndex, glyph.codepoint, paint.getTextSize())
                : rasterizer_.rasterizeGlyph(*face, glyph.codepoint, paint.getTextSize());
            if (!rasterized) {
                addDiagnosticOnce(wsc::text::TextBackendDiagnostic::Severity::Warning,
                                  "raster-glyph#" + face->family() + "#" + std::to_string(glyph.codepoint)
                                      + "#" + std::to_string(glyph.glyphIndex),
                                  "Glyph rasterization failed; falling back to alternate text path.",
                                  glyph.codepoint,
                                  face->family());
                return std::nullopt;
            }

            pendingGlyphs.push_back(
                {glyph, std::move(rasterized->key),
                 std::move(rasterized->bitmap), std::nullopt});
        }

        const float baselineY = result.drawY + paint.getTextSize();
        const float spacing = std::isfinite(paint.getLetterSpacing()) ? paint.getLetterSpacing() : 0.0f;
        bool uploadedConsistentGeneration = false;
        for (int attempt = 0; attempt < 3 && !uploadedConsistentGeneration; ++attempt) {
            result.glyphAtlasQuads.clear();
            float penX = alignedX;
            bool usedRasterGlyph = false;
            bool restartUpload = false;

            for (const PendingGlyphDraw &pending : pendingGlyphs) {
                if (usedRasterGlyph) {
                    penX += spacing;
                }

                if (pending.cachedEntry || pending.bitmap) {
                    const std::uint64_t generationBeforeUpload = glyphAtlas_.stats().generation;
                    const auto entry = pending.cachedEntry
                        ? pending.cachedEntry
                        : glyphAtlas_.uploadGlyph(pending.key, *pending.bitmap);
                    if (!entry) {
                        addDiagnosticOnce(wsc::text::TextBackendDiagnostic::Severity::Warning,
                                          "atlas-upload#" + std::to_string(pending.glyph.codepoint),
                                          "Glyph atlas upload failed; falling back to alternate text path.",
                                          pending.glyph.codepoint,
                                          diagnosticFontFamily(paint));
                        return std::nullopt;
                    }
                    const std::uint64_t generationAfterUpload = glyphAtlas_.stats().generation;
                    if (generationAfterUpload != generationBeforeUpload && !result.glyphAtlasQuads.empty()) {
                        restartUpload = true;
                        break;
                    }

                    if (entry->width > 0 && entry->height > 0) {
                        TextRenderResult::GlyphAtlasQuad quad;
                        quad.x = penX + pending.glyph.offsetX + entry->bearingX;
                        quad.y = baselineY + pending.glyph.offsetY + entry->bearingY;
                        quad.width = static_cast<float>(entry->width);
                        quad.height = static_cast<float>(entry->height);
                        quad.u0 = entry->u0;
                        quad.v0 = entry->v0;
                        quad.u1 = entry->u1;
                        quad.v1 = entry->v1;
                        result.glyphAtlasQuads.push_back(quad);
                    }
                }

                penX += pending.glyph.advanceX;
                usedRasterGlyph = true;
            }

            uploadedConsistentGeneration = !restartUpload;
        }

        if (!uploadedConsistentGeneration) {
            addDiagnosticOnce(wsc::text::TextBackendDiagnostic::Severity::Warning,
                              "atlas-generation#" + diagnosticFontFamily(paint),
                              "Glyph atlas upload did not stabilize after retries.",
                              0u,
                              diagnosticFontFamily(paint));
            return std::nullopt;
        }

        if (result.glyphAtlasQuads.empty()) {
            addDiagnosticOnce(wsc::text::TextBackendDiagnostic::Severity::Warning,
                              "atlas-empty#" + diagnosticFontFamily(paint),
                              "Glyph atlas render emitted no drawable quads.",
                              0u,
                              diagnosticFontFamily(paint));
            return std::nullopt;
        }

        RasterGlyphLayout cachedLayout;
        cachedLayout.atlasGeneration = glyphAtlas_.stats().generation;
        cachedLayout.drawXOffset = result.drawX - x;
        cachedLayout.drawYOffset = result.drawY - y;
        cachedLayout.width = result.width;
        cachedLayout.height = result.height;
        cachedLayout.quads = result.glyphAtlasQuads;
        for (TextRenderResult::GlyphAtlasQuad &quad :
             cachedLayout.quads) {
            quad.x -= x;
            quad.y -= y;
        }
        rasterLayoutCache_[layoutCacheKey] = std::move(cachedLayout);
        touchCacheEntry(
            layoutCacheKey, rasterLayoutCache_,
            rasterLayoutCacheOrder_, kMaxRasterLayoutCacheEntries);
        populateAtlasResult(result);
        return result;
    }

    void populateAtlasResult(TextRenderResult &result) const
    {
        const wsc::text::GlyphAtlasStats stats = glyphAtlas_.stats();
        result.atlasWidth = stats.width;
        result.atlasHeight = stats.height;
        // The Canvas owns this backend for its entire lifetime, so returning
        // an atlas view is safe.
        result.atlasAlphaPixelsView = &glyphAtlas_.pixels();
        if (glyphAtlas_.hasColorPixels()) {
            result.atlasPixelFormat =
                wsc::text::GlyphAtlasPixelFormat::RGBA;
            result.atlasRgbaPixelsView = &glyphAtlas_.rgbaPixels();
        }
        result.atlasRevision = stats.uploadCount;
        const std::vector<wsc::text::GlyphAtlasDirtyRect> dirtyRects =
            glyphAtlas_.consumeDirtyRects();
        result.atlasDirtyRects.reserve(dirtyRects.size());
        for (const wsc::text::GlyphAtlasDirtyRect &dirtyRect : dirtyRects) {
            TextRenderResult::GlyphAtlasDirtyRect resultRect;
            resultRect.x = dirtyRect.x;
            resultRect.y = dirtyRect.y;
            resultRect.width = dirtyRect.width;
            resultRect.height = dirtyRect.height;
            result.atlasDirtyRects.push_back(resultRect);
        }
    }

    std::optional<wsc::text::ShapedTextRun> shapeRasterizedText(const std::string &normalizedText,
                                                                const Paint &paint) const
    {
        if (!paint.hasFontFamily()) {
            if (!fontManager_.hasFamily(wsc::FontSystem::kDefaultPrimaryFamily)) {
                return std::nullopt;
            }
        }

        if (!shaper_) {
            return std::nullopt;
        }

        const std::string cacheKey = rasterShapeCacheKey(normalizedText, paint);
        if (const auto cached = rasterShapeCache_.find(cacheKey); cached != rasterShapeCache_.end()) {
            touchCacheEntry(cacheKey, rasterShapeCache_, rasterShapeCacheOrder_, kMaxRasterShapeCacheEntries);
            return cached->second;
        }

        std::vector<wsc::text::BidiRun> bidiRuns = wsc::text::segmentBidiRuns(normalizedText);
        if (bidiRuns.empty()) {
            return std::nullopt;
        }

        wsc::text::ShapedTextRun combined;
        bool hasGlyph = false;
        const float spacing = std::isfinite(paint.getLetterSpacing()) ? paint.getLetterSpacing() : 0.0f;

        for (const wsc::text::BidiRun &bidiRun : bidiRuns) {
            const auto segments = buildRasterTextSegments(normalizedText, paint, bidiRun.sourceStart, bidiRun.sourceEnd);
            if (!segments) {
                return std::nullopt;
            }

            for (const RasterTextSegment &segment : *segments) {
                if (segment.face == nullptr || segment.sourceEnd <= segment.sourceStart
                    || segment.sourceEnd > normalizedText.size()) {
                    return std::nullopt;
                }

                const std::string segmentText = normalizedText.substr(segment.sourceStart,
                                                                      segment.sourceEnd - segment.sourceStart);
                wsc::text::TextShapeInput input;
                input.normalizedText = segmentText;
                input.letterSpacing = paint.getLetterSpacing();
                input.pixelSize = paint.getTextSize();
                input.fontData = rasterizer_.fontData(*segment.face);

                const auto resolver = [&](std::uint32_t codepoint) -> std::optional<wsc::text::ResolvedGlyph> {
                    const auto metrics = rasterizer_.glyphMetrics(*segment.face, codepoint, paint.getTextSize());
                    if (!metrics) {
                        return std::nullopt;
                    }
                    return wsc::text::ResolvedGlyph{metrics->glyphIndex, metrics->advanceX};
                };

                auto shaped = shaper_->shape(input, resolver);
                if (!shaped && shaper_->supportsOpenTypeFeatures()) {
                    shaped = wsc::text::shapeTextSimple(segmentText, paint.getLetterSpacing(), resolver);
                }
                if (!shaped) {
                    return std::nullopt;
                }

                if (!shaper_->supportsOpenTypeFeatures()) {
                    for (std::size_t index = 0; index + 1 < shaped->glyphs.size(); ++index) {
                        const auto kerning = rasterizer_.glyphKerning(*segment.face,
                                                                       shaped->glyphs[index].glyphIndex,
                                                                       shaped->glyphs[index + 1].glyphIndex,
                                                                       paint.getTextSize());
                        if (!kerning || *kerning == 0.0f) {
                            continue;
                        }
                        const float adjustedAdvance = std::max(0.0f, shaped->glyphs[index].advanceX + *kerning);
                        shaped->width += adjustedAdvance - shaped->glyphs[index].advanceX;
                        shaped->glyphs[index].advanceX = adjustedAdvance;
                    }
                }

                combined.rightToLeft = combined.rightToLeft || bidiRun.rightToLeft || shaped->rightToLeft;
                for (wsc::text::ShapedGlyph glyph : shaped->glyphs) {
                    glyph.sourceStart += segment.sourceStart;
                    if (hasGlyph) {
                        combined.width += spacing;
                    }
                    combined.width += glyph.advanceX;
                    combined.glyphs.push_back(glyph);
                    hasGlyph = true;
                }
            }
        }

        if (!hasGlyph) {
            return std::nullopt;
        }
        combined.width = std::max(0.0f, combined.width);
        rasterShapeCache_[cacheKey] = combined;
        touchCacheEntry(cacheKey, rasterShapeCache_, rasterShapeCacheOrder_, kMaxRasterShapeCacheEntries);
        return combined;
    }

    std::string diagnosticFontFamily(const Paint &paint) const
    {
        return paint.hasFontFamily() ? paint.getFontFamily() : wsc::FontSystem::kDefaultPrimaryFamily;
    }

    void addDiagnosticOnce(wsc::text::TextBackendDiagnostic::Severity severity,
                           const std::string &key,
                           const std::string &message,
                           std::uint32_t codepoint,
                           const std::string &family) const
    {
        if (!diagnosticKeys_.insert(key).second) {
            return;
        }

        wsc::text::TextBackendDiagnostic diagnostic;
        diagnostic.severity = severity;
        diagnostic.message = message;
        diagnostic.codepoint = codepoint;
        diagnostic.fontFamily = family;
        diagnostics_.push_back(std::move(diagnostic));
    }

    void addMissingGlyphDiagnostic(std::uint32_t codepoint, const std::string &family) const
    {
        const std::string key = family + '#' + std::to_string(codepoint);
        if (!missingGlyphDiagnosticKeys_.insert(key).second) {
            return;
        }

        wsc::text::TextBackendDiagnostic diagnostic;
        diagnostic.severity = wsc::text::TextBackendDiagnostic::Severity::Warning;
        diagnostic.message = "Missing glyph for requested codepoint.";
        diagnostic.codepoint = codepoint;
        diagnostic.fontFamily = family;
        diagnostics_.push_back(std::move(diagnostic));
    }

    wsc::text::BasicTextBackendOptions options_;
    std::unique_ptr<wsc::text::ITextShapingEngine> shaper_;

    template <typename TValue>
    static void touchCacheEntry(const std::string &cacheKey,
                                std::unordered_map<std::string, TValue> &cache,
                                std::deque<std::string> &order,
                                std::size_t capacity)
    {
        auto existing = std::find(order.begin(), order.end(), cacheKey);
        if (existing != order.end()) {
            order.erase(existing);
        }

        order.push_back(cacheKey);
        while (order.size() > capacity) {
            const std::string evictedKey = order.front();
            order.pop_front();
            cache.erase(evictedKey);
        }
    }

    static std::string rasterShapeCacheKey(const std::string &text, const Paint &paint)
    {
        return text + '\x1f' + paint.getFontFamily() + '\x1f' + std::to_string(paint.getTextSize()) + '\x1f'
               + std::to_string(paint.getLetterSpacing()) + '\x1f' + std::to_string(paint.getFontWeight()) + '\x1f'
               + std::to_string(static_cast<int>(paint.getFontSlant())) + '\x1f'
               + paint.getTextLocale();
    }

    static std::string rasterLayoutCacheKey(
        const std::string &text, const Paint &paint)
    {
        return rasterShapeCacheKey(text, paint) + '\x1f'
            + std::to_string(static_cast<int>(paint.getTextAlign()))
            + '\x1f'
            + std::to_string(static_cast<int>(paint.getTextBaseline()));
    }

    static std::string rasterFaceCacheKey(std::uint32_t codepoint, const Paint &paint)
    {
        return paint.getFontFamily() + '\x1f' + std::to_string(codepoint) + '\x1f'
               + std::to_string(paint.getFontWeight()) + '\x1f'
               + std::to_string(static_cast<int>(paint.getFontSlant()));
    }

    void clearRasterCaches()
    {
        rasterShapeCache_.clear();
        rasterShapeCacheOrder_.clear();
        rasterLayoutCache_.clear();
        rasterLayoutCacheOrder_.clear();
        rasterFaceCache_.clear();
    }

#ifdef _WIN32

    static std::string makeNativeCacheKey(const std::string &text, const Paint &paint)
    {
        return text + '\x1f' + paint.getFontFamily() + '\x1f' +
               std::to_string(paint.getTextSize()) + '\x1f' +
               std::to_string(paint.getLetterSpacing()) + '\x1f' +
               std::to_string(paint.getFontWeight()) + '\x1f' +
               std::to_string(static_cast<int>(paint.getFontSlant())) + '\x1f' +
               paint.getTextLocale();
    }

    wsc::text::NativeTextMeasure getNativeMeasure(const std::string &text, const Paint &paint) const
    {
        const std::string cacheKey = makeNativeCacheKey(text, paint);
        auto cached = nativeMeasureCache_.find(cacheKey);
        if (cached != nativeMeasureCache_.end()) {
            touchCacheEntry(cacheKey, nativeMeasureCache_, nativeMeasureCacheOrder_, kMaxNativeTextCacheEntries);
            return cached->second;
        }

        const auto measure = wsc::text::measureNativeText(text, paint);
        nativeMeasureCache_[cacheKey] = measure;
        touchCacheEntry(cacheKey, nativeMeasureCache_, nativeMeasureCacheOrder_, kMaxNativeTextCacheEntries);
        return measure;
    }

    wsc::text::NativeTextBitmap getNativeBitmap(const std::string &text, const Paint &paint,
                                                        const wsc::text::NativeTextMeasure &measure) const
    {
        const std::string cacheKey = makeNativeCacheKey(text, paint);
        auto cached = nativeBitmapCache_.find(cacheKey);
        if (cached != nativeBitmapCache_.end()) {
            touchCacheEntry(cacheKey, nativeBitmapCache_, nativeBitmapCacheOrder_, kMaxNativeTextCacheEntries);
            return cached->second;
        }

        const auto bitmap = wsc::text::renderNativeTextBitmap(text, paint, measure);
        nativeBitmapCache_[cacheKey] = bitmap;
        touchCacheEntry(cacheKey, nativeBitmapCache_, nativeBitmapCacheOrder_, kMaxNativeTextCacheEntries);
        return bitmap;
    }

    mutable std::unordered_map<std::string, wsc::text::NativeTextMeasure> nativeMeasureCache_;
    mutable std::unordered_map<std::string, wsc::text::NativeTextBitmap> nativeBitmapCache_;
    mutable std::deque<std::string> nativeMeasureCacheOrder_;
    mutable std::deque<std::string> nativeBitmapCacheOrder_;
#endif
    wsc::FontManager fontManager_;
    mutable wsc::text::FontRasterizer rasterizer_;
    mutable wsc::text::GlyphAtlas glyphAtlas_{kDefaultGlyphAtlasSize, kDefaultGlyphAtlasSize, 1};
    std::unique_ptr<wsc::text::ITextBackend> directWriteBackend_;
    mutable std::unordered_map<std::string, wsc::text::ShapedTextRun> rasterShapeCache_;
    mutable std::deque<std::string> rasterShapeCacheOrder_;
    mutable std::unordered_map<std::string, RasterGlyphLayout> rasterLayoutCache_;
    mutable std::deque<std::string> rasterLayoutCacheOrder_;
    mutable std::unordered_map<std::string, const wsc::FontFace *> rasterFaceCache_;
    mutable std::vector<wsc::text::TextBackendDiagnostic> diagnostics_;
    mutable std::unordered_set<std::string> diagnosticKeys_;
    mutable std::unordered_set<std::string> missingGlyphDiagnosticKeys_;
};

} // namespace

namespace wsc::text {

std::unique_ptr<ITextBackend> createBasicTextBackend()
{
    return createBasicTextBackend(BasicTextBackendOptions{});
}

std::vector<TextBackendCapability> queryTextBackendCapabilities()
{
    std::vector<TextBackendCapability> capabilities;
    capabilities.push_back({TextBackendKind::Portable,
                            "portable",
                            true,
                            false,
                            true,
                            true,
                            true,
                            isOpenTypeShapingAvailable()});
    capabilities.push_back({TextBackendKind::WindowsNative,
                            "windows-native",
#if defined(_WIN32)
                            true,
#else
                            false,
#endif
                            true,
                            false,
                            false,
                            false,
                            false});
    capabilities.push_back({TextBackendKind::DirectWrite,
                            "directwrite",
                            wsc::text::isDirectWriteAvailable(),
                            true,
                            true,
                            false,
                            false,
                            true});
    capabilities.push_back({TextBackendKind::CoreText,
                            "coretext",
                            false,
                            true,
                            false,
                            false,
                            false,
                            false});
    return capabilities;
}

std::unique_ptr<ITextBackend> createBasicTextBackend(const BasicTextBackendOptions &options)
{
    return std::make_unique<BasicTextBackend>(options);
}

bool isNativeDirectWriteActive(const ITextBackend *backend)
{
    const auto *basic = dynamic_cast<const BasicTextBackend *>(backend);
    return basic != nullptr && basic->hasNativeDirectWriteBackend();
}

std::unique_ptr<ITextBackend> createPortableTextBackend()
{
    BasicTextBackendOptions options;
    options.backendKind = TextBackendKind::Portable;
    options.enableNativeText = false;
    options.enableSystemFontFallback = false;
    return createBasicTextBackend(options);
}

std::unique_ptr<ITextBackend> createTextBackend(TextBackendKind kind)
{
    BasicTextBackendOptions options;
    options.backendKind = kind;
    switch (kind) {
    case TextBackendKind::Portable:
    case TextBackendKind::DirectWrite:
    case TextBackendKind::CoreText:
        options.enableNativeText = false;
        break;
    case TextBackendKind::WindowsNative:
#if defined(_WIN32)
        options.enableNativeText = true;
#else
        options.enableNativeText = false;
#endif
        break;
    case TextBackendKind::Auto:
        options.enableNativeText = true;
        break;
    }
    return createBasicTextBackend(options);
}

} // namespace wsc::text

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
constexpr int kDefaultGlyphAtlasSize = 1024;

class BasicTextBackend final : public wsc::text::ITextBackend
{
public:
    explicit BasicTextBackend(wsc::text::BasicTextBackendOptions options)
        : options_(options),
          shaper_(wsc::text::createTextShapingEngine(options_.shapingBackend))
    {
        if (options_.shapingBackend == wsc::text::TextShapingBackend::OpenType
            && (shaper_ == nullptr || !shaper_->supportsOpenTypeFeatures())) {
            diagnostics_.push_back({wsc::text::TextBackendDiagnostic::Severity::Warning,
                                    "OpenType shaping backend is unavailable; using simple shaping."});
        }
        if (shaper_ == nullptr) {
            shaper_ = wsc::text::createSimpleTextShapingEngine();
        }
    }

    bool registerFontFace(const wsc::FontFace &face) override
    {
        const bool registered = fontManager_.registerFace(face);
        if (!registered) {
            diagnostics_.push_back({wsc::text::TextBackendDiagnostic::Severity::Warning,
                                    "Rejected invalid font face registration."});
        }
        return registered;
    }

    bool setFontFallbackChain(const wsc::FontFallbackChain &chain) override
    {
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
        if (fontManager_.hasFamily(preferredFamily)) {
            return fontManager_.resolveFamilies(preferredFamily);
        }
        return preferredFamily.empty() ? std::vector<std::string>() : std::vector<std::string>{preferredFamily};
    }

    std::vector<wsc::text::TextLineBreak> breakLines(const std::string &text, float maxWidth,
                                                     const Paint &paint) const override
    {
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
            std::size_t position = paragraphStart;
            while (position < paragraphEnd) {
                while (position < paragraphEnd && normalizedText[position] == ' ') {
                    ++position;
                }
                if (position >= paragraphEnd) {
                    break;
                }

                const std::size_t wordStart = position;
                while (position < paragraphEnd && normalizedText[position] != ' ') {
                    ++position;
                }

                const std::string word = normalizedText.substr(wordStart, position - wordStart);
                const std::string candidate = currentLine.empty() ? word : currentLine + " " + word;
                if (currentLine.empty() || measureTextWidth(candidate, paint) <= maxWidth) {
                    if (currentLine.empty()) {
                        currentStart = wordStart;
                    }
                    currentLine = candidate;
                    currentEnd = position;
                } else {
                    result.push_back({currentStart, currentEnd - currentStart,
                                      measureTextWidth(currentLine, paint)});
                    currentLine = word;
                    currentStart = wordStart;
                    currentEnd = position;
                }
            }

            if (!currentLine.empty()) {
                result.push_back({currentStart, currentEnd - currentStart,
                                  measureTextWidth(currentLine, paint)});
            }
        };

        std::size_t paragraphStart = 0;
        for (std::size_t i = 0; i <= normalizedText.size(); ++i) {
            if (i == normalizedText.size() || normalizedText[i] == '\n') {
                appendParagraph(paragraphStart, i);
                paragraphStart = i + 1;
            }
        }
        return result;
    }

    bool hasGlyphForCodepoint(std::uint32_t codepoint, const Paint &paint) const override
    {
        if (codepoint == '\n' || codepoint == '\t' || wsc::text::isBidiControlCodepoint(codepoint)
            || (codepoint >= 32 && codepoint <= 126)) {
            return true;
        }

        if (paint.hasFontFamily()) {
            const std::vector<std::string> families = resolveFontFamilies(paint.getFontFamily());
            for (const std::string &family : families) {
                for (const wsc::FontFace *face : fontManager_.findFaces(family)) {
                    if (face != nullptr
                        && ((face->hasCodepointRanges() && face->supportsCodepoint(codepoint))
                            || rasterizer_.hasGlyph(*face, codepoint))) {
                        return true;
                    }
                }
            }
        }

#ifdef _WIN32
        if (options_.enableNativeText && paint.hasFontFamily()) {
            return true;
        }
#endif
        addMissingGlyphDiagnostic(codepoint, paint.hasFontFamily() ? paint.getFontFamily() : std::string());
        return false;
    }

    std::vector<wsc::text::TextBackendDiagnostic> diagnostics() const override
    {
        return diagnostics_;
    }

    float measureTextWidth(const std::string &text, const Paint &paint) const override
    {
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
        if (normalizedText.empty() || paint.getTextSize() <= 0.0f || !paint.hasFontFamily()) {
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
                    result.drawX = alignedX;
                    result.drawY = y + wsc::text::textBaselineOffset(paint.getTextBaseline(), nativeMeasure.height);
                    result.width = nativeMeasure.width;
                    result.height = nativeMeasure.height;
                    result.bitmapWidth = bitmap.width;
                    result.bitmapHeight = bitmap.height;
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
    const wsc::FontFace *findRasterFaceForCodepoint(std::uint32_t codepoint, const Paint &paint) const
    {
        if (!paint.hasFontFamily()) {
            return nullptr;
        }

        for (const std::string &family : resolveFontFamilies(paint.getFontFamily())) {
            for (const wsc::FontFace *face : fontManager_.findFaces(family)) {
                if (face != nullptr && rasterizer_.hasGlyph(*face, codepoint)) {
                    return face;
                }
            }
        }
        return nullptr;
    }

    struct RasterTextSegment
    {
        const wsc::FontFace *face = nullptr;
        std::size_t sourceStart = 0;
        std::size_t sourceEnd = 0;
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
            if (codepoint.value < 32 || wsc::text::isBidiControlCodepoint(codepoint.value)) {
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
        const auto shapedRun = shapeRasterizedText(normalizedText, paint);
        if (!shapedRun) {
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

        float penX = alignedX;
        const float baselineY = result.drawY + paint.getTextSize();
        const float spacing = std::isfinite(paint.getLetterSpacing()) ? paint.getLetterSpacing() : 0.0f;
        bool usedRasterGlyph = false;
        for (const wsc::text::ShapedGlyph &glyph : shapedRun->glyphs) {
            const wsc::FontFace *face = findRasterFaceForCodepoint(glyph.codepoint, paint);
            if (face == nullptr) {
                return std::nullopt;
            }

            const auto rasterized = glyph.glyphIndex > 0
                ? rasterizer_.rasterizeGlyphIndex(*face, glyph.glyphIndex, glyph.codepoint, paint.getTextSize())
                : rasterizer_.rasterizeGlyph(*face, glyph.codepoint, paint.getTextSize());
            if (!rasterized) {
                return std::nullopt;
            }

            if (usedRasterGlyph) {
                penX += spacing;
            }

            if (rasterized->bitmap.width > 0 && rasterized->bitmap.height > 0) {
                const auto entry = glyphAtlas_.uploadGlyph(rasterized->key, rasterized->bitmap);
                if (!entry) {
                    return std::nullopt;
                }

                TextRenderResult::GlyphAtlasQuad quad;
                quad.x = penX + glyph.offsetX + entry->bearingX;
                quad.y = baselineY + glyph.offsetY + entry->bearingY;
                quad.width = static_cast<float>(entry->width);
                quad.height = static_cast<float>(entry->height);
                quad.u0 = entry->u0;
                quad.v0 = entry->v0;
                quad.u1 = entry->u1;
                quad.v1 = entry->v1;
                result.glyphAtlasQuads.push_back(quad);
            }

            penX += glyph.advanceX;
            usedRasterGlyph = true;
        }

        if (result.glyphAtlasQuads.empty()) {
            return std::nullopt;
        }

        result.atlasWidth = glyphAtlas_.stats().width;
        result.atlasHeight = glyphAtlas_.stats().height;
        result.atlasAlphaPixels = glyphAtlas_.pixels();
        if (glyphAtlas_.hasColorPixels()) {
            result.atlasPixelFormat = wsc::text::GlyphAtlasPixelFormat::RGBA;
            result.atlasRgbaPixels = glyphAtlas_.rgbaPixels();
        }
        const std::vector<wsc::text::GlyphAtlasDirtyRect> dirtyRects = glyphAtlas_.consumeDirtyRects();
        result.atlasDirtyRects.reserve(dirtyRects.size());
        for (const wsc::text::GlyphAtlasDirtyRect &dirtyRect : dirtyRects) {
            TextRenderResult::GlyphAtlasDirtyRect resultRect;
            resultRect.x = dirtyRect.x;
            resultRect.y = dirtyRect.y;
            resultRect.width = dirtyRect.width;
            resultRect.height = dirtyRect.height;
            result.atlasDirtyRects.push_back(resultRect);
        }
        return result;
    }

    std::optional<wsc::text::ShapedTextRun> shapeRasterizedText(const std::string &normalizedText,
                                                                const Paint &paint) const
    {
        if (!paint.hasFontFamily()) {
            return std::nullopt;
        }

        if (!shaper_) {
            return std::nullopt;
        }

        std::vector<wsc::text::BidiRun> bidiRuns = wsc::text::segmentBidiRuns(normalizedText);
        if (bidiRuns.empty()) {
            return std::nullopt;
        }
        if (bidiRuns.front().rightToLeft) {
            std::reverse(bidiRuns.begin(), bidiRuns.end());
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
        return combined;
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

#ifdef _WIN32
    template <typename TValue>
    static void touchCacheEntry(const std::string &cacheKey,
                                std::unordered_map<std::string, TValue> &cache,
                                std::deque<std::string> &order)
    {
        auto existing = std::find(order.begin(), order.end(), cacheKey);
        if (existing != order.end()) {
            order.erase(existing);
        }

        order.push_back(cacheKey);
        while (order.size() > kMaxNativeTextCacheEntries) {
            const std::string evictedKey = order.front();
            order.pop_front();
            cache.erase(evictedKey);
        }
    }

    static std::string makeNativeCacheKey(const std::string &text, const Paint &paint)
    {
        return text + '\x1f' + paint.getFontFamily() + '\x1f' +
               std::to_string(paint.getTextSize()) + '\x1f' +
               std::to_string(paint.getLetterSpacing());
    }

    wsc::text::NativeTextMeasure getNativeMeasure(const std::string &text, const Paint &paint) const
    {
        const std::string cacheKey = makeNativeCacheKey(text, paint);
        auto cached = nativeMeasureCache_.find(cacheKey);
        if (cached != nativeMeasureCache_.end()) {
            touchCacheEntry(cacheKey, nativeMeasureCache_, nativeMeasureCacheOrder_);
            return cached->second;
        }

        const auto measure = wsc::text::measureNativeText(text, paint);
        nativeMeasureCache_[cacheKey] = measure;
        touchCacheEntry(cacheKey, nativeMeasureCache_, nativeMeasureCacheOrder_);
        return measure;
    }

    wsc::text::NativeTextBitmap getNativeBitmap(const std::string &text, const Paint &paint,
                                                        const wsc::text::NativeTextMeasure &measure) const
    {
        const std::string cacheKey = makeNativeCacheKey(text, paint);
        auto cached = nativeBitmapCache_.find(cacheKey);
        if (cached != nativeBitmapCache_.end()) {
            touchCacheEntry(cacheKey, nativeBitmapCache_, nativeBitmapCacheOrder_);
            return cached->second;
        }

        const auto bitmap = wsc::text::renderNativeTextBitmap(text, paint, measure);
        nativeBitmapCache_[cacheKey] = bitmap;
        touchCacheEntry(cacheKey, nativeBitmapCache_, nativeBitmapCacheOrder_);
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
    mutable std::vector<wsc::text::TextBackendDiagnostic> diagnostics_;
    mutable std::unordered_set<std::string> missingGlyphDiagnosticKeys_;
};

} // namespace

namespace wsc::text {

std::unique_ptr<ITextBackend> createBasicTextBackend()
{
    return createBasicTextBackend(BasicTextBackendOptions{});
}

std::unique_ptr<ITextBackend> createBasicTextBackend(const BasicTextBackendOptions &options)
{
    return std::make_unique<BasicTextBackend>(options);
}

std::unique_ptr<ITextBackend> createPortableTextBackend()
{
    BasicTextBackendOptions options;
    options.enableNativeText = false;
    return createBasicTextBackend(options);
}

} // namespace wsc::text

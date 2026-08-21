#include <wsc/FontSystem.h>

#include "text/BasicTextBackend.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

#include "../../include/wsc/Font.h"
#include "../../include/wsc/FontResolver.h"
#include "canvas/Paint.h"
#include "render/LruCache.h"
#include "text/DirectWriteTextBackend.h"
#include "text/CoreTextTextBackend.h"
#include "text/FontRasterizer.h"
#include "text/GlyphAtlas.h"
#include "text/ITextBackend.h"
#include "text/NativeText.h"
#include "text/platform/AndroidFontProvider.h"
#include "text/TextShaper.h"
#include "text/TextUtils.h"

namespace {

using wsc::RectF;

using wsc::text::TextRenderKind;
using wsc::text::TextRenderResult;

using CpuClock = std::chrono::steady_clock;

std::uint64_t elapsedCpuTimeNs(CpuClock::time_point start)
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            CpuClock::now() - start).count());
}

constexpr size_t kMaxNativeTextCacheEntries = 128;
// Dynamic UI text commonly combines a few hundred strings with multiple
// sizes and weights. Keep that working set hot while retaining a firm bound.
constexpr size_t kMaxRasterShapeCacheEntries = 2048;
constexpr size_t kMaxRasterLayoutCacheEntries = 2048;
// Start modestly and grow on demand. A 2048x2048 atlas made the first colour
// emoji upgrade allocate and upload a 16 MiB RGBA texture even for a handful
// of labels. The atlas already has deterministic power-of-two growth and
// rebuild bookkeeping, so 1024 avoids that cold-start cliff while retaining
// the same 4096 maximum for dense text workloads.
constexpr int kDefaultGlyphAtlasSize = 1024;

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

wsc::FontFace applyPaintFontVariations(const wsc::FontFace &face,
                                       const Paint &paint)
{
    wsc::FontFace effective = face;
    for (const wsc::FontVariationCoordinate &coordinate :
         paint.getFontVariations()) {
        (void)effective.setVariationCoordinate(coordinate.tag, coordinate.value);
    }
    return effective;
}

void appendPaintFontVariations(std::string &key, const Paint &paint)
{
    key += '\x1d' + wsc::text::fontVariationIdentity(
        paint.getFontVariations());
}

class BasicTextBackend final : public wsc::text::ITextBackend
{
public:
    explicit BasicTextBackend(wsc::text::BasicTextBackendOptions options)
        : options_(options),
          shaper_(wsc::text::createTextShapingEngine(options_.shapingBackend))
    {
        if (auto androidProvider =
                wsc::text::createAndroidSystemFontProvider()) {
            platformSystemFontProvider_ = std::move(androidProvider);
            fontResolver_.addProvider(platformSystemFontProvider_);
        }
        fontResolver_.addProvider(std::make_shared<wsc::FontManagerProvider>(
            dynamicFontManager_, wsc::FontProviderKind::DYNAMIC, "dynamic"));
        fontResolver_.addProvider(std::make_shared<wsc::FontManagerProvider>(
            systemFontManager_, wsc::FontProviderKind::SYSTEM, "system"));

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
            wsc::text::CoreTextBackendOptions coreTextOptions;
            coreTextOptions.enableSystemFontFallback = options_.enableSystemFontFallback;
            coreTextBackend_ = wsc::text::createCoreTextTextBackend(coreTextOptions);
            if (coreTextBackend_ == nullptr) {
                diagnostics_.push_back({wsc::text::TextBackendDiagnostic::Severity::Warning,
                                        "CoreText text adapter is unavailable on this platform; using portable glyph-atlas backend."});
            }
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

    bool hasNativeCoreTextBackend() const
    {
        return coreTextBackend_ != nullptr;
    }

    wsc::text::ITextBackend *nativeBackend()
    {
        return directWriteBackend_ != nullptr ? directWriteBackend_.get()
             : coreTextBackend_ != nullptr ? coreTextBackend_.get() : nullptr;
    }

    const wsc::text::ITextBackend *nativeBackend() const
    {
        return directWriteBackend_ != nullptr ? directWriteBackend_.get()
             : coreTextBackend_ != nullptr ? coreTextBackend_.get() : nullptr;
    }

    wsc::text::TextRenderStats renderStats() const override
    {
        const auto *native = nativeBackend();
        return native != nullptr ? native->renderStats() : renderStats_;
    }

    void resetRenderStats() override
    {
        renderStats_ = {};
        if (auto *native = nativeBackend()) {
            native->resetRenderStats();
        }
    }

    bool registerFontFace(const wsc::FontFace &face) override
    {
        if (auto *native = nativeBackend()) {
            return native->registerFontFace(face);
        }
        clearRasterCaches();
        const bool registered = dynamicFontManager_->registerFace(face);
        if (!registered) {
            diagnostics_.push_back({wsc::text::TextBackendDiagnostic::Severity::Warning,
                                    "Rejected invalid font face registration."});
        }
        return registered;
    }

    bool addFontProvider(std::shared_ptr<wsc::FontProvider> provider) override
    {
        if (!provider) return false;
        if (auto *native = nativeBackend()) {
            return native->addFontProvider(std::move(provider));
        }
        clearRasterCaches();
        glyphAtlas_.clear();
        fontResolver_.addProvider(std::move(provider));
        return true;
    }

    bool refreshSystemFonts() override
    {
        if (auto *native = nativeBackend()) {
            return native->refreshSystemFonts();
        }

        wsc::FontSystem::refreshInstalledFonts();
        clearRasterCaches();
        rasterizer_.clearCache();
        glyphAtlas_.clear();
#ifdef _WIN32
        nativeMeasureCache_.clear();
        nativeBitmapCache_.clear();
#endif
        fontResolver_.refreshProviders(wsc::FontProviderKind::SYSTEM);
        systemFontManager_->clear();
        registerSystemFontFallbacks();
        for (const auto &entry : userFallbackChains_) {
            fontResolver_.setFallbackChain(entry.second);
        }
        return true;
    }

    bool setFontFallbackChain(const wsc::FontFallbackChain &chain) override
    {
        if (auto *native = nativeBackend()) {
            return native->setFontFallbackChain(chain);
        }
        clearRasterCaches();
        if (chain.primaryFamily().empty() || !fontResolver_.hasFamily(chain.primaryFamily())) {
            diagnostics_.push_back({wsc::text::TextBackendDiagnostic::Severity::Warning,
                                    "Rejected fallback chain for an unknown primary family."});
            return false;
        }

        const bool ok = fontResolver_.setFallbackChain(chain);
        if (!ok) {
            diagnostics_.push_back({wsc::text::TextBackendDiagnostic::Severity::Warning,
                                    "Skipped one or more unknown fallback families."});
        } else {
            userFallbackChains_.insert_or_assign(chain.primaryFamily(), chain);
        }
        return ok;
    }

    std::vector<std::string> resolveFontFamilies(const std::string &preferredFamily) const override
    {
        if (const auto *native = nativeBackend()) {
            return native->resolveFontFamilies(preferredFamily);
        }
        if (fontResolver_.hasFamily(preferredFamily)) {
            return fontResolver_.resolveFamilies(preferredFamily);
        }
        if (preferredFamily.empty() && fontResolver_.hasFamily(wsc::FontSystem::kDefaultPrimaryFamily)) {
            return fontResolver_.resolveFamilies(wsc::FontSystem::kDefaultPrimaryFamily);
        }
        return preferredFamily.empty() ? std::vector<std::string>() : std::vector<std::string>{preferredFamily};
    }

    std::vector<wsc::text::TextLineBreak> breakLines(const std::string &text, float maxWidth,
                                                     const Paint &paint) const override
    {
        if (const auto *native = nativeBackend()) {
            return native->breakLines(text, maxWidth, paint);
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
            auto appendClusterToken = [&](const std::string &tokenText,
                                          std::size_t tokenStart,
                                          std::size_t tokenEnd) {
                const auto clusters =
                    wsc::text::buildFontFallbackClusters(tokenText, 0, tokenText.size());
                for (const wsc::text::FontFallbackCluster &cluster : clusters) {
                    const std::string clusterText = tokenText.substr(
                        cluster.sourceStart, cluster.sourceEnd - cluster.sourceStart);
                    const std::size_t clusterStart = tokenStart + cluster.sourceStart;
                    const std::size_t clusterEnd = std::min(tokenStart + cluster.sourceEnd, tokenEnd);
                    const std::string candidate = currentLine.empty()
                        ? clusterText
                        : currentLine + clusterText;
                    if (!currentLine.empty() && measureTextWidth(candidate, paint) > maxWidth) {
                        pushCurrentLine();
                    }
                    if (currentLine.empty()) {
                        currentStart = clusterStart;
                    }
                    currentLine += clusterText;
                    currentEnd = clusterEnd;
                }
            };
            const std::vector<wsc::text::TextBreakToken> tokens =
                wsc::text::buildTextBreakTokens(normalizedText, paragraphStart, paragraphEnd);
            for (const wsc::text::TextBreakToken &token : tokens) {
                const std::string tokenText = normalizedText.substr(token.sourceStart, token.sourceEnd - token.sourceStart);
                const std::string candidate =
                    currentLine.empty() ? tokenText : currentLine + (token.prefixSpace ? " " : "") + tokenText;
                if (currentLine.empty() && measureTextWidth(tokenText, paint) > maxWidth) {
                    appendClusterToken(tokenText, token.sourceStart, token.sourceEnd);
                } else if (currentLine.empty() || measureTextWidth(candidate, paint) <= maxWidth) {
                    if (currentLine.empty()) {
                        currentStart = token.sourceStart;
                    }
                    currentLine = candidate;
                    currentEnd = token.sourceEnd;
                } else {
                    pushCurrentLine();
                    if (measureTextWidth(tokenText, paint) > maxWidth) {
                        appendClusterToken(tokenText, token.sourceStart, token.sourceEnd);
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
        if (const auto *native = nativeBackend()) {
            return native->hasGlyphForCodepoint(codepoint, paint);
        }
        if (codepoint == '\n' || codepoint == '\t' || wsc::text::isBidiControlCodepoint(codepoint)
            || wsc::text::isZeroWidthBreakCodepoint(codepoint)
            || (codepoint >= 32 && codepoint <= 126)) {
            return true;
        }

        if (resolveRasterFace({codepoint}, paint, true) != nullptr) return true;

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
        std::vector<wsc::text::TextBackendDiagnostic> combined = diagnostics_;
        if (directWriteBackend_ != nullptr) {
            const auto dwDiags = directWriteBackend_->diagnostics();
            combined.insert(combined.end(), dwDiags.begin(), dwDiags.end());
        }
        if (coreTextBackend_ != nullptr) {
            const auto coreTextDiagnostics = coreTextBackend_->diagnostics();
            combined.insert(combined.end(), coreTextDiagnostics.begin(),
                            coreTextDiagnostics.end());
        }
        return combined;
    }

    float measureTextWidth(const std::string &text, const Paint &paint) const override
    {
        if (const auto *native = nativeBackend()) {
            return native->measureTextWidth(text, paint);
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
        if (const auto *native = nativeBackend()) {
            return native->measureTextBounds(text, paint);
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
        if (const auto *native = nativeBackend()) {
            return native->measureTextMetrics(text, paint);
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
            || (!paint.hasFontFamily() && !fontResolver_.hasFamily(wsc::FontSystem::kDefaultPrimaryFamily))) {
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
                const wsc::FontFace effectiveFace =
                    applyPaintFontVariations(*segment.face, paint);
                const auto vertical = rasterizer_.verticalMetrics(
                    effectiveFace, paint.getTextSize());
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
        return renderTextImpl(text, x, y, paint, false);
    }

    TextRenderResult renderTextView(const std::string &text, float x, float y,
                                    const Paint &paint) const override
    {
        return renderTextImpl(text, x, y, paint, true);
    }

private:
    TextRenderResult renderTextImpl(const std::string &text, float x, float y,
                                    const Paint &paint, bool allowLayoutView) const
    {
        if (const auto *native = nativeBackend()) {
            return native->renderTextView(text, x, y, paint);
        }
        TextRenderResult result;
        ++renderStats_.normalizationCount;
        const auto normalizationStart = CpuClock::now();
        const std::string normalizedText = wsc::text::normalizeUtf8ForText(text);
        renderStats_.normalizationCpuTimeNs +=
            elapsedCpuTimeNs(normalizationStart);
        if (normalizedText.empty() || paint.getTextSize() <= 0.0f) {
            return result;
        }

        if (auto atlasResult = renderRasterizedText(normalizedText, x, y, paint,
                                                    allowLayoutView)) {
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
                || wsc::text::isVariationSelectorCodepoint(codepoint.value)
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

    void registerSystemFontFallbacks()
    {
        // These aliases are resolved from the native system-font enumeration;
        // this backend does not assume any platform-specific font directory.
        const std::vector<wsc::FontFace> faces = wsc::FontSystem::defaultSystemFontFaces();
        if (faces.empty()) {
            diagnostics_.push_back({wsc::text::TextBackendDiagnostic::Severity::Info,
                                    "No file-backed default system font aliases were discovered; a platform provider may still resolve fonts."});
        }

        for (const wsc::FontFace &face : faces) {
            systemFontManager_->registerFace(face);
        }

        const wsc::FontFallbackChain defaultChain = wsc::FontSystem::defaultFallbackChain();
        fontResolver_.setFallbackChain(defaultChain);
    }

    const wsc::FontFace *resolveRasterFace(
        const std::vector<std::uint32_t> &codepoints, const Paint &paint,
        bool acceptDeclaredRanges = false,
        const std::vector<std::uint32_t> *coverageCodepoints = nullptr) const
    {
        wsc::FontMatchRequest request;
        request.family = paint.hasFontFamily()
            ? paint.getFontFamily()
            : wsc::FontSystem::kDefaultPrimaryFamily;
        request.weight = paint.getFontWeight();
        request.slant = paint.getFontSlant();
        request.locale = paint.getTextLocale();
        request.codepoints = codepoints;
        const std::vector<std::uint32_t> &required = coverageCodepoints == nullptr
            ? codepoints : *coverageCodepoints;
        const wsc::FontMatchResult match = fontResolver_.resolve(
            request, [&](const wsc::FontFace &face,
                         const std::vector<std::uint32_t> &) {
                return std::all_of(required.begin(), required.end(),
                                   [&](std::uint32_t codepoint) {
                    return (acceptDeclaredRanges && face.hasCodepointRanges()
                            && face.supportsCodepoint(codepoint))
                        || rasterizer_.hasGlyph(face, codepoint);
                });
            });
        return match.face;
    }

    const wsc::FontFace *findRasterFaceForCodepoint(std::uint32_t codepoint, const Paint &paint) const
    {
        const std::string cacheKey = rasterFaceCacheKey(codepoint, paint);
        if (const auto cached = rasterFaceCache_.find(cacheKey); cached != rasterFaceCache_.end()) {
            return cached->second;
        }
        // Resolve the requested family's primary face once per style. Most UI
        // strings stay on that face, so checking its already-loaded cmap is
        // far cheaper than asking every platform provider to rematch each new
        // codepoint. Full fallback still runs whenever the primary lacks it.
        if (const wsc::FontFace *primary = findPrimaryRasterFace(paint);
            primary != nullptr && rasterizer_.hasGlyph(*primary, codepoint)) {
            rasterFaceCache_.emplace(cacheKey, primary);
            return primary;
        }
        if (const wsc::FontFace *face = resolveRasterFace({codepoint}, paint)) {
            rasterFaceCache_.emplace(cacheKey, face);
            return face;
        }
        return nullptr;
    }

    const wsc::FontFace *findRasterFaceForCluster(const std::vector<std::uint32_t> &codepoints,
                                                   const Paint &paint) const
    {
        std::vector<std::uint32_t> required;
        required.reserve(codepoints.size());
        for (std::uint32_t codepoint : codepoints) {
            if (codepoint < 32 || codepoint == 0x200D
                || wsc::text::isVariationSelectorCodepoint(codepoint)
                || wsc::text::isBidiControlCodepoint(codepoint)
                || wsc::text::isZeroWidthBreakCodepoint(codepoint)) {
                continue;
            }
            required.push_back(codepoint);
        }
        if (required.empty()) {
            return nullptr;
        }

        // The common cluster is one ordinary codepoint. Route it through the
        // existing face cache instead of invoking the platform matcher for
        // every occurrence in every label. Keep variation-selector/ZWJ
        // clusters intact because Android may choose a different emoji face.
        if (codepoints.size() == 1u && required.size() == 1u) {
            return findRasterFaceForCodepoint(required.front(), paint);
        }

        const std::string clusterCacheKey =
            rasterClusterFaceCacheKey(codepoints, paint);
        if (const auto cached = rasterFaceCache_.find(clusterCacheKey);
            cached != rasterFaceCache_.end()) {
            return cached->second;
        }

        if (const wsc::FontFace *primary = findPrimaryRasterFace(paint);
            primary != nullptr
            && std::all_of(required.begin(), required.end(),
                           [&](std::uint32_t codepoint) {
                               return rasterizer_.hasGlyph(*primary, codepoint);
                           })) {
            rasterFaceCache_.emplace(clusterCacheKey, primary);
            return primary;
        }

        if (const wsc::FontFace *face = resolveRasterFace(
                codepoints, paint, false, &required)) {
            rasterFaceCache_.emplace(clusterCacheKey, face);
            return face;
        }

        // Keep an indivisible grapheme on one face even when no installed face
        // covers every mark. This is preferable to splitting a combining/ZWJ
        // sequence across unrelated fonts; the shaper can then emit .notdef in
        // a deterministic way for the missing part.
        return findRasterFaceForCodepoint(required.front(), paint);
    }

    const wsc::FontFace *findPrimaryRasterFace(const Paint &paint) const
    {
        const std::string cacheKey =
            "primary\x1f" + rasterFaceCacheKey(0u, paint);
        if (const auto cached = rasterFaceCache_.find(cacheKey);
            cached != rasterFaceCache_.end()) {
            return cached->second;
        }

        wsc::FontMatchRequest request;
        request.family = paint.hasFontFamily()
            ? paint.getFontFamily()
            : wsc::FontSystem::kDefaultPrimaryFamily;
        request.weight = paint.getFontWeight();
        request.slant = paint.getFontSlant();
        request.locale = paint.getTextLocale();
        request.allowFallback = false;
        const wsc::FontMatchResult match = fontResolver_.resolve(request);
        rasterFaceCache_.emplace(cacheKey, match.face);
        return match.face;
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

        for (const wsc::text::FontFallbackCluster &cluster :
             wsc::text::buildFontFallbackClusters(normalizedText, sourceStart, sourceEnd)) {
            const wsc::FontFace *face = findRasterFaceForCluster(cluster.codepoints, paint);
            if (face == nullptr && std::all_of(cluster.codepoints.begin(), cluster.codepoints.end(),
                    [](std::uint32_t codepoint) {
                        return codepoint < 32 || codepoint == 0x200D
                            || wsc::text::isBidiControlCodepoint(codepoint)
                            || wsc::text::isZeroWidthBreakCodepoint(codepoint);
                    })) {
                continue;
            }
            if (face == nullptr) {
                const auto missing = std::find_if(
                    cluster.codepoints.begin(), cluster.codepoints.end(),
                    [](std::uint32_t codepoint) {
                        return codepoint >= 32 && codepoint != 0x200D
                            && !wsc::text::isVariationSelectorCodepoint(codepoint)
                            && !wsc::text::isBidiControlCodepoint(codepoint)
                            && !wsc::text::isZeroWidthBreakCodepoint(codepoint);
                    });
                const std::uint32_t missingCodepoint = missing == cluster.codepoints.end()
                    ? 0u : *missing;
                addDiagnosticOnce(
                    wsc::text::TextBackendDiagnostic::Severity::Warning,
                    "raster-cluster-face#" + diagnosticFontFamily(paint) + "#"
                        + std::to_string(missingCodepoint),
                    "No font face covers the complete grapheme cluster.",
                    missingCodepoint, diagnosticFontFamily(paint));
                return std::nullopt;
            }
            if (face != currentFace) {
                finishCurrent();
                currentFace = face;
                currentStart = cluster.sourceStart;
            }
            currentEnd = cluster.sourceEnd;
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
                                                         const Paint &paint,
                                                         bool allowLayoutView) const
    {
        const auto layoutCacheStart = CpuClock::now();
        const std::string layoutCacheKey =
            rasterLayoutCacheKey(normalizedText, paint);
        const std::uint64_t atlasGeneration =
            glyphAtlas_.stats().generation;
        if (const auto *cached = rasterLayoutCache_.find(layoutCacheKey);
            cached != nullptr
            && cached->atlasGeneration == atlasGeneration) {
            ++renderStats_.layoutCacheHits;
            TextRenderResult result;
            result.kind = TextRenderKind::GlyphAtlas;
            result.drawX = x + cached->drawXOffset;
            result.drawY = y + cached->drawYOffset;
            result.width = cached->width;
            result.height = cached->height;
            if (allowLayoutView) {
                result.glyphAtlasQuadsView = &cached->quads;
                result.glyphAtlasQuadOffsetX = x;
                result.glyphAtlasQuadOffsetY = y;
                ++renderStats_.layoutViewHits;
            } else {
                result.glyphAtlasQuads = cached->quads;
                for (TextRenderResult::GlyphAtlasQuad &quad :
                     result.glyphAtlasQuads) {
                    quad.x += x;
                    quad.y += y;
                }
            }
            renderStats_.generatedQuadCount +=
                cached->quads.size();
            populateAtlasResult(result);
            renderStats_.layoutCacheCpuTimeNs +=
                elapsedCpuTimeNs(layoutCacheStart);
            return result;
        }
        renderStats_.layoutCacheCpuTimeNs +=
            elapsedCpuTimeNs(layoutCacheStart);
        ++renderStats_.layoutCacheMisses;

        const auto shapingStart = CpuClock::now();
        const auto shapedRun = shapeRasterizedText(normalizedText, paint);
        renderStats_.shapingCpuTimeNs +=
            elapsedCpuTimeNs(shapingStart);
        if (!shapedRun) {
            addDiagnosticOnce(wsc::text::TextBackendDiagnostic::Severity::Warning,
                              "raster-shape#" + diagnosticFontFamily(paint),
                              "Raster text shaping failed; falling back to alternate text path.",
                              0u,
                              diagnosticFontFamily(paint));
            return std::nullopt;
        }

        float drawXOffset = 0.0f;
        if (paint.getTextAlign() == Paint::TextAlign::CENTER) {
            drawXOffset -= shapedRun->width * 0.5f;
        } else if (paint.getTextAlign() == Paint::TextAlign::RIGHT) {
            drawXOffset -= shapedRun->width;
        }
        const float drawYOffset =
            wsc::text::textBaselineOffset(
                paint.getTextBaseline(), paint.getTextSize());

        TextRenderResult result;
        result.kind = TextRenderKind::GlyphAtlas;
        result.drawX = x + drawXOffset;
        result.drawY = y + drawYOffset;
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
            const auto glyphLookupStart = CpuClock::now();
            const wsc::FontFace *face = glyph.fontFace != nullptr
                ? glyph.fontFace
                : findRasterFaceForCodepoint(glyph.codepoint, paint);
            if (face == nullptr) {
                addDiagnosticOnce(wsc::text::TextBackendDiagnostic::Severity::Warning,
                                  "raster-face#" + diagnosticFontFamily(paint) + "#"
                                      + std::to_string(glyph.codepoint),
                                  "No raster font face resolved for shaped glyph.",
                                  glyph.codepoint,
                                  diagnosticFontFamily(paint));
                return std::nullopt;
            }
            const wsc::FontFace effectiveFace =
                applyPaintFontVariations(*face, paint);

            // Most UI text is stable across frames. Consult the atlas before
            // asking FreeType to rasterize a glyph again; the atlas already
            // owns both the bitmap and its metrics for the common alpha case.
            wsc::text::GlyphKey cachedKey;
            cachedKey.fontFamily = effectiveFace.family();
            cachedKey.codepoint = glyph.codepoint;
            cachedKey.glyphIndex = glyph.glyphIndex;
            cachedKey.pixelSize = paint.getTextSize();
            cachedKey.format = wsc::text::GlyphBitmapFormat::Alpha;
            cachedKey.weight = effectiveFace.weight();
            cachedKey.slant = effectiveFace.slant();
            cachedKey.faceIndex = effectiveFace.faceIndex();
            cachedKey.fontIdentity = wsc::text::fontFaceIdentity(effectiveFace);
            const wsc::text::GlyphAtlasEntry *cached = glyphAtlas_.find(cachedKey);
            if (cached == nullptr) {
                cachedKey.format = wsc::text::GlyphBitmapFormat::RGBA;
                cached = glyphAtlas_.find(cachedKey);
            }
            if (cached != nullptr) {
                renderStats_.glyphCacheLookupCpuTimeNs +=
                    elapsedCpuTimeNs(glyphLookupStart);
                ++renderStats_.atlasHits;
                if (cached->width == 0 && cached->height == 0) {
                    ++renderStats_.zeroAreaGlyphHits;
                }
                pendingGlyphs.push_back(
                    {glyph, cached->key, std::nullopt, *cached});
                continue;
            }
            renderStats_.glyphCacheLookupCpuTimeNs +=
                elapsedCpuTimeNs(glyphLookupStart);

            ++renderStats_.atlasMisses;
            ++renderStats_.rasterizationCount;

            const auto rasterStart = CpuClock::now();
            auto rasterized = glyph.glyphIndex > 0
                ? rasterizer_.rasterizeGlyphIndex(effectiveFace, glyph.glyphIndex, glyph.codepoint, paint.getTextSize())
                : rasterizer_.rasterizeGlyph(effectiveFace, glyph.codepoint, paint.getTextSize());
            renderStats_.glyphRasterCpuTimeNs +=
                elapsedCpuTimeNs(rasterStart);
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

        const float baselineY = drawYOffset + paint.getTextSize();
        const float spacing = std::isfinite(paint.getLetterSpacing()) ? paint.getLetterSpacing() : 0.0f;
        bool uploadedConsistentGeneration = false;
        for (int attempt = 0; attempt < 3 && !uploadedConsistentGeneration; ++attempt) {
            result.glyphAtlasQuads.clear();
            float penX = drawXOffset;
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
                        : [&]() {
                            const auto uploadStart = CpuClock::now();
                            auto uploaded = glyphAtlas_.uploadGlyph(
                                pending.key, *pending.bitmap);
                            renderStats_.atlasUploadCpuTimeNs +=
                                elapsedCpuTimeNs(uploadStart);
                            return uploaded;
                        }();
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
                        quad.isColorGlyph =
                            pending.key.format == wsc::text::GlyphBitmapFormat::RGBA;
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
        cachedLayout.drawXOffset = drawXOffset;
        cachedLayout.drawYOffset = drawYOffset;
        cachedLayout.width = result.width;
        cachedLayout.height = result.height;
        cachedLayout.quads = result.glyphAtlasQuads;
        for (TextRenderResult::GlyphAtlasQuad &quad :
             result.glyphAtlasQuads) {
            quad.x += x;
            quad.y += y;
        }
        rasterLayoutCache_.insert(
            layoutCacheKey, std::move(cachedLayout));
        renderStats_.generatedQuadCount +=
            result.glyphAtlasQuads.size();
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
        const std::size_t bytesPerPixel =
            glyphAtlas_.hasColorPixels() ? 4u : 1u;
        result.atlasDirtyRects.reserve(dirtyRects.size());
        for (const wsc::text::GlyphAtlasDirtyRect &dirtyRect : dirtyRects) {
            renderStats_.atlasDirtyBytes +=
                static_cast<std::size_t>(std::max(0, dirtyRect.width))
                * static_cast<std::size_t>(std::max(0, dirtyRect.height))
                * bytesPerPixel;
            TextRenderResult::GlyphAtlasDirtyRect resultRect;
            resultRect.x = dirtyRect.x;
            resultRect.y = dirtyRect.y;
            resultRect.width = dirtyRect.width;
            resultRect.height = dirtyRect.height;
            result.atlasDirtyRects.push_back(resultRect);
        }
    }

    const wsc::text::ShapedTextRun *shapeRasterizedText(
        const std::string &normalizedText,
        const Paint &paint) const
    {
        if (!paint.hasFontFamily()) {
            if (!fontResolver_.hasFamily(wsc::FontSystem::kDefaultPrimaryFamily)) {
                return nullptr;
            }
        }

        if (!shaper_) {
            return nullptr;
        }

        const std::string cacheKey = rasterShapeCacheKey(normalizedText, paint);
        if (const auto *cached = rasterShapeCache_.find(cacheKey);
            cached != nullptr) {
            ++renderStats_.shapeCacheHits;
            return cached;
        }
        ++renderStats_.shapeCacheMisses;

        const auto bidiStart = CpuClock::now();
        std::vector<wsc::text::BidiRun> bidiRuns = wsc::text::segmentBidiRuns(normalizedText);
        renderStats_.bidiCpuTimeNs += elapsedCpuTimeNs(bidiStart);
        if (bidiRuns.empty()) {
            return nullptr;
        }

        wsc::text::ShapedTextRun combined;
        bool hasGlyph = false;
        const float spacing = std::isfinite(paint.getLetterSpacing()) ? paint.getLetterSpacing() : 0.0f;

        for (const wsc::text::BidiRun &bidiRun : bidiRuns) {
            const auto fallbackStart = CpuClock::now();
            const auto segments = buildRasterTextSegments(normalizedText, paint, bidiRun.sourceStart, bidiRun.sourceEnd);
            renderStats_.fontFallbackCpuTimeNs +=
                elapsedCpuTimeNs(fallbackStart);
            if (!segments) {
                return nullptr;
            }

            for (const RasterTextSegment &segment : *segments) {
                if (segment.face == nullptr || segment.sourceEnd <= segment.sourceStart
                    || segment.sourceEnd > normalizedText.size()) {
                    return nullptr;
                }

                const std::string segmentText = normalizedText.substr(segment.sourceStart,
                                                                      segment.sourceEnd - segment.sourceStart);
                const wsc::FontFace effectiveFace =
                    applyPaintFontVariations(*segment.face, paint);
                wsc::text::TextShapeInput input;
                input.normalizedText = segmentText;
                input.letterSpacing = paint.getLetterSpacing();
                input.pixelSize = paint.getTextSize();
                input.language = paint.getTextLocale();
                input.direction = bidiRun.rightToLeft
                    ? wsc::text::TextDirection::RightToLeft
                    : wsc::text::TextDirection::LeftToRight;
                input.openTypeFeatures.reserve(paint.getFontFeatures().size());
                for (const Paint::FontFeature &feature : paint.getFontFeatures()) {
                    input.openTypeFeatures.push_back({feature.tag, feature.value});
                }
                input.variationCoordinates = effectiveFace.variationCoordinates();
                const auto fontDataStart = CpuClock::now();
                input.fontData = rasterizer_.fontData(effectiveFace);
                renderStats_.fontDataCpuTimeNs +=
                    elapsedCpuTimeNs(fontDataStart);

                const auto segmentCodepoints =
                    wsc::text::decodeUtf8(segmentText);
                const std::uint32_t diagnosticCodepoint = segmentCodepoints.empty()
                    ? 0u : segmentCodepoints.front().value;
                if (!input.fontData) {
                    addDiagnosticOnce(
                        wsc::text::TextBackendDiagnostic::Severity::Warning,
                        "raster-font-data#" + wsc::text::fontFaceIdentity(effectiveFace),
                        "Resolved font bytes could not be loaded for shaping.",
                        diagnosticCodepoint, effectiveFace.family());
                    return nullptr;
                }

                const auto resolver = [&](std::uint32_t codepoint) -> std::optional<wsc::text::ResolvedGlyph> {
                    const auto metrics = rasterizer_.glyphMetrics(effectiveFace, codepoint, paint.getTextSize());
                    if (!metrics) {
                        return std::nullopt;
                    }
                    return wsc::text::ResolvedGlyph{metrics->glyphIndex, metrics->advanceX};
                };

                const auto shapeEngineStart = CpuClock::now();
                auto shaped = shaper_->shape(input, resolver);
                renderStats_.shapeEngineCpuTimeNs +=
                    elapsedCpuTimeNs(shapeEngineStart);
                if (!shaped && shaper_->supportsOpenTypeFeatures()) {
                    shaped = wsc::text::shapeTextSimple(segmentText, paint.getLetterSpacing(), resolver);
                }
                if (!shaped) {
                    addDiagnosticOnce(
                        wsc::text::TextBackendDiagnostic::Severity::Warning,
                        "raster-segment-shape#"
                            + wsc::text::fontFaceIdentity(effectiveFace) + "#"
                            + std::to_string(diagnosticCodepoint),
                        "Resolved font segment failed OpenType and simple shaping. source="
                            + effectiveFace.path() + " faceIndex="
                            + std::to_string(effectiveFace.faceIndex()),
                        diagnosticCodepoint, effectiveFace.family());
                    return nullptr;
                }

                if (!shaper_->supportsOpenTypeFeatures()) {
                    for (std::size_t index = 0; index + 1 < shaped->glyphs.size(); ++index) {
                        const auto kerning = rasterizer_.glyphKerning(effectiveFace,
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
                    glyph.fontFace = segment.face;
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
            return nullptr;
        }
        combined.width = std::max(0.0f, combined.width);
        return &rasterShapeCache_.insert(
            cacheKey, std::move(combined));
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
#if defined(__ANDROID__)
        __android_log_print(
            severity == wsc::text::TextBackendDiagnostic::Severity::Error
                ? ANDROID_LOG_ERROR : ANDROID_LOG_WARN,
            "WhatsCanvas", "%s family=%s codepoint=U+%04X",
            message.c_str(), family.c_str(), codepoint);
#endif
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

    std::string rasterShapeCacheKey(const std::string &text, const Paint &paint) const
    {
        const std::string resolutionFamily = paint.hasFontFamily()
            ? paint.getFontFamily() : wsc::FontSystem::kDefaultPrimaryFamily;
        std::string key = text + '\x1f' + paint.getFontFamily() + '\x1f' + std::to_string(paint.getTextSize()) + '\x1f'
               + std::to_string(paint.getLetterSpacing()) + '\x1f' + std::to_string(paint.getFontWeight()) + '\x1f'
               + std::to_string(static_cast<int>(paint.getFontSlant())) + '\x1f'
               + paint.getTextLocale() + '\x1f'
               + std::to_string(fontResolver_.resolutionGeneration(
                   resolutionFamily));
        for (const Paint::FontFeature &feature : paint.getFontFeatures()) {
            key += '\x1e' + feature.tag + '=' + std::to_string(feature.value);
        }
        appendPaintFontVariations(key, paint);
        return key;
    }

    std::string rasterLayoutCacheKey(
        const std::string &text, const Paint &paint)
        const
    {
        return rasterShapeCacheKey(text, paint) + '\x1f'
            + std::to_string(static_cast<int>(paint.getTextAlign()))
            + '\x1f'
            + std::to_string(static_cast<int>(paint.getTextBaseline()));
    }

    std::string rasterFaceCacheKey(std::uint32_t codepoint, const Paint &paint) const
    {
        const std::string resolutionFamily = paint.hasFontFamily()
            ? paint.getFontFamily() : wsc::FontSystem::kDefaultPrimaryFamily;
        return paint.getFontFamily() + '\x1f' + std::to_string(codepoint) + '\x1f'
               + std::to_string(paint.getFontWeight()) + '\x1f'
               + std::to_string(static_cast<int>(paint.getFontSlant())) + '\x1f'
               + paint.getTextLocale() + '\x1f'
               + std::to_string(fontResolver_.resolutionGeneration(
                   resolutionFamily));
    }

    std::string rasterClusterFaceCacheKey(
        const std::vector<std::uint32_t> &codepoints,
        const Paint &paint) const
    {
        std::string key = "cluster";
        for (std::uint32_t codepoint : codepoints) {
            key += '\x1e' + std::to_string(codepoint);
        }
        key += '\x1f' + rasterFaceCacheKey(0u, paint);
        return key;
    }

    void clearRasterCaches()
    {
        rasterShapeCache_.clear();
        rasterLayoutCache_.clear();
        rasterFaceCache_.clear();
    }

#ifdef _WIN32

    static std::string makeNativeCacheKey(const std::string &text, const Paint &paint)
    {
        std::string key = text + '\x1f' + paint.getFontFamily() + '\x1f' +
               std::to_string(paint.getTextSize()) + '\x1f' +
               std::to_string(paint.getLetterSpacing()) + '\x1f' +
               std::to_string(paint.getFontWeight()) + '\x1f' +
               std::to_string(static_cast<int>(paint.getFontSlant())) + '\x1f' +
               paint.getTextLocale();
        for (const Paint::FontFeature &feature : paint.getFontFeatures()) {
            key += '\x1e' + feature.tag + '=' + std::to_string(feature.value);
        }
        appendPaintFontVariations(key, paint);
        return key;
    }

    wsc::text::NativeTextMeasure getNativeMeasure(const std::string &text, const Paint &paint) const
    {
        const std::string cacheKey = makeNativeCacheKey(text, paint);
        if (const auto *cached = nativeMeasureCache_.find(cacheKey);
            cached != nullptr) {
            return *cached;
        }

        const auto measure = wsc::text::measureNativeText(text, paint);
        nativeMeasureCache_.insert(cacheKey, measure);
        return measure;
    }

    wsc::text::NativeTextBitmap getNativeBitmap(const std::string &text, const Paint &paint,
                                                        const wsc::text::NativeTextMeasure &measure) const
    {
        const std::string cacheKey = makeNativeCacheKey(text, paint);
        if (const auto *cached = nativeBitmapCache_.find(cacheKey);
            cached != nullptr) {
            return *cached;
        }

        const auto bitmap = wsc::text::renderNativeTextBitmap(text, paint, measure);
        nativeBitmapCache_.insert(cacheKey, bitmap);
        return bitmap;
    }

    mutable wsc::render::LruCache<
        wsc::text::NativeTextMeasure, std::string>
        nativeMeasureCache_{kMaxNativeTextCacheEntries};
    mutable wsc::render::LruCache<
        wsc::text::NativeTextBitmap, std::string>
        nativeBitmapCache_{kMaxNativeTextCacheEntries};
#endif
    std::shared_ptr<wsc::FontManager> dynamicFontManager_ =
        std::make_shared<wsc::FontManager>();
    std::shared_ptr<wsc::FontManager> systemFontManager_ =
        std::make_shared<wsc::FontManager>();
    std::shared_ptr<wsc::FontProvider> platformSystemFontProvider_;
    wsc::FontResolver fontResolver_;
    std::unordered_map<std::string, wsc::FontFallbackChain> userFallbackChains_;
    mutable wsc::text::FontRasterizer rasterizer_;
    mutable wsc::text::GlyphAtlas glyphAtlas_{kDefaultGlyphAtlasSize, kDefaultGlyphAtlasSize, 1};
    std::unique_ptr<wsc::text::ITextBackend> directWriteBackend_;
    std::unique_ptr<wsc::text::ITextBackend> coreTextBackend_;
    mutable wsc::render::LruCache<
        wsc::text::ShapedTextRun, std::string>
        rasterShapeCache_{kMaxRasterShapeCacheEntries};
    mutable wsc::render::LruCache<
        RasterGlyphLayout, std::string>
        rasterLayoutCache_{kMaxRasterLayoutCacheEntries};
    mutable std::unordered_map<std::string, const wsc::FontFace *> rasterFaceCache_;
    mutable std::vector<wsc::text::TextBackendDiagnostic> diagnostics_;
    mutable std::unordered_set<std::string> diagnosticKeys_;
    mutable std::unordered_set<std::string> missingGlyphDiagnosticKeys_;
    mutable wsc::text::TextRenderStats renderStats_;
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
                            wsc::text::isCoreTextAvailable(),
                            true,
                            true,
                            false,
                            false,
                            true});
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

bool isNativeCoreTextActive(const ITextBackend *backend)
{
    const auto *basic = dynamic_cast<const BasicTextBackend *>(backend);
    return basic != nullptr && basic->hasNativeCoreTextBackend();
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

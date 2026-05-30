#include "text/BasicTextBackend.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>

#include "canvas/Paint.h"
#include "text/ITextBackend.h"
#include "text/NativeText.h"
#include "text/TextUtils.h"

namespace {

using prismcanvas::text::TextRenderKind;
using prismcanvas::text::TextRenderResult;

constexpr size_t kMaxNativeTextCacheEntries = 128;

class BasicTextBackend final : public prismcanvas::text::ITextBackend
{
public:
    float measureTextWidth(const std::string &text, const Paint &paint) const override
    {
        const std::string asciiText = prismcanvas::text::sanitizeTextToAscii(text);
        if (asciiText.empty() || paint.getTextSize() <= 0.0f) {
            return 0.0f;
        }

#ifdef _WIN32
        if (paint.hasFontFamily()) {
            const auto nativeMeasure = getNativeMeasure(asciiText, paint);
            if (nativeMeasure.valid) {
                return nativeMeasure.width;
            }
        }
#endif

        constexpr float kTextBaseSize = 8.0f;
        const float textScale = std::max(0.01f, paint.getTextSize() / kTextBaseSize);
        return prismcanvas::text::measureAsciiTextWidth(asciiText, textScale, paint.getLetterSpacing());
    }

    RectF measureTextBounds(const std::string &text, const Paint &paint) const override
    {
        const std::string asciiText = prismcanvas::text::sanitizeTextToAscii(text);
        if (asciiText.empty() || paint.getTextSize() <= 0.0f) {
            return RectF();
        }

#ifdef _WIN32
        if (paint.hasFontFamily()) {
            const auto nativeMeasure = getNativeMeasure(asciiText, paint);
            if (nativeMeasure.valid) {
                float left = 0.0f;
                if (paint.getTextAlign() == Paint::TextAlign::CENTER) {
                    left = -nativeMeasure.width * 0.5f;
                } else if (paint.getTextAlign() == Paint::TextAlign::RIGHT) {
                    left = -nativeMeasure.width;
                }
                return RectF(left,
                             prismcanvas::text::textBaselineOffset(paint.getTextBaseline(), nativeMeasure.height),
                             nativeMeasure.width,
                             nativeMeasure.height);
            }
        }
#endif

        constexpr float kTextBaseSize = 8.0f;
        const float textScale = std::max(0.01f, paint.getTextSize() / kTextBaseSize);
        const float width = measureTextWidth(asciiText, paint);
        const float height = prismcanvas::text::measureAsciiTextHeight(asciiText, textScale);

        float left = 0.0f;
        if (paint.getTextAlign() == Paint::TextAlign::CENTER) {
            left = -width * 0.5f;
        } else if (paint.getTextAlign() == Paint::TextAlign::RIGHT) {
            left = -width;
        }

        return RectF(left,
                     prismcanvas::text::textBaselineOffset(paint.getTextBaseline(), height),
                     width,
                     height);
    }

    TextRenderResult renderText(const std::string &text, float x, float y, const Paint &paint) const override
    {
        TextRenderResult result;
        const std::string asciiText = prismcanvas::text::sanitizeTextToAscii(text);
        if (asciiText.empty() || paint.getTextSize() <= 0.0f) {
            return result;
        }

#ifdef _WIN32
        if (paint.hasFontFamily()) {
            const auto nativeMeasure = getNativeMeasure(asciiText, paint);
            if (nativeMeasure.valid) {
                const auto bitmap = getNativeBitmap(asciiText, paint, nativeMeasure);
                if (!bitmap.pixels.empty()) {
                    float alignedX = x;
                    if (paint.getTextAlign() == Paint::TextAlign::CENTER) {
                        alignedX -= nativeMeasure.width * 0.5f;
                    } else if (paint.getTextAlign() == Paint::TextAlign::RIGHT) {
                        alignedX -= nativeMeasure.width;
                    }

                    result.kind = TextRenderKind::Bitmap;
                    result.drawX = alignedX;
                    result.drawY = y + prismcanvas::text::textBaselineOffset(paint.getTextBaseline(), nativeMeasure.height);
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

        constexpr float kTextBaseSize = 8.0f;
        const float textScale = std::max(0.01f, paint.getTextSize() / kTextBaseSize);
        const float textHeight = prismcanvas::text::measureAsciiTextHeight(asciiText, textScale);
        const float textWidth = measureTextWidth(asciiText, paint);
        float alignedX = x;
        if (paint.getTextAlign() == Paint::TextAlign::CENTER) {
            alignedX -= textWidth * 0.5f;
        } else if (paint.getTextAlign() == Paint::TextAlign::RIGHT) {
            alignedX -= textWidth;
        }

        result.kind = TextRenderKind::Geometry;
        result.drawX = alignedX;
        result.drawY = y + prismcanvas::text::textBaselineOffset(paint.getTextBaseline(), textHeight);
        result.width = textWidth;
        result.height = textHeight;
        result.vertices = prismcanvas::text::buildTextVertices(asciiText, alignedX, result.drawY,
                                                               textScale, paint.getLetterSpacing());
        if (result.vertices.empty()) {
            result.kind = TextRenderKind::None;
        }
        return result;
    }

private:
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

    prismcanvas::text::NativeTextMeasure getNativeMeasure(const std::string &text, const Paint &paint) const
    {
        const std::string cacheKey = makeNativeCacheKey(text, paint);
        auto cached = nativeMeasureCache_.find(cacheKey);
        if (cached != nativeMeasureCache_.end()) {
            touchCacheEntry(cacheKey, nativeMeasureCache_, nativeMeasureCacheOrder_);
            return cached->second;
        }

        const auto measure = prismcanvas::text::measureNativeText(text, paint);
        nativeMeasureCache_[cacheKey] = measure;
        touchCacheEntry(cacheKey, nativeMeasureCache_, nativeMeasureCacheOrder_);
        return measure;
    }

    prismcanvas::text::NativeTextBitmap getNativeBitmap(const std::string &text, const Paint &paint,
                                                        const prismcanvas::text::NativeTextMeasure &measure) const
    {
        const std::string cacheKey = makeNativeCacheKey(text, paint);
        auto cached = nativeBitmapCache_.find(cacheKey);
        if (cached != nativeBitmapCache_.end()) {
            touchCacheEntry(cacheKey, nativeBitmapCache_, nativeBitmapCacheOrder_);
            return cached->second;
        }

        const auto bitmap = prismcanvas::text::renderNativeTextBitmap(text, paint, measure);
        nativeBitmapCache_[cacheKey] = bitmap;
        touchCacheEntry(cacheKey, nativeBitmapCache_, nativeBitmapCacheOrder_);
        return bitmap;
    }

    mutable std::unordered_map<std::string, prismcanvas::text::NativeTextMeasure> nativeMeasureCache_;
    mutable std::unordered_map<std::string, prismcanvas::text::NativeTextBitmap> nativeBitmapCache_;
    mutable std::deque<std::string> nativeMeasureCacheOrder_;
    mutable std::deque<std::string> nativeBitmapCacheOrder_;
#endif
};

} // namespace

namespace prismcanvas::text {

std::unique_ptr<ITextBackend> createBasicTextBackend()
{
    return std::make_unique<BasicTextBackend>();
}

} // namespace prismcanvas::text
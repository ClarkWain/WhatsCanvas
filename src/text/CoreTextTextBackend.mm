#include <wsc/FontSystem.h>

#include "text/CoreTextTextBackend.h"

#if defined(__APPLE__)

#include "canvas/Paint.h"
#include "text/TextUtils.h"
#include "wsc/Font.h"
#include "wsc/FontResolver.h"

#include <CoreGraphics/CoreGraphics.h>
#include <CoreText/CoreText.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace wsc::text {
namespace {

template <typename T>
class CFRef
{
public:
    CFRef() = default;
    explicit CFRef(T value) : value_(value) {}
    ~CFRef()
    {
        if (value_ != nullptr) CFRelease(value_);
    }
    CFRef(const CFRef &) = delete;
    CFRef &operator=(const CFRef &) = delete;
    CFRef(CFRef &&other) noexcept : value_(other.value_)
    {
        other.value_ = nullptr;
    }
    CFRef &operator=(CFRef &&other) noexcept
    {
        if (this == &other) return *this;
        if (value_ != nullptr) CFRelease(value_);
        value_ = other.value_;
        other.value_ = nullptr;
        return *this;
    }
    T get() const { return value_; }
    explicit operator bool() const { return value_ != nullptr; }

private:
    T value_ = nullptr;
};

CFRef<CFStringRef> makeString(const std::string &value)
{
    return CFRef<CFStringRef>(CFStringCreateWithBytes(
        kCFAllocatorDefault,
        reinterpret_cast<const UInt8 *>(value.data()),
        static_cast<CFIndex>(value.size()), kCFStringEncodingUTF8, false));
}

float normalizedWeight(int cssWeight)
{
    const float weight = static_cast<float>(std::clamp(cssWeight, 1, 1000));
    return std::clamp((weight - 400.0f) / (weight < 400.0f ? 400.0f : 500.0f),
                      -1.0f, 1.0f);
}

std::uint32_t openTypeTag(const std::string &tag)
{
    if (tag.size() != 4) return 0;
    return (static_cast<std::uint32_t>(static_cast<unsigned char>(tag[0])) << 24u)
         | (static_cast<std::uint32_t>(static_cast<unsigned char>(tag[1])) << 16u)
         | (static_cast<std::uint32_t>(static_cast<unsigned char>(tag[2])) << 8u)
         | static_cast<std::uint32_t>(static_cast<unsigned char>(tag[3]));
}

CFRef<CTFontRef> createFont(const Paint &paint,
                            CTFontDescriptorRef registeredDescriptor)
{
    CFRef<CFMutableDictionaryRef> attributes(
        CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
                                  &kCFTypeDictionaryKeyCallBacks,
                                  &kCFTypeDictionaryValueCallBacks));
    if (!attributes) return {};

    if (paint.hasFontFamily()) {
        auto family = makeString(paint.getFontFamily());
        if (family) {
            CFDictionarySetValue(attributes.get(), kCTFontFamilyNameAttribute,
                                 family.get());
        }
    }

    CFRef<CFMutableDictionaryRef> traits(
        CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
                                  &kCFTypeDictionaryKeyCallBacks,
                                  &kCFTypeDictionaryValueCallBacks));
    const float weight = normalizedWeight(paint.getFontWeight());
    CFRef<CFNumberRef> weightNumber(CFNumberCreate(
        kCFAllocatorDefault, kCFNumberFloatType, &weight));
    if (traits && weightNumber) {
        CFDictionarySetValue(traits.get(), kCTFontWeightTrait, weightNumber.get());
    }
    CTFontSymbolicTraits symbolic = 0;
    if (paint.getFontSlant() != FontSlant::NORMAL) {
        symbolic |= kCTFontItalicTrait;
    }
    if (paint.getFontWeight() >= 600) {
        symbolic |= kCTFontBoldTrait;
    }
    CFRef<CFNumberRef> symbolicNumber(CFNumberCreate(
        kCFAllocatorDefault, kCFNumberSInt32Type, &symbolic));
    if (traits && symbolicNumber) {
        CFDictionarySetValue(traits.get(), kCTFontSymbolicTrait,
                             symbolicNumber.get());
    }
    if (traits) {
        CFDictionarySetValue(attributes.get(), kCTFontTraitsAttribute, traits.get());
    }

    if (!paint.getFontVariations().empty()) {
        CFRef<CFMutableDictionaryRef> variations(
            CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
                                      &kCFTypeDictionaryKeyCallBacks,
                                      &kCFTypeDictionaryValueCallBacks));
        for (const FontVariationCoordinate &coordinate : paint.getFontVariations()) {
            const std::uint32_t tag = openTypeTag(coordinate.tag);
            if (tag == 0 || !std::isfinite(coordinate.value)) continue;
            CFRef<CFNumberRef> key(CFNumberCreate(
                kCFAllocatorDefault, kCFNumberSInt32Type, &tag));
            CFRef<CFNumberRef> value(CFNumberCreate(
                kCFAllocatorDefault, kCFNumberFloatType, &coordinate.value));
            if (key && value) {
                CFDictionarySetValue(variations.get(), key.get(), value.get());
            }
        }
        if (variations && CFDictionaryGetCount(variations.get()) > 0) {
            CFDictionarySetValue(attributes.get(), kCTFontVariationAttribute,
                                 variations.get());
        }
    }

    if (!paint.getFontFeatures().empty()) {
        CFRef<CFMutableArrayRef> features(CFArrayCreateMutable(
            kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks));
        for (const Paint::FontFeature &feature : paint.getFontFeatures()) {
            const std::uint32_t tag = openTypeTag(feature.tag);
            if (tag == 0) continue;
            CFRef<CFNumberRef> tagNumber(CFNumberCreate(
                kCFAllocatorDefault, kCFNumberSInt32Type, &tag));
            CFRef<CFNumberRef> valueNumber(CFNumberCreate(
                kCFAllocatorDefault, kCFNumberSInt32Type, &feature.value));
            const void *keys[] = {kCTFontOpenTypeFeatureTag,
                                  kCTFontOpenTypeFeatureValue};
            const void *values[] = {tagNumber.get(), valueNumber.get()};
            CFRef<CFDictionaryRef> setting(CFDictionaryCreate(
                kCFAllocatorDefault, keys, values, 2,
                &kCFTypeDictionaryKeyCallBacks,
                &kCFTypeDictionaryValueCallBacks));
            if (setting) CFArrayAppendValue(features.get(), setting.get());
        }
        if (features && CFArrayGetCount(features.get()) > 0) {
            CFDictionarySetValue(attributes.get(), kCTFontFeatureSettingsAttribute,
                                 features.get());
        }
    }

    CFRef<CTFontDescriptorRef> descriptor(
        registeredDescriptor != nullptr
            ? CTFontDescriptorCreateCopyWithAttributes(
                  registeredDescriptor, attributes.get())
            : CTFontDescriptorCreateWithAttributes(attributes.get()));
    CTFontRef font = descriptor
        ? CTFontCreateWithFontDescriptor(descriptor.get(), paint.getTextSize(), nullptr)
        : nullptr;
    if (font == nullptr) {
        font = CTFontCreateUIFontForLanguage(kCTFontUIFontSystem,
                                             paint.getTextSize(), nullptr);
    }
    return CFRef<CTFontRef>(font);
}

CFRef<CFAttributedStringRef> createAttributedString(const std::string &text,
                                                     const Paint &paint,
                                                     CTFontDescriptorRef descriptor)
{
    auto string = makeString(text);
    auto font = createFont(paint, descriptor);
    if (!string || !font) return {};

    CFRef<CFMutableDictionaryRef> attributes(
        CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
                                  &kCFTypeDictionaryKeyCallBacks,
                                  &kCFTypeDictionaryValueCallBacks));
    CFDictionarySetValue(attributes.get(), kCTFontAttributeName, font.get());
    CFDictionarySetValue(attributes.get(), kCTForegroundColorFromContextAttributeName,
                         kCFBooleanTrue);
    const float spacing = std::isfinite(paint.getLetterSpacing())
        ? paint.getLetterSpacing() : 0.0f;
    if (spacing != 0.0f) {
        CFRef<CFNumberRef> kern(CFNumberCreate(
            kCFAllocatorDefault, kCFNumberFloatType, &spacing));
        CFDictionarySetValue(attributes.get(), kCTKernAttributeName, kern.get());
    }
    if (paint.hasTextLocale()) {
        auto language = makeString(paint.getTextLocale());
        if (language) {
            CFDictionarySetValue(attributes.get(), kCTLanguageAttributeName,
                                 language.get());
        }
    }
    return CFRef<CFAttributedStringRef>(CFAttributedStringCreate(
        kCFAllocatorDefault, string.get(), attributes.get()));
}

struct LayoutInfo
{
    float width = 0.0f;
    float height = 0.0f;
    float ascent = 0.0f;
    float descent = 0.0f;
    float leading = 0.0f;
};

LayoutInfo measureAttributedString(CFAttributedStringRef attributed)
{
    LayoutInfo result;
    if (attributed == nullptr || CFAttributedStringGetLength(attributed) == 0) {
        return result;
    }
    CFRef<CTFramesetterRef> framesetter(
        CTFramesetterCreateWithAttributedString(attributed));
    if (!framesetter) return result;
    const CGSize constraint = CGSizeMake(1000000.0, 1000000.0);
    const CGSize size = CTFramesetterSuggestFrameSizeWithConstraints(
        framesetter.get(), CFRangeMake(0, 0), nullptr, constraint, nullptr);
    result.width = std::max(0.0f, static_cast<float>(size.width));
    result.height = std::max(0.0f, static_cast<float>(size.height));

    CFRef<CTLineRef> line(CTLineCreateWithAttributedString(attributed));
    if (line) {
        CGFloat ascent = 0.0;
        CGFloat descent = 0.0;
        CGFloat leading = 0.0;
        CTLineGetTypographicBounds(line.get(), &ascent, &descent, &leading);
        result.ascent = static_cast<float>(ascent);
        result.descent = static_cast<float>(descent);
        result.leading = static_cast<float>(leading);
        if (result.height <= 0.0f) {
            result.height = result.ascent + result.descent + result.leading;
        }
    }
    return result;
}

std::uint64_t hashPixels(const std::vector<unsigned char> &pixels,
                         int width, int height)
{
    std::uint64_t hash = 1469598103934665603ull;
    const auto append = [&](unsigned char byte, std::uint64_t &value) {
        value ^= byte;
        value *= 1099511628211ull;
    };
    for (int value : {width, height}) {
        for (int shift = 0; shift < 32; shift += 8) {
            append(static_cast<unsigned char>((value >> shift) & 0xff), hash);
        }
    }
    for (unsigned char byte : pixels) append(byte, hash);
    return hash == 0 ? 1 : hash;
}

std::string renderCacheKey(const std::string &text, const Paint &paint,
                           std::uint64_t generation)
{
    std::string key = std::to_string(generation) + '|' + text + '|'
        + std::to_string(paint.getTextSize()) + '|'
        + std::to_string(paint.getFontWeight()) + '|'
        + std::to_string(static_cast<int>(paint.getFontSlant())) + '|'
        + std::to_string(paint.getLetterSpacing()) + '|'
        + (paint.hasFontFamily() ? paint.getFontFamily() : std::string()) + '|'
        + (paint.hasTextLocale() ? paint.getTextLocale() : std::string());
    for (const auto &feature : paint.getFontFeatures()) {
        key += '|' + feature.tag + '=' + std::to_string(feature.value);
    }
    for (const auto &variation : paint.getFontVariations()) {
        key += '|' + variation.tag + '=' + std::to_string(variation.value);
    }
    key += paint.isUnderline() ? "|u" : "|-";
    key += paint.isStrikethrough() ? "|s" : "|-";
    return key;
}

std::vector<std::size_t> buildUtf16ToUtf8Map(const std::string &text,
                                             CFIndex utf16Length)
{
    std::vector<std::size_t> map(static_cast<std::size_t>(utf16Length) + 1,
                                 text.size());
    std::size_t unit = 0;
    for (const Utf8Codepoint &codepoint : decodeUtf8(text)) {
        if (unit >= map.size() - 1) break;
        map[unit++] = codepoint.offset;
        if (codepoint.value > 0xffff && unit < map.size() - 1) {
            map[unit++] = codepoint.offset;
        }
        if (unit < map.size()) {
            map[unit] = codepoint.offset + codepoint.length;
        }
    }
    map.back() = text.size();
    return map;
}

class CoreTextTextBackend final : public ITextBackend
{
public:
    explicit CoreTextTextBackend(CoreTextBackendOptions options)
        : options_(options)
    {
        diagnostics_.push_back({TextBackendDiagnostic::Severity::Info,
                                "CoreText native layout and raster backend active."});
    }

    ~CoreTextTextBackend() override
    {
        for (CFURLRef url : registeredFontUrls_) {
            CTFontManagerUnregisterFontsForURL(
                url, kCTFontManagerScopeProcess, nullptr);
            CFRelease(url);
        }
        for (const auto &entry : registeredDescriptors_) {
            for (const RegisteredDescriptor &registered : entry.second) {
                CFRelease(registered.descriptor);
            }
        }
    }

    bool registerFontFace(const FontFace &face) override
    {
        return registerFontFaceInternal(face);
    }

    bool addFontProvider(std::shared_ptr<FontProvider> provider) override
    {
        if (!provider) return false;
        const bool duplicate = std::any_of(
            providers_.begin(), providers_.end(), [&](const auto &existing) {
                return existing.get() == provider.get();
            });
        if (!duplicate) providers_.push_back(std::move(provider));
        return true;
    }

    bool refreshSystemFonts() override
    {
        FontSystem::refreshInstalledFonts();
        for (const auto &provider : providers_) provider->refresh();
        invalidateCache();
        return true;
    }

    bool setFontFallbackChain(const FontFallbackChain &chain) override
    {
        fallbackFamilies_ = chain.familiesInResolutionOrder();
        invalidateCache();
        return true;
    }

    std::vector<std::string> resolveFontFamilies(
        const std::string &preferredFamily) const override
    {
        if (!fallbackFamilies_.empty()
            && (preferredFamily.empty()
                || canonicalFontFamilyName(preferredFamily)
                    == canonicalFontFamilyName(fallbackFamilies_.front()))) {
            return fallbackFamilies_;
        }
        return preferredFamily.empty() ? std::vector<std::string>{}
                                       : std::vector<std::string>{preferredFamily};
    }

    std::vector<TextLineBreak> breakLines(const std::string &text, float maxWidth,
                                          const Paint &paint) const override
    {
        std::vector<TextLineBreak> result;
        if (text.empty() || maxWidth <= 0.0f || paint.getTextSize() <= 0.0f) {
            return result;
        }
        ensureProviderFamily(paint);
        auto attributed = createAttributedString(
            text, paint, registeredDescriptorForPaint(paint));
        if (!attributed) return result;
        CFRef<CTTypesetterRef> typesetter(
            CTTypesetterCreateWithAttributedString(attributed.get()));
        if (!typesetter) return result;
        const CFIndex length = CFAttributedStringGetLength(attributed.get());
        const auto map = buildUtf16ToUtf8Map(text, length);
        CFIndex start = 0;
        while (start < length) {
            CFIndex count = CTTypesetterSuggestLineBreak(
                typesetter.get(), start, maxWidth);
            if (count <= 0) count = 1;
            CFRef<CTLineRef> line(CTTypesetterCreateLine(
                typesetter.get(), CFRangeMake(start, count)));
            const float width = line
                ? static_cast<float>(CTLineGetTypographicBounds(
                      line.get(), nullptr, nullptr, nullptr))
                : 0.0f;
            const std::size_t sourceStart = map[static_cast<std::size_t>(start)];
            const std::size_t sourceEnd = map[static_cast<std::size_t>(
                std::min(start + count, length))];
            result.push_back({sourceStart, sourceEnd - sourceStart, width});
            start += count;
        }
        return result;
    }

    bool hasGlyphForCodepoint(std::uint32_t codepoint,
                              const Paint &paint) const override
    {
        if (codepoint > 0x10ffff || codepoint < 32) return false;
        std::string utf8;
        if (codepoint <= 0x7f) {
            utf8.push_back(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7ff) {
            utf8.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
            utf8.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        } else if (codepoint <= 0xffff) {
            utf8.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
            utf8.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
            utf8.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        } else {
            utf8.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
            utf8.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
            utf8.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
            utf8.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        }
        auto attributed = createAttributedString(
            utf8, paint, registeredDescriptorForPaint(paint));
        if (!attributed) return false;
        CFRef<CTLineRef> line(CTLineCreateWithAttributedString(attributed.get()));
        if (!line) return false;
        CFArrayRef runs = CTLineGetGlyphRuns(line.get());
        if (runs == nullptr) return false;
        for (CFIndex i = 0; i < CFArrayGetCount(runs); ++i) {
            const CTRunRef run = static_cast<CTRunRef>(
                const_cast<void *>(CFArrayGetValueAtIndex(runs, i)));
            const CFIndex glyphCount = CTRunGetGlyphCount(run);
            std::vector<CGGlyph> glyphs(static_cast<std::size_t>(glyphCount));
            if (glyphCount > 0) {
                CTRunGetGlyphs(run, CFRangeMake(0, 0), glyphs.data());
            }
            if (std::any_of(glyphs.begin(), glyphs.end(),
                            [](CGGlyph glyph) { return glyph != 0; })) {
                return true;
            }
        }
        return false;
    }

    std::vector<TextBackendDiagnostic> diagnostics() const override
    {
        return diagnostics_;
    }

    float measureTextWidth(const std::string &text,
                           const Paint &paint) const override
    {
        return measure(text, paint).width;
    }

    RectF measureTextBounds(const std::string &text,
                            const Paint &paint) const override
    {
        const LayoutInfo info = measure(text, paint);
        float left = 0.0f;
        if (paint.getTextAlign() == Paint::TextAlign::CENTER) {
            left = -info.width * 0.5f;
        } else if (paint.getTextAlign() == Paint::TextAlign::RIGHT) {
            left = -info.width;
        }
        return {left,
                textBaselineOffset(
                    paint.getTextBaseline(), info.height, info.ascent),
                info.width, info.height};
    }

    TextMetrics measureTextMetrics(const std::string &text,
                                   const Paint &paint) const override
    {
        const LayoutInfo info = measure(text, paint);
        TextMetrics result;
        result.width = info.width;
        result.height = info.height;
        result.ascent = info.ascent;
        result.descent = info.descent;
        result.lineGap = info.leading;
        result.lineHeight = info.ascent + info.descent + info.leading;
        result.top = 0.0f;
        result.bottom = info.height;
        result.bounds = measureTextBounds(text, paint);
        return result;
    }

    TextRenderResult renderText(const std::string &text, float x, float y,
                                const Paint &paint) const override
    {
        TextRenderResult result;
        if (text.empty() || paint.getTextSize() <= 0.0f) return result;
        ensureProviderFamily(paint);
        const std::string key = renderCacheKey(text, paint, generation_);
        auto found = renderCache_.find(key);
        if (found == renderCache_.end()) {
            CachedRender rendered;
            if (!rasterize(text, paint, rendered)) return result;
            if (rendered.pixels.size() <= kCacheBudgetBytes) {
                while (renderCacheBytes_ + rendered.pixels.size()
                       > kCacheBudgetBytes && !renderCache_.empty()) {
                    auto victim = renderCache_.begin();
                    renderCacheBytes_ -= victim->second.pixels.size();
                    renderCache_.erase(victim);
                }
                renderCacheBytes_ += rendered.pixels.size();
                found = renderCache_.emplace(key, std::move(rendered)).first;
            } else {
                transientRender_ = std::move(rendered);
                ++stats_.layoutCacheMisses;
                return makeResult(transientRender_, x, y, paint);
            }
            ++stats_.layoutCacheMisses;
        } else {
            ++stats_.layoutCacheHits;
        }
        return makeResult(found->second, x, y, paint);
    }

    TextRenderStats renderStats() const override { return stats_; }
    void resetRenderStats() override { stats_ = {}; }

private:
    struct CachedRender
    {
        float width = 0.0f;
        float height = 0.0f;
        float alphabeticBaseline = 0.0f;
        int pixelWidth = 0;
        int pixelHeight = 0;
        std::uint64_t contentId = 0;
        std::vector<unsigned char> pixels;
    };

    struct RegisteredDescriptor
    {
        int weight = 400;
        FontSlant slant = FontSlant::NORMAL;
        CTFontDescriptorRef descriptor = nullptr;
    };

    static constexpr std::size_t kCacheBudgetBytes = 8u * 1024u * 1024u;

    LayoutInfo measure(const std::string &text, const Paint &paint) const
    {
        if (text.empty() || paint.getTextSize() <= 0.0f) return {};
        ensureProviderFamily(paint);
        auto attributed = createAttributedString(
            text, paint, registeredDescriptorForPaint(paint));
        return attributed ? measureAttributedString(attributed.get()) : LayoutInfo{};
    }

    bool rasterize(const std::string &text, const Paint &paint,
                   CachedRender &result) const
    {
        auto attributed = createAttributedString(
            text, paint, registeredDescriptorForPaint(paint));
        if (!attributed) return false;
        const LayoutInfo info = measureAttributedString(attributed.get());
        result.width = info.width;
        result.height = info.height;
        result.alphabeticBaseline = info.ascent;
        result.pixelWidth = std::max(1, static_cast<int>(std::ceil(info.width)));
        result.pixelHeight = std::max(1, static_cast<int>(std::ceil(info.height)));
        const std::size_t rowBytes = static_cast<std::size_t>(result.pixelWidth) * 4u;
        result.pixels.assign(rowBytes * static_cast<std::size_t>(result.pixelHeight), 0);

        CFRef<CGColorSpaceRef> colorSpace(CGColorSpaceCreateDeviceRGB());
        CFRef<CGContextRef> context(CGBitmapContextCreate(
            result.pixels.data(), result.pixelWidth, result.pixelHeight, 8,
            rowBytes, colorSpace.get(),
            kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big));
        if (!context) return false;
        CGContextSetShouldAntialias(context.get(), paint.isAntiAlias());
        CGContextSetShouldSmoothFonts(context.get(), paint.isAntiAlias());
        CGContextSetRGBFillColor(context.get(), 1.0, 1.0, 1.0, 1.0);
        CGContextSetTextMatrix(context.get(), CGAffineTransformIdentity);

        CFRef<CTFramesetterRef> framesetter(
            CTFramesetterCreateWithAttributedString(attributed.get()));
        CFRef<CGMutablePathRef> path(CGPathCreateMutable());
        CGPathAddRect(path.get(), nullptr,
                      CGRectMake(0.0, 0.0, result.pixelWidth, result.pixelHeight));
        CFRef<CTFrameRef> frame(CTFramesetterCreateFrame(
            framesetter.get(), CFRangeMake(0, 0), path.get(), nullptr));
        if (!frame) return false;
        CTFrameDraw(frame.get(), context.get());

        if (paint.isUnderline() || paint.isStrikethrough()) {
            const float thickness = std::max(1.0f, paint.getTextSize() * 0.06f);
            CGContextSetRGBFillColor(context.get(), 1.0, 1.0, 1.0, 1.0);
            if (paint.isUnderline()) {
                CGContextFillRect(context.get(), CGRectMake(
                    0.0, std::max(0.0f, info.descent * 0.25f),
                    result.pixelWidth, thickness));
            }
            if (paint.isStrikethrough()) {
                CGContextFillRect(context.get(), CGRectMake(
                    0.0, std::max(0.0f, info.descent + info.ascent * 0.55f),
                    result.pixelWidth, thickness));
            }
        }

        // CGBitmapContext exposes its backing memory in the same row order that
        // Metal's top-left image resources consume. Keep those rows intact;
        // flipping here mirrors every glyph inside its destination quad.
        // Core Graphics stores premultiplied channels, while ImageResource's
        // RGBA contract is straight alpha. Convert once so GPU backends can
        // apply paint tint and premultiply exactly once at composition time.
        for (std::size_t offset = 0; offset + 3u < result.pixels.size();
             offset += 4u) {
            const unsigned int alpha = result.pixels[offset + 3u];
            if (alpha == 0u || alpha == 255u) continue;
            for (std::size_t channel = 0; channel < 3u; ++channel) {
                const unsigned int value = result.pixels[offset + channel];
                result.pixels[offset + channel] = static_cast<unsigned char>(
                    std::min(255u, (value * 255u + alpha / 2u) / alpha));
            }
        }
        result.contentId = hashPixels(result.pixels, result.pixelWidth,
                                      result.pixelHeight);
        ++stats_.rasterizationCount;
        return true;
    }

    TextRenderResult makeResult(const CachedRender &cached, float x, float y,
                                const Paint &paint) const
    {
        TextRenderResult result;
        float alignedX = x;
        if (paint.getTextAlign() == Paint::TextAlign::CENTER) {
            alignedX -= cached.width * 0.5f;
        } else if (paint.getTextAlign() == Paint::TextAlign::RIGHT) {
            alignedX -= cached.width;
        }
        result.kind = TextRenderKind::Bitmap;
        result.drawX = alignedX;
        result.drawY = y + textBaselineOffset(
            paint.getTextBaseline(), cached.height,
            cached.alphabeticBaseline);
        result.width = cached.width;
        result.height = cached.height;
        result.bitmapWidth = cached.pixelWidth;
        result.bitmapHeight = cached.pixelHeight;
        result.bitmapContentId = cached.contentId;
        result.bitmapPixels = cached.pixels;
        return result;
    }

    bool registerFontFaceInternal(const FontFace &face) const
    {
        if (!face.isValid()) return false;
        const std::string familyKey = canonicalFontFamilyName(face.family());
        std::string faceKey = familyKey + '|' + std::to_string(face.weight())
            + '|' + std::to_string(static_cast<int>(face.slant()))
            + '|' + std::to_string(face.faceIndex()) + '|';
        if (face.sourceType() == FontSourceType::FILE) {
            faceKey += face.path();
        } else {
            faceKey += face.sourceId();
            faceKey += '|' + std::to_string(
                reinterpret_cast<std::uintptr_t>(face.bytes()));
        }
        if (registeredFaces_.find(faceKey) != registeredFaces_.end()) return true;

        if (face.sourceType() == FontSourceType::FILE) {
            auto path = makeString(face.path());
            CFRef<CFURLRef> url(path ? CFURLCreateWithFileSystemPath(
                kCFAllocatorDefault, path.get(), kCFURLPOSIXPathStyle, false)
                                  : nullptr);
            if (!url) return false;
            CFErrorRef error = nullptr;
            const bool registered = CTFontManagerRegisterFontsForURL(
                url.get(), kCTFontManagerScopeProcess, &error);
            const bool alreadyRegistered = error != nullptr
                && CFErrorGetCode(error) == kCTFontManagerErrorAlreadyRegistered;
            if (error != nullptr) CFRelease(error);
            if (!registered && !alreadyRegistered) return false;
            if (registered) {
                CFRetain(url.get());
                registeredFontUrls_.push_back(url.get());
            }
        } else if (const auto *bytes = face.bytes(); bytes != nullptr) {
            CFRef<CFDataRef> data(CFDataCreate(
                kCFAllocatorDefault, bytes->data(),
                static_cast<CFIndex>(bytes->size())));
            if (!data) return false;
            CFRef<CFArrayRef> descriptors(
                CTFontManagerCreateFontDescriptorsFromData(data.get()));
            if (!descriptors || CFArrayGetCount(descriptors.get()) == 0) {
                return false;
            }
            const CFIndex selectedIndex = std::min<CFIndex>(
                static_cast<CFIndex>(face.faceIndex()),
                CFArrayGetCount(descriptors.get()) - 1);
            const auto selected = static_cast<CTFontDescriptorRef>(
                const_cast<void *>(CFArrayGetValueAtIndex(
                    descriptors.get(), selectedIndex)));
            if (selected == nullptr) return false;
            CFRetain(selected);
            registeredDescriptors_[familyKey].push_back(
                {face.weight(), face.slant(), selected});
        } else {
            return false;
        }
        registeredFaces_.emplace(std::move(faceKey), true);
        registeredFamilies_.emplace(familyKey, true);
        invalidateCache();
        return true;
    }

    void ensureProviderFamily(const Paint &paint) const
    {
        if (!paint.hasFontFamily()) return;
        const std::string familyKey = canonicalFontFamilyName(paint.getFontFamily());
        if (registeredFamilies_.find(familyKey) != registeredFamilies_.end()) return;
        FontMatchRequest request;
        request.family = paint.getFontFamily();
        request.weight = paint.getFontWeight();
        request.slant = paint.getFontSlant();
        request.locale = paint.getTextLocale();
        for (const auto &provider : providers_) {
            if (!provider->hasFamily(request.family)) continue;
            for (const FontFace *face : provider->match(request)) {
                if (face != nullptr && registerFontFaceInternal(*face)) return;
            }
        }
    }

    CTFontDescriptorRef registeredDescriptorForPaint(const Paint &paint) const
    {
        if (!paint.hasFontFamily()) return nullptr;
        const auto found = registeredDescriptors_.find(
            canonicalFontFamilyName(paint.getFontFamily()));
        if (found == registeredDescriptors_.end() || found->second.empty()) {
            return nullptr;
        }
        const RegisteredDescriptor *best = &found->second.front();
        int bestScore = std::numeric_limits<int>::max();
        for (const RegisteredDescriptor &candidate : found->second) {
            const int slantPenalty =
                candidate.slant == paint.getFontSlant() ? 0 : 1000;
            const int score = std::abs(candidate.weight - paint.getFontWeight())
                + slantPenalty;
            if (score < bestScore) {
                best = &candidate;
                bestScore = score;
            }
        }
        return best->descriptor;
    }

    void invalidateCache() const
    {
        renderCache_.clear();
        renderCacheBytes_ = 0;
        ++generation_;
        if (generation_ == 0) generation_ = 1;
    }

    CoreTextBackendOptions options_;
    std::vector<std::shared_ptr<FontProvider>> providers_;
    std::vector<std::string> fallbackFamilies_;
    mutable std::unordered_map<std::string, bool> registeredFamilies_;
    mutable std::unordered_map<std::string, bool> registeredFaces_;
    mutable std::vector<CFURLRef> registeredFontUrls_;
    mutable std::unordered_map<std::string, std::vector<RegisteredDescriptor>>
        registeredDescriptors_;
    mutable std::unordered_map<std::string, CachedRender> renderCache_;
    mutable CachedRender transientRender_;
    mutable std::size_t renderCacheBytes_ = 0;
    mutable std::uint64_t generation_ = 1;
    mutable TextRenderStats stats_;
    std::vector<TextBackendDiagnostic> diagnostics_;
};

} // namespace

bool isCoreTextAvailable()
{
    return true;
}

std::unique_ptr<ITextBackend> createCoreTextTextBackend(
    const CoreTextBackendOptions &options)
{
    return std::make_unique<CoreTextTextBackend>(options);
}

} // namespace wsc::text

#else

namespace wsc::text {
bool isCoreTextAvailable() { return false; }
std::unique_ptr<ITextBackend> createCoreTextTextBackend(
    const CoreTextBackendOptions &) { return nullptr; }
} // namespace wsc::text

#endif

#include "text/TextShaper.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <vector>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

#if __has_include(<hb.h>)
#include <hb.h>
#include <hb-ot.h>
#elif __has_include(<harfbuzz/hb.h>)
#include <harfbuzz/hb.h>
#include <harfbuzz/hb-ot.h>
#else
#error "HarfBuzz headers are required when compiling HarfBuzzTextShaper.cpp"
#endif

#include "text/TextUtils.h"

namespace wsc::text {

namespace {

#if defined(__ANDROID__)
void logHarfBuzzFailure(const char *stage, unsigned int first = 0,
                        unsigned int second = 0)
{
    __android_log_print(ANDROID_LOG_WARN, "WhatsCanvas",
                        "HarfBuzz shaping failure: stage=%s first=%u second=%u",
                        stage, first, second);
}
#else
void logHarfBuzzFailure(const char *, unsigned int = 0,
                        unsigned int = 0)
{
}
#endif

struct HbBlobDeleter
{
    void operator()(hb_blob_t *blob) const
    {
        hb_blob_destroy(blob);
    }
};

struct HbFaceDeleter
{
    void operator()(hb_face_t *face) const
    {
        hb_face_destroy(face);
    }
};

struct HbFontDeleter
{
    void operator()(hb_font_t *font) const
    {
        hb_font_destroy(font);
    }
};

struct HbBufferDeleter
{
    void operator()(hb_buffer_t *buffer) const
    {
        hb_buffer_destroy(buffer);
    }
};

using HbBlobPtr = std::unique_ptr<hb_blob_t, HbBlobDeleter>;
using HbFacePtr = std::unique_ptr<hb_face_t, HbFaceDeleter>;
using HbFontPtr = std::unique_ptr<hb_font_t, HbFontDeleter>;
using HbBufferPtr = std::unique_ptr<hb_buffer_t, HbBufferDeleter>;

const Utf8Codepoint *codepointForCluster(const std::vector<Utf8Codepoint> &codepoints, std::size_t cluster)
{
    const Utf8Codepoint *best = nullptr;
    for (const Utf8Codepoint &codepoint : codepoints) {
        if (codepoint.offset > cluster) {
            break;
        }
        best = &codepoint;
    }
    return best;
}

bool isLineBreakCodepoint(std::uint32_t codepoint)
{
    return codepoint == '\n' || codepoint == '\r';
}

class HarfBuzzTextShapingEngine final : public ITextShapingEngine
{
public:
    TextShapingBackend backend() const override
    {
        return TextShapingBackend::OpenType;
    }

    const char *name() const override
    {
        return "harfbuzz";
    }

    bool supportsOpenTypeFeatures() const override
    {
        return true;
    }

    std::optional<ShapedTextRun> shape(const TextShapeInput &input,
                                       const GlyphResolver &glyphResolver) const override
    {
        if (!glyphResolver || input.normalizedText.empty() || input.pixelSize <= 0.0f
            || !input.fontData || input.fontData->data == nullptr || input.fontData->size == 0) {
            logHarfBuzzFailure(
                "invalid-input",
                input.fontData ? static_cast<unsigned int>(input.fontData->size) : 0u,
                input.fontData && input.fontData->data != nullptr ? 1u : 0u);
            return std::nullopt;
        }

        const auto codepoints = decodeUtf8(input.normalizedText);
        if (codepoints.empty()) {
            return std::nullopt;
        }

        HbBlobPtr blob(hb_blob_create(reinterpret_cast<const char *>(input.fontData->data),
                                      static_cast<unsigned int>(input.fontData->size),
                                      HB_MEMORY_MODE_READONLY,
                                      nullptr,
                                      nullptr));
        if (!blob) {
            logHarfBuzzFailure("blob");
            return std::nullopt;
        }

        const unsigned int faceIndex = static_cast<unsigned int>(std::max(0, input.fontData->faceIndex));
        const unsigned int faceCount = hb_face_count(blob.get());
        if (faceIndex >= faceCount) {
            logHarfBuzzFailure("face-index", faceIndex, faceCount);
            return std::nullopt;
        }
        HbFacePtr face(hb_face_create(blob.get(), faceIndex));
        if (!face) {
            logHarfBuzzFailure("face", faceIndex, faceCount);
            return std::nullopt;
        }

        HbFontPtr font(hb_font_create(face.get()));
        if (!font) {
            logHarfBuzzFailure("font");
            return std::nullopt;
        }
        hb_ot_font_set_funcs(font.get());
        const int scale = std::max(1, static_cast<int>(std::round(input.pixelSize * 64.0f)));
        hb_font_set_scale(font.get(), scale, scale);
        std::vector<hb_variation_t> variations;
        variations.reserve(input.variationCoordinates.size());
        for (const wsc::FontVariationCoordinate &coordinate :
             input.variationCoordinates) {
            if (coordinate.tag.size() != 4 || !std::isfinite(coordinate.value)) continue;
            variations.push_back({
                hb_tag_from_string(coordinate.tag.c_str(), 4), coordinate.value});
        }
        if (!variations.empty()) {
            hb_font_set_variations(font.get(), variations.data(),
                                   static_cast<unsigned int>(variations.size()));
        }

        HbBufferPtr buffer(hb_buffer_create());
        if (!buffer) {
            logHarfBuzzFailure("buffer");
            return std::nullopt;
        }
        if (!input.language.empty()) {
            hb_buffer_set_language(buffer.get(), hb_language_from_string(input.language.c_str(), -1));
        }
        if (input.direction == TextDirection::LeftToRight) {
            hb_buffer_set_direction(buffer.get(), HB_DIRECTION_LTR);
        } else if (input.direction == TextDirection::RightToLeft) {
            hb_buffer_set_direction(buffer.get(), HB_DIRECTION_RTL);
        }
        hb_buffer_add_utf8(buffer.get(),
                           input.normalizedText.c_str(),
                           static_cast<int>(input.normalizedText.size()),
                           0,
                           static_cast<int>(input.normalizedText.size()));
        hb_buffer_guess_segment_properties(buffer.get());
        std::vector<hb_feature_t> features;
        features.reserve(input.openTypeFeatures.size());
        for (const OpenTypeFeature &feature : input.openTypeFeatures) {
            if (feature.tag.size() != 4) {
                continue;
            }
            hb_feature_t hbFeature;
            hbFeature.tag = hb_tag_from_string(feature.tag.c_str(), 4);
            hbFeature.value = feature.value;
            hbFeature.start = HB_FEATURE_GLOBAL_START;
            hbFeature.end = HB_FEATURE_GLOBAL_END;
            features.push_back(hbFeature);
        }
        hb_shape(font.get(), buffer.get(), features.empty() ? nullptr : features.data(),
                 static_cast<unsigned int>(features.size()));

        unsigned int glyphCount = 0;
        hb_glyph_info_t *glyphInfos = hb_buffer_get_glyph_infos(buffer.get(), &glyphCount);
        hb_glyph_position_t *glyphPositions = hb_buffer_get_glyph_positions(buffer.get(), &glyphCount);
        if (glyphInfos == nullptr || glyphPositions == nullptr || glyphCount == 0) {
            logHarfBuzzFailure("empty-buffer", glyphCount);
            return std::nullopt;
        }

        ShapedTextRun run;
        run.rightToLeft = HB_DIRECTION_IS_BACKWARD(hb_buffer_get_direction(buffer.get()));
        const float spacing = std::isfinite(input.letterSpacing) ? input.letterSpacing : 0.0f;
        bool hasVisibleGlyph = false;

        for (unsigned int i = 0; i < glyphCount; ++i) {
            const auto *source = codepointForCluster(codepoints, glyphInfos[i].cluster);
            if (source == nullptr || isLineBreakCodepoint(source->value)) {
                break;
            }
            if (source->value < 32 || isBidiControlCodepoint(source->value)
                || isZeroWidthBreakCodepoint(source->value)) {
                continue;
            }

            const int glyphIndex = static_cast<int>(glyphInfos[i].codepoint);
            if (glyphIndex <= 0) {
                continue;
            }

            if (hasVisibleGlyph) {
                run.width += spacing;
            }

            ShapedGlyph glyph;
            glyph.codepoint = source->value;
            glyph.glyphIndex = glyphIndex;
            glyph.sourceStart = source->offset;
            glyph.sourceLength = source->length;
            glyph.advanceX = static_cast<float>(glyphPositions[i].x_advance) / 64.0f;
            glyph.offsetX = static_cast<float>(glyphPositions[i].x_offset) / 64.0f;
            glyph.offsetY = -static_cast<float>(glyphPositions[i].y_offset) / 64.0f;
            glyph.visible = true;
            run.width += glyph.advanceX;
            run.glyphs.push_back(glyph);
            hasVisibleGlyph = true;
        }

        if (!hasVisibleGlyph || run.glyphs.empty()) {
            logHarfBuzzFailure("no-visible-glyph", glyphCount,
                               glyphCount == 0 ? 0 : glyphInfos[0].codepoint);
            return std::nullopt;
        }

        run.width = std::max(0.0f, run.width);
        return run;
    }
};

} // namespace

std::unique_ptr<ITextShapingEngine> createHarfBuzzTextShapingEngine()
{
    return std::make_unique<HarfBuzzTextShapingEngine>();
}

} // namespace wsc::text

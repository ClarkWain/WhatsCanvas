#include "text/FontRasterizer.h"

#include <algorithm>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include "../../include/wsc/Font.h"

namespace {

std::uint32_t readU32BE(const unsigned char *data)
{
    return (static_cast<std::uint32_t>(data[0]) << 24u)
        | (static_cast<std::uint32_t>(data[1]) << 16u)
        | (static_cast<std::uint32_t>(data[2]) << 8u)
        | static_cast<std::uint32_t>(data[3]);
}

std::uint16_t readU16BE(const unsigned char *data)
{
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(data[0]) << 8u)
                                      | static_cast<std::uint16_t>(data[1]));
}

bool tagEquals(const unsigned char *data, const char tag[4])
{
    return data[0] == static_cast<unsigned char>(tag[0])
        && data[1] == static_cast<unsigned char>(tag[1])
        && data[2] == static_cast<unsigned char>(tag[2])
        && data[3] == static_cast<unsigned char>(tag[3]);
}

std::string makeFaceKey(const wsc::FontFace &face)
{
    if (face.sourceType() == wsc::FontSourceType::FILE) {
        return std::string("file:") + face.path();
    }
    return std::string("memory:") + face.family() + ":" + std::to_string(reinterpret_cast<std::uintptr_t>(face.bytes()));
}

std::vector<unsigned char> readFileBytes(const std::string &path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }

    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size <= 0) {
        return {};
    }

    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    input.seekg(0, std::ios::beg);
    input.read(reinterpret_cast<char *>(bytes.data()), size);
    return input ? bytes : std::vector<unsigned char>();
}

} // namespace

namespace wsc::text {

ColorFontTables detectColorFontTables(FontDataView fontData)
{
    ColorFontTables result;
    if (fontData.data == nullptr || fontData.size < 12u) {
        return result;
    }

    std::size_t fontOffset = 0;
    if (tagEquals(fontData.data, "ttcf")) {
        if (fontData.size < 16u || readU32BE(fontData.data + 8u) == 0u) {
            return result;
        }
        fontOffset = static_cast<std::size_t>(readU32BE(fontData.data + 12u));
        if (fontOffset > fontData.size || fontData.size - fontOffset < 12u) {
            return result;
        }
    }

    const unsigned char *sfnt = fontData.data + fontOffset;
    const std::size_t sfntSize = fontData.size - fontOffset;
    const std::uint16_t tableCount = readU16BE(sfnt + 4u);
    if (tableCount == 0u || tableCount > (sfntSize - 12u) / 16u) {
        return result;
    }

    for (std::uint16_t i = 0; i < tableCount; ++i) {
        const unsigned char *record = sfnt + 12u + static_cast<std::size_t>(i) * 16u;
        result.colr = result.colr || tagEquals(record, "COLR");
        result.cpal = result.cpal || tagEquals(record, "CPAL");
        result.cbdt = result.cbdt || tagEquals(record, "CBDT");
        result.cblc = result.cblc || tagEquals(record, "CBLC");
        result.sbix = result.sbix || tagEquals(record, "sbix");
        result.svg = result.svg || tagEquals(record, "SVG ");
    }
    return result;
}

struct FontRasterizer::LoadedFace
{
    std::vector<unsigned char> bytes;
    stbtt_fontinfo info = {};
    bool valid = false;
};

const FontRasterizer::LoadedFace *FontRasterizer::loadFace(const FontFace &face) const
{
    static std::unordered_map<std::string, std::unique_ptr<LoadedFace>> cache;

    const std::string key = makeFaceKey(face);
    const auto found = cache.find(key);
    if (found != cache.end()) {
        return found->second->valid ? found->second.get() : nullptr;
    }

    auto loaded = std::make_unique<LoadedFace>();
    if (face.sourceType() == FontSourceType::FILE) {
        loaded->bytes = readFileBytes(face.path());
    } else if (const std::vector<std::uint8_t> *bytes = face.bytes()) {
        loaded->bytes.assign(bytes->begin(), bytes->end());
    }

    if (!loaded->bytes.empty()) {
        const int fontOffset = stbtt_GetFontOffsetForIndex(loaded->bytes.data(), 0);
        loaded->valid = fontOffset >= 0
            && stbtt_InitFont(&loaded->info, loaded->bytes.data(), fontOffset) != 0;
    }

    LoadedFace *result = loaded.get();
    cache.emplace(key, std::move(loaded));
    return result->valid ? result : nullptr;
}

bool FontRasterizer::hasGlyph(const FontFace &face, std::uint32_t codepoint) const
{
    if (face.hasCodepointRanges() && !face.supportsCodepoint(codepoint)) {
        return false;
    }

    const LoadedFace *loaded = loadFace(face);
    return loaded != nullptr && stbtt_FindGlyphIndex(&loaded->info, static_cast<int>(codepoint)) != 0;
}

std::optional<int> FontRasterizer::glyphIndex(const FontFace &face, std::uint32_t codepoint) const
{
    const LoadedFace *loaded = loadFace(face);
    if (loaded == nullptr) {
        return std::nullopt;
    }

    const int index = stbtt_FindGlyphIndex(&loaded->info, static_cast<int>(codepoint));
    return index == 0 ? std::nullopt : std::optional<int>(index);
}

std::optional<float> FontRasterizer::glyphAdvance(const FontFace &face, std::uint32_t codepoint,
                                                  float pixelSize) const
{
    const auto metrics = glyphMetrics(face, codepoint, pixelSize);
    return metrics ? std::optional<float>(metrics->advanceX) : std::nullopt;
}

std::optional<GlyphMetrics> FontRasterizer::glyphMetrics(const FontFace &face, std::uint32_t codepoint,
                                                         float pixelSize) const
{
    const LoadedFace *loaded = loadFace(face);
    if (loaded == nullptr || pixelSize <= 0.0f) {
        return std::nullopt;
    }

    const int glyphIndex = stbtt_FindGlyphIndex(&loaded->info, static_cast<int>(codepoint));
    if (glyphIndex == 0) {
        return std::nullopt;
    }

    int advance = 0;
    int leftBearing = 0;
    stbtt_GetGlyphHMetrics(&loaded->info, glyphIndex, &advance, &leftBearing);
    GlyphMetrics metrics;
    metrics.glyphIndex = glyphIndex;
    metrics.advanceX = static_cast<float>(advance) * stbtt_ScaleForPixelHeight(&loaded->info, pixelSize);
    return metrics;
}

std::optional<RasterizedGlyph> FontRasterizer::rasterizeGlyph(const FontFace &face, std::uint32_t codepoint,
                                                              float pixelSize) const
{
    const auto index = glyphIndex(face, codepoint);
    if (!index) {
        return std::nullopt;
    }

    return rasterizeGlyphIndex(face, *index, codepoint, pixelSize);
}

std::optional<RasterizedGlyph> FontRasterizer::rasterizeGlyphIndex(const FontFace &face, int glyphIndex,
                                                                   std::uint32_t sourceCodepoint,
                                                                   float pixelSize) const
{
    const LoadedFace *loaded = loadFace(face);
    if (loaded == nullptr || pixelSize <= 0.0f || glyphIndex <= 0) {
        return std::nullopt;
    }

    const float scale = stbtt_ScaleForPixelHeight(&loaded->info, pixelSize);
    int advance = 0;
    int leftBearing = 0;
    stbtt_GetGlyphHMetrics(&loaded->info, glyphIndex, &advance, &leftBearing);

    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    stbtt_GetGlyphBitmapBox(&loaded->info, glyphIndex, scale, scale, &x0, &y0, &x1, &y1);

    GlyphBitmap bitmap;
    bitmap.format = GlyphBitmapFormat::Alpha;
    bitmap.width = std::max(0, x1 - x0);
    bitmap.height = std::max(0, y1 - y0);
    bitmap.bearingX = static_cast<float>(x0);
    bitmap.bearingY = static_cast<float>(y0);
    bitmap.advanceX = static_cast<float>(advance) * scale;
    bitmap.alphaPixels.resize(static_cast<std::size_t>(bitmap.width) * static_cast<std::size_t>(bitmap.height));

    if (bitmap.width > 0 && bitmap.height > 0) {
        stbtt_MakeGlyphBitmap(&loaded->info, bitmap.alphaPixels.data(), bitmap.width, bitmap.height,
                              bitmap.width, scale, scale, glyphIndex);
    }

    RasterizedGlyph glyph;
    glyph.key.fontFamily = face.family();
    glyph.key.codepoint = sourceCodepoint;
    glyph.key.glyphIndex = glyphIndex;
    glyph.key.pixelSize = pixelSize;
    glyph.key.format = GlyphBitmapFormat::Alpha;
    glyph.bitmap = std::move(bitmap);
    return glyph;
}

std::optional<FontDataView> FontRasterizer::fontData(const FontFace &face) const
{
    const LoadedFace *loaded = loadFace(face);
    if (loaded == nullptr || loaded->bytes.empty()) {
        return std::nullopt;
    }

    return FontDataView{loaded->bytes.data(), loaded->bytes.size()};
}

std::optional<ColorFontTables> FontRasterizer::colorFontTables(const FontFace &face) const
{
    const auto data = fontData(face);
    if (!data) {
        return std::nullopt;
    }
    return detectColorFontTables(*data);
}

} // namespace wsc::text

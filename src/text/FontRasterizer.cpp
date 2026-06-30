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

} // namespace wsc::text

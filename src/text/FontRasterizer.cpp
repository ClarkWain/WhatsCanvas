#include "text/FontRasterizer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#if defined(WHATSCANVAS_HAS_FREETYPE)
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#endif

#include "../../include/wsc/Font.h"

namespace {

constexpr std::size_t kDefaultLoadedFaceCacheCapacity = 64;

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
    const std::string indexSuffix = "#" + std::to_string(face.faceIndex());
    if (face.sourceType() == wsc::FontSourceType::FILE) {
        return std::string("file:") + face.path() + indexSuffix;
    }
    return std::string("memory:") + face.family() + ":" + std::to_string(reinterpret_cast<std::uintptr_t>(face.bytes()))
        + indexSuffix;
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

struct TableView
{
    const unsigned char *data = nullptr;
    std::size_t size = 0;
};

struct ColorLayer
{
    int glyphIndex = 0;
    std::uint16_t paletteIndex = 0;
};

struct RgbaColor
{
    unsigned char r = 255;
    unsigned char g = 255;
    unsigned char b = 255;
    unsigned char a = 255;
};

std::optional<TableView> findSfntTable(const std::vector<unsigned char> &bytes,
                                       std::size_t fontOffset,
                                       const char tag[4])
{
    if (fontOffset > bytes.size() || bytes.size() - fontOffset < 12u) {
        return std::nullopt;
    }

    const unsigned char *sfnt = bytes.data() + fontOffset;
    const std::size_t sfntSize = bytes.size() - fontOffset;
    const std::uint16_t tableCount = readU16BE(sfnt + 4u);
    if (tableCount == 0u || tableCount > (sfntSize - 12u) / 16u) {
        return std::nullopt;
    }

    for (std::uint16_t i = 0; i < tableCount; ++i) {
        const unsigned char *record = sfnt + 12u + static_cast<std::size_t>(i) * 16u;
        if (!tagEquals(record, tag)) {
            continue;
        }

        const std::uint32_t offset = readU32BE(record + 8u);
        const std::uint32_t length = readU32BE(record + 12u);
        if (offset > bytes.size() || length > bytes.size() - offset) {
            return std::nullopt;
        }
        return TableView{bytes.data() + offset, static_cast<std::size_t>(length)};
    }

    return std::nullopt;
}

std::optional<std::vector<ColorLayer>> findColrLayers(TableView colr, int glyphIndex)
{
    if (colr.data == nullptr || colr.size < 14u || glyphIndex <= 0 || glyphIndex > 0xffff
        || readU16BE(colr.data) != 0u) {
        return std::nullopt;
    }

    const std::uint16_t baseGlyphCount = readU16BE(colr.data + 2u);
    const std::uint32_t baseGlyphOffset = readU32BE(colr.data + 4u);
    const std::uint32_t layerOffset = readU32BE(colr.data + 8u);
    const std::uint16_t layerCount = readU16BE(colr.data + 12u);
    if (baseGlyphOffset > colr.size || layerOffset > colr.size
        || static_cast<std::size_t>(baseGlyphCount) * 6u > colr.size - baseGlyphOffset
        || static_cast<std::size_t>(layerCount) * 4u > colr.size - layerOffset) {
        return std::nullopt;
    }

    for (std::uint16_t i = 0; i < baseGlyphCount; ++i) {
        const unsigned char *base = colr.data + baseGlyphOffset + static_cast<std::size_t>(i) * 6u;
        if (readU16BE(base) != static_cast<std::uint16_t>(glyphIndex)) {
            continue;
        }

        const std::uint16_t firstLayer = readU16BE(base + 2u);
        const std::uint16_t glyphLayerCount = readU16BE(base + 4u);
        if (firstLayer > layerCount || glyphLayerCount > layerCount - firstLayer) {
            return std::nullopt;
        }

        std::vector<ColorLayer> layers;
        layers.reserve(glyphLayerCount);
        for (std::uint16_t layer = 0; layer < glyphLayerCount; ++layer) {
            const unsigned char *record = colr.data + layerOffset
                + static_cast<std::size_t>(firstLayer + layer) * 4u;
            layers.push_back({static_cast<int>(readU16BE(record)), readU16BE(record + 2u)});
        }
        return layers.empty() ? std::nullopt : std::optional<std::vector<ColorLayer>>(std::move(layers));
    }

    return std::nullopt;
}

std::optional<RgbaColor> cpalColor(TableView cpal, std::uint16_t paletteIndex)
{
    if (paletteIndex == 0xffffu) {
        return RgbaColor{};
    }
    if (cpal.data == nullptr || cpal.size < 12u) {
        return std::nullopt;
    }

    const std::uint16_t version = readU16BE(cpal.data);
    const std::uint16_t paletteEntryCount = readU16BE(cpal.data + 2u);
    const std::uint16_t paletteCount = readU16BE(cpal.data + 4u);
    const std::uint16_t colorRecordCount = readU16BE(cpal.data + 6u);
    const std::uint32_t colorRecordOffset = readU32BE(cpal.data + 8u);
    if (version > 1u || paletteCount == 0u || paletteIndex >= paletteEntryCount
        || static_cast<std::size_t>(paletteCount) * 2u > cpal.size - 12u
        || colorRecordOffset > cpal.size
        || static_cast<std::size_t>(colorRecordCount) * 4u > cpal.size - colorRecordOffset) {
        return std::nullopt;
    }

    const std::uint16_t firstPaletteColor = readU16BE(cpal.data + 12u);
    const std::uint32_t colorIndex = static_cast<std::uint32_t>(firstPaletteColor) + paletteIndex;
    if (colorIndex >= colorRecordCount) {
        return std::nullopt;
    }

    const unsigned char *color = cpal.data + colorRecordOffset + static_cast<std::size_t>(colorIndex) * 4u;
    return RgbaColor{color[2], color[1], color[0], color[3]};
}

void compositePixel(unsigned char *dst, RgbaColor color, unsigned char coverage)
{
    const int srcA = static_cast<int>(color.a) * static_cast<int>(coverage) / 255;
    if (srcA <= 0) {
        return;
    }

    const int dstA = dst[3];
    const int outA = srcA + dstA * (255 - srcA) / 255;
    if (outA <= 0) {
        dst[0] = 0;
        dst[1] = 0;
        dst[2] = 0;
        dst[3] = 0;
        return;
    }

    dst[0] = static_cast<unsigned char>((static_cast<int>(color.r) * srcA
        + static_cast<int>(dst[0]) * dstA * (255 - srcA) / 255) / outA);
    dst[1] = static_cast<unsigned char>((static_cast<int>(color.g) * srcA
        + static_cast<int>(dst[1]) * dstA * (255 - srcA) / 255) / outA);
    dst[2] = static_cast<unsigned char>((static_cast<int>(color.b) * srcA
        + static_cast<int>(dst[2]) * dstA * (255 - srcA) / 255) / outA);
    dst[3] = static_cast<unsigned char>(outA);
}

#if defined(WHATSCANVAS_HAS_FREETYPE)
struct FreeTypeLibrary
{
    FT_Library library = nullptr;

    FreeTypeLibrary()
    {
        if (FT_Init_FreeType(&library) != 0) {
            library = nullptr;
        }
    }

    ~FreeTypeLibrary()
    {
        if (library != nullptr) {
            FT_Done_FreeType(library);
        }
    }

    bool valid() const
    {
        return library != nullptr;
    }
};

FreeTypeLibrary &freeTypeLibrary()
{
    static FreeTypeLibrary *library = new FreeTypeLibrary();
    return *library;
}

bool setFreeTypePixelSize(FT_Face face, float pixelSize)
{
    if (face == nullptr || pixelSize <= 0.0f) {
        return false;
    }
    const auto roundedSize = static_cast<FT_UInt>(std::max(1.0f, std::round(pixelSize)));
    return FT_Set_Pixel_Sizes(face, 0, roundedSize) == 0;
}
#endif

} // namespace

namespace wsc::text {

ColorFontTables detectColorFontTables(FontDataView fontData, int faceIndex)
{
    ColorFontTables result;
    if (fontData.data == nullptr || fontData.size < 12u) {
        return result;
    }

    std::size_t fontOffset = 0;
    const int clampedFaceIndex = std::max(0, faceIndex);
    if (tagEquals(fontData.data, "ttcf")) {
        if (fontData.size < 16u) {
            return result;
        }
        const std::uint32_t faceCount = readU32BE(fontData.data + 8u);
        if (faceCount == 0u || static_cast<std::uint32_t>(clampedFaceIndex) >= faceCount
            || fontData.size - 12u < (static_cast<std::size_t>(clampedFaceIndex) + 1u) * 4u) {
            return result;
        }
        fontOffset = static_cast<std::size_t>(readU32BE(fontData.data + 12u
            + static_cast<std::size_t>(clampedFaceIndex) * 4u));
        if (fontOffset > fontData.size || fontData.size - fontOffset < 12u) {
            return result;
        }
    } else if (clampedFaceIndex > 0) {
        return result;
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
    ~LoadedFace()
    {
#if defined(WHATSCANVAS_HAS_FREETYPE)
        if (ftFace != nullptr) {
            FT_Done_Face(ftFace);
        }
#endif
    }

    std::vector<unsigned char> bytes;
    stbtt_fontinfo info = {};
    std::size_t fontOffset = 0;
    bool stbValid = false;
#if defined(WHATSCANVAS_HAS_FREETYPE)
    FT_Face ftFace = nullptr;
#endif
    bool valid = false;
};

struct FontRasterizer::CacheState
{
    std::unordered_map<std::string, std::unique_ptr<LoadedFace>> entries;
    std::deque<std::string> lruOrder;
    std::size_t capacity = kDefaultLoadedFaceCacheCapacity;
    std::size_t hitCount = 0;
    std::size_t missCount = 0;
    std::size_t evictionCount = 0;
};

FontRasterizer::CacheState &FontRasterizer::cacheState()
{
    static CacheState state;
    return state;
}

void FontRasterizer::touchCacheEntry(CacheState &cache, const std::string &key)
{
    const auto existing = std::find(cache.lruOrder.begin(), cache.lruOrder.end(), key);
    if (existing != cache.lruOrder.end()) {
        cache.lruOrder.erase(existing);
    }
    cache.lruOrder.push_back(key);
}

void FontRasterizer::trimCache(CacheState &cache)
{
    while (cache.entries.size() > cache.capacity && !cache.lruOrder.empty()) {
        const std::string evictedKey = cache.lruOrder.front();
        cache.lruOrder.pop_front();
        if (cache.entries.erase(evictedKey) > 0u) {
            ++cache.evictionCount;
        }
    }
}

FontRasterizerCacheStats FontRasterizer::cacheStats() const
{
    const CacheState &cache = cacheState();
    FontRasterizerCacheStats stats;
    stats.faceCount = cache.entries.size();
    stats.capacity = cache.capacity;
    stats.hitCount = cache.hitCount;
    stats.missCount = cache.missCount;
    stats.evictionCount = cache.evictionCount;
    return stats;
}

void FontRasterizer::clearCache() const
{
    CacheState &cache = cacheState();
    cache.entries.clear();
    cache.lruOrder.clear();
    cache.hitCount = 0;
    cache.missCount = 0;
    cache.evictionCount = 0;
}

void FontRasterizer::setCacheCapacity(std::size_t capacity) const
{
    CacheState &cache = cacheState();
    cache.capacity = std::max<std::size_t>(1u, capacity);
    trimCache(cache);
}

const FontRasterizer::LoadedFace *FontRasterizer::loadFace(const FontFace &face) const
{
    CacheState &cache = cacheState();
    const std::string key = makeFaceKey(face);
    const auto found = cache.entries.find(key);
    if (found != cache.entries.end()) {
        ++cache.hitCount;
        touchCacheEntry(cache, key);
        return found->second->valid ? found->second.get() : nullptr;
    }
    ++cache.missCount;

    auto loaded = std::make_unique<LoadedFace>();
    if (face.sourceType() == FontSourceType::FILE) {
        loaded->bytes = readFileBytes(face.path());
    } else if (const std::vector<std::uint8_t> *bytes = face.bytes()) {
        loaded->bytes.assign(bytes->begin(), bytes->end());
    }

    if (!loaded->bytes.empty()) {
        const int fontOffset = stbtt_GetFontOffsetForIndex(loaded->bytes.data(), face.faceIndex());
        loaded->fontOffset = fontOffset >= 0 ? static_cast<std::size_t>(fontOffset) : 0u;
        loaded->stbValid = fontOffset >= 0
            && stbtt_InitFont(&loaded->info, loaded->bytes.data(), fontOffset) != 0;
        loaded->valid = loaded->stbValid;
#if defined(WHATSCANVAS_HAS_FREETYPE)
        if (freeTypeLibrary().valid()) {
            FT_Face ftFace = nullptr;
            if (FT_New_Memory_Face(freeTypeLibrary().library,
                                   loaded->bytes.data(),
                                   static_cast<FT_Long>(loaded->bytes.size()),
                                   static_cast<FT_Long>(face.faceIndex()),
                                   &ftFace) == 0) {
                loaded->ftFace = ftFace;
                loaded->valid = true;
            }
        }
#endif
    }

    LoadedFace *result = loaded.get();
    cache.entries.emplace(key, std::move(loaded));
    touchCacheEntry(cache, key);
    trimCache(cache);
    return result->valid ? result : nullptr;
}

bool FontRasterizer::hasGlyph(const FontFace &face, std::uint32_t codepoint) const
{
    if (face.hasCodepointRanges() && !face.supportsCodepoint(codepoint)) {
        return false;
    }

    const LoadedFace *loaded = loadFace(face);
#if defined(WHATSCANVAS_HAS_FREETYPE)
    if (loaded != nullptr && loaded->ftFace != nullptr) {
        return FT_Get_Char_Index(loaded->ftFace, static_cast<FT_ULong>(codepoint)) != 0;
    }
#endif
    return loaded != nullptr && loaded->stbValid
        && stbtt_FindGlyphIndex(&loaded->info, static_cast<int>(codepoint)) != 0;
}

std::optional<int> FontRasterizer::glyphIndex(const FontFace &face, std::uint32_t codepoint) const
{
    const LoadedFace *loaded = loadFace(face);
    if (loaded == nullptr) {
        return std::nullopt;
    }

#if defined(WHATSCANVAS_HAS_FREETYPE)
    if (loaded->ftFace != nullptr) {
        const FT_UInt index = FT_Get_Char_Index(loaded->ftFace, static_cast<FT_ULong>(codepoint));
        return index == 0 ? std::nullopt : std::optional<int>(static_cast<int>(index));
    }
#endif

    if (!loaded->stbValid) {
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

std::optional<float> FontRasterizer::glyphKerning(const FontFace &face, int leftGlyphIndex, int rightGlyphIndex,
                                                  float pixelSize) const
{
    const LoadedFace *loaded = loadFace(face);
    if (loaded == nullptr || pixelSize <= 0.0f || leftGlyphIndex <= 0 || rightGlyphIndex <= 0) {
        return std::nullopt;
    }

#if defined(WHATSCANVAS_HAS_FREETYPE)
    if (loaded->ftFace != nullptr && FT_HAS_KERNING(loaded->ftFace)
        && setFreeTypePixelSize(loaded->ftFace, pixelSize)) {
        FT_Vector kerning = {};
        if (FT_Get_Kerning(loaded->ftFace,
                           static_cast<FT_UInt>(leftGlyphIndex),
                           static_cast<FT_UInt>(rightGlyphIndex),
                           FT_KERNING_DEFAULT,
                           &kerning) == 0) {
            return static_cast<float>(kerning.x) / 64.0f;
        }
    }
#endif

    if (!loaded->stbValid) {
        return std::nullopt;
    }

    const int advance = stbtt_GetGlyphKernAdvance(&loaded->info, leftGlyphIndex, rightGlyphIndex);
    return static_cast<float>(advance) * stbtt_ScaleForPixelHeight(&loaded->info, pixelSize);
}

std::optional<FontVerticalMetrics> FontRasterizer::verticalMetrics(const FontFace &face, float pixelSize) const
{
    const LoadedFace *loaded = loadFace(face);
    if (loaded == nullptr || pixelSize <= 0.0f) {
        return std::nullopt;
    }

#if defined(WHATSCANVAS_HAS_FREETYPE)
    if (loaded->ftFace != nullptr && setFreeTypePixelSize(loaded->ftFace, pixelSize)) {
        const FT_Size_Metrics &ftMetrics = loaded->ftFace->size->metrics;
        FontVerticalMetrics metrics;
        metrics.ascent = static_cast<float>(ftMetrics.ascender) / 64.0f;
        metrics.descent = static_cast<float>(ftMetrics.descender) / 64.0f;
        metrics.lineHeight = std::max(metrics.ascent - metrics.descent,
                                      static_cast<float>(ftMetrics.height) / 64.0f);
        metrics.lineGap = metrics.lineHeight - (metrics.ascent - metrics.descent);
        return metrics;
    }
#endif

    if (!loaded->stbValid) {
        return std::nullopt;
    }

    int ascent = 0;
    int descent = 0;
    int lineGap = 0;
    stbtt_GetFontVMetrics(&loaded->info, &ascent, &descent, &lineGap);
    const float scale = stbtt_ScaleForPixelHeight(&loaded->info, pixelSize);

    FontVerticalMetrics metrics;
    metrics.ascent = static_cast<float>(ascent) * scale;
    metrics.descent = static_cast<float>(descent) * scale;
    metrics.lineGap = static_cast<float>(lineGap) * scale;
    metrics.lineHeight = metrics.ascent - metrics.descent + metrics.lineGap;
    return metrics;
}

std::optional<GlyphMetrics> FontRasterizer::glyphMetrics(const FontFace &face, std::uint32_t codepoint,
                                                         float pixelSize) const
{
    const LoadedFace *loaded = loadFace(face);
    if (loaded == nullptr || pixelSize <= 0.0f) {
        return std::nullopt;
    }

#if defined(WHATSCANVAS_HAS_FREETYPE)
    if (loaded->ftFace != nullptr && setFreeTypePixelSize(loaded->ftFace, pixelSize)) {
        const FT_UInt index = FT_Get_Char_Index(loaded->ftFace, static_cast<FT_ULong>(codepoint));
        if (index == 0 || FT_Load_Glyph(loaded->ftFace, index, FT_LOAD_DEFAULT) != 0) {
            return std::nullopt;
        }

        GlyphMetrics metrics;
        metrics.glyphIndex = static_cast<int>(index);
        metrics.advanceX = static_cast<float>(loaded->ftFace->glyph->advance.x) / 64.0f;
        return metrics;
    }
#endif

    if (!loaded->stbValid) {
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

    if (loaded->stbValid) {
        if (auto colorGlyph = rasterizeColorGlyph(face, *loaded, glyphIndex, sourceCodepoint, pixelSize)) {
            return colorGlyph;
        }
    }

#if defined(WHATSCANVAS_HAS_FREETYPE)
    if (loaded->ftFace != nullptr && setFreeTypePixelSize(loaded->ftFace, pixelSize)
        && FT_Load_Glyph(loaded->ftFace, static_cast<FT_UInt>(glyphIndex), FT_LOAD_DEFAULT) == 0
        && FT_Render_Glyph(loaded->ftFace->glyph, FT_RENDER_MODE_NORMAL) == 0) {
        const FT_GlyphSlot slot = loaded->ftFace->glyph;
        const FT_Bitmap &ftBitmap = slot->bitmap;

        GlyphBitmap bitmap;
        bitmap.format = GlyphBitmapFormat::Alpha;
        bitmap.width = static_cast<int>(ftBitmap.width);
        bitmap.height = static_cast<int>(ftBitmap.rows);
        bitmap.bearingX = static_cast<float>(slot->bitmap_left);
        bitmap.bearingY = -static_cast<float>(slot->bitmap_top);
        bitmap.advanceX = static_cast<float>(slot->advance.x) / 64.0f;
        bitmap.alphaPixels.resize(static_cast<std::size_t>(std::max(0, bitmap.width))
                                  * static_cast<std::size_t>(std::max(0, bitmap.height)));

        if (bitmap.width > 0 && bitmap.height > 0) {
            for (int row = 0; row < bitmap.height; ++row) {
                for (int col = 0; col < bitmap.width; ++col) {
                    const std::size_t dst = static_cast<std::size_t>(row) * static_cast<std::size_t>(bitmap.width)
                        + static_cast<std::size_t>(col);
                    const unsigned char *src = ftBitmap.buffer
                        + static_cast<std::ptrdiff_t>(row) * ftBitmap.pitch
                        + static_cast<std::ptrdiff_t>(col);
                    bitmap.alphaPixels[dst] = ftBitmap.pixel_mode == FT_PIXEL_MODE_GRAY ? *src : (*src != 0 ? 255 : 0);
                }
            }
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
#endif

    if (!loaded->stbValid) {
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

std::optional<RasterizedGlyph> FontRasterizer::rasterizeColorGlyph(const FontFace &face,
                                                                   const LoadedFace &loaded,
                                                                   int glyphIndex,
                                                                   std::uint32_t sourceCodepoint,
                                                                   float pixelSize) const
{
    const auto colr = findSfntTable(loaded.bytes, loaded.fontOffset, "COLR");
    const auto cpal = findSfntTable(loaded.bytes, loaded.fontOffset, "CPAL");
    if (!colr || !cpal) {
        return std::nullopt;
    }

    const auto layers = findColrLayers(*colr, glyphIndex);
    if (!layers) {
        return std::nullopt;
    }

    const float scale = stbtt_ScaleForPixelHeight(&loaded.info, pixelSize);
    int advance = 0;
    int leftBearing = 0;
    stbtt_GetGlyphHMetrics(&loaded.info, glyphIndex, &advance, &leftBearing);

    int unionLeft = 0;
    int unionTop = 0;
    int unionRight = 0;
    int unionBottom = 0;
    bool hasBounds = false;
    for (const ColorLayer &layer : *layers) {
        int x0 = 0;
        int y0 = 0;
        int x1 = 0;
        int y1 = 0;
        stbtt_GetGlyphBitmapBox(&loaded.info, layer.glyphIndex, scale, scale, &x0, &y0, &x1, &y1);
        if (x1 <= x0 || y1 <= y0) {
            continue;
        }
        if (!hasBounds) {
            unionLeft = x0;
            unionTop = y0;
            unionRight = x1;
            unionBottom = y1;
            hasBounds = true;
        } else {
            unionLeft = std::min(unionLeft, x0);
            unionTop = std::min(unionTop, y0);
            unionRight = std::max(unionRight, x1);
            unionBottom = std::max(unionBottom, y1);
        }
    }

    if (!hasBounds) {
        return std::nullopt;
    }

    GlyphBitmap bitmap;
    bitmap.format = GlyphBitmapFormat::RGBA;
    bitmap.width = unionRight - unionLeft;
    bitmap.height = unionBottom - unionTop;
    bitmap.bearingX = static_cast<float>(unionLeft);
    bitmap.bearingY = static_cast<float>(unionTop);
    bitmap.advanceX = static_cast<float>(advance) * scale;
    bitmap.rgbaPixels.resize(static_cast<std::size_t>(bitmap.width) * static_cast<std::size_t>(bitmap.height) * 4u);

    bool painted = false;
    for (const ColorLayer &layer : *layers) {
        const auto color = cpalColor(*cpal, layer.paletteIndex);
        if (!color) {
            continue;
        }

        int x0 = 0;
        int y0 = 0;
        int x1 = 0;
        int y1 = 0;
        stbtt_GetGlyphBitmapBox(&loaded.info, layer.glyphIndex, scale, scale, &x0, &y0, &x1, &y1);
        const int layerWidth = x1 - x0;
        const int layerHeight = y1 - y0;
        if (layerWidth <= 0 || layerHeight <= 0) {
            continue;
        }

        std::vector<unsigned char> coverage(static_cast<std::size_t>(layerWidth) * static_cast<std::size_t>(layerHeight));
        stbtt_MakeGlyphBitmap(&loaded.info, coverage.data(), layerWidth, layerHeight,
                              layerWidth, scale, scale, layer.glyphIndex);

        for (int y = 0; y < layerHeight; ++y) {
            for (int x = 0; x < layerWidth; ++x) {
                const std::size_t coverageIndex = static_cast<std::size_t>(y) * static_cast<std::size_t>(layerWidth)
                    + static_cast<std::size_t>(x);
                const int dstX = x0 - unionLeft + x;
                const int dstY = y0 - unionTop + y;
                const std::size_t dstIndex = (static_cast<std::size_t>(dstY) * static_cast<std::size_t>(bitmap.width)
                    + static_cast<std::size_t>(dstX)) * 4u;
                compositePixel(bitmap.rgbaPixels.data() + dstIndex, *color, coverage[coverageIndex]);
                painted = painted || coverage[coverageIndex] != 0;
            }
        }
    }

    if (!painted) {
        return std::nullopt;
    }

    RasterizedGlyph glyph;
    glyph.key.fontFamily = face.family();
    glyph.key.codepoint = sourceCodepoint;
    glyph.key.glyphIndex = glyphIndex;
    glyph.key.pixelSize = pixelSize;
    glyph.key.format = GlyphBitmapFormat::RGBA;
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
    return detectColorFontTables(*data, face.faceIndex());
}

} // namespace wsc::text

#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

#include "text/GlyphAtlas.h"

namespace wsc {
class FontFace;
class Paint;
}

namespace wsc::text {

struct RasterizedGlyph
{
    GlyphKey key;
    GlyphBitmap bitmap;
};

struct GlyphMetrics
{
    int glyphIndex = 0;
    float advanceX = 0.0f;
};

struct FontVerticalMetrics
{
    float ascent = 0.0f;
    float descent = 0.0f;
    float lineGap = 0.0f;
    float lineHeight = 0.0f;
};

struct FontDataView
{
    const unsigned char *data = nullptr;
    std::size_t size = 0;
    int faceIndex = 0;
};

std::string fontFaceIdentity(const FontFace &face);

struct ColorFontTables
{
    bool colr = false;
    bool cpal = false;
    bool cbdt = false;
    bool cblc = false;
    bool sbix = false;
    bool svg = false;

    bool hasAny() const
    {
        return colr || cpal || cbdt || cblc || sbix || svg;
    }
};

ColorFontTables detectColorFontTables(FontDataView fontData, int faceIndex = 0);

struct FontRasterizerCacheStats
{
    std::size_t faceCount = 0;
    std::size_t capacity = 0;
    std::size_t hitCount = 0;
    std::size_t missCount = 0;
    std::size_t evictionCount = 0;
};

class FontRasterizer
{
public:
    bool hasGlyph(const FontFace &face, std::uint32_t codepoint) const;
    std::optional<int> glyphIndex(const FontFace &face, std::uint32_t codepoint) const;
    std::optional<float> glyphAdvance(const FontFace &face, std::uint32_t codepoint,
                                      float pixelSize) const;
    std::optional<float> glyphKerning(const FontFace &face, int leftGlyphIndex, int rightGlyphIndex,
                                      float pixelSize) const;
    std::optional<FontVerticalMetrics> verticalMetrics(const FontFace &face, float pixelSize) const;
    std::optional<GlyphMetrics> glyphMetrics(const FontFace &face, std::uint32_t codepoint,
                                             float pixelSize) const;
    std::optional<RasterizedGlyph> rasterizeGlyph(const FontFace &face, std::uint32_t codepoint,
                                                  float pixelSize) const;
    std::optional<RasterizedGlyph> rasterizeGlyphIndex(const FontFace &face, int glyphIndex,
                                                       std::uint32_t sourceCodepoint,
                                                       float pixelSize) const;
    // Returns a thread-local snapshot that remains valid until the next fontData call on the same thread.
    std::optional<FontDataView> fontData(const FontFace &face) const;
    std::optional<ColorFontTables> colorFontTables(const FontFace &face) const;
    FontRasterizerCacheStats cacheStats() const;
    void clearCache() const;
    void setCacheCapacity(std::size_t capacity) const;

private:
    struct LoadedFace;
    struct CacheState;

    static CacheState &cacheState();
    static std::mutex &cacheMutex();
    static void touchCacheEntry(CacheState &cache, const std::string &key);
    static void trimCache(CacheState &cache);
    const LoadedFace *loadFace(const FontFace &face) const;
    std::optional<RasterizedGlyph> rasterizeColorGlyph(const FontFace &face,
                                                       const LoadedFace &loaded,
                                                       int glyphIndex,
                                                       std::uint32_t sourceCodepoint,
                                                       float pixelSize) const;
};

} // namespace wsc::text

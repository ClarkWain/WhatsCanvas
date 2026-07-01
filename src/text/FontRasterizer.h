#pragma once

#include <cstddef>
#include <cstdint>
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

struct FontDataView
{
    const unsigned char *data = nullptr;
    std::size_t size = 0;
};

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

ColorFontTables detectColorFontTables(FontDataView fontData);

class FontRasterizer
{
public:
    bool hasGlyph(const FontFace &face, std::uint32_t codepoint) const;
    std::optional<int> glyphIndex(const FontFace &face, std::uint32_t codepoint) const;
    std::optional<float> glyphAdvance(const FontFace &face, std::uint32_t codepoint,
                                      float pixelSize) const;
    std::optional<GlyphMetrics> glyphMetrics(const FontFace &face, std::uint32_t codepoint,
                                             float pixelSize) const;
    std::optional<RasterizedGlyph> rasterizeGlyph(const FontFace &face, std::uint32_t codepoint,
                                                  float pixelSize) const;
    std::optional<RasterizedGlyph> rasterizeGlyphIndex(const FontFace &face, int glyphIndex,
                                                       std::uint32_t sourceCodepoint,
                                                       float pixelSize) const;
    std::optional<FontDataView> fontData(const FontFace &face) const;
    std::optional<ColorFontTables> colorFontTables(const FontFace &face) const;

private:
    struct LoadedFace;

    const LoadedFace *loadFace(const FontFace &face) const;
};

} // namespace wsc::text

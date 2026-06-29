#pragma once

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

class FontRasterizer
{
public:
    bool hasGlyph(const FontFace &face, std::uint32_t codepoint) const;
    std::optional<float> glyphAdvance(const FontFace &face, std::uint32_t codepoint,
                                      float pixelSize) const;
    std::optional<RasterizedGlyph> rasterizeGlyph(const FontFace &face, std::uint32_t codepoint,
                                                  float pixelSize) const;

private:
    struct LoadedFace;

    const LoadedFace *loadFace(const FontFace &face) const;
};

} // namespace wsc::text

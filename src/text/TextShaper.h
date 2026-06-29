#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace wsc::text {

struct ShapedGlyph
{
    std::uint32_t codepoint = 0;
    int glyphIndex = 0;
    std::size_t sourceStart = 0;
    std::size_t sourceLength = 0;
    float advanceX = 0.0f;
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    bool visible = false;
};

struct ShapedTextRun
{
    std::vector<ShapedGlyph> glyphs;
    float width = 0.0f;
    bool rightToLeft = false;
};

struct ResolvedGlyph
{
    int glyphIndex = 0;
    float advanceX = 0.0f;
};

using GlyphResolver = std::function<std::optional<ResolvedGlyph>(std::uint32_t codepoint)>;

std::optional<ShapedTextRun> shapeTextSimple(const std::string &normalizedText,
                                             float letterSpacing,
                                             const GlyphResolver &glyphResolver);

} // namespace wsc::text

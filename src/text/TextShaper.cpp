#include "text/TextShaper.h"

#include <algorithm>
#include <cmath>

#include "text/TextUtils.h"

namespace wsc::text {

namespace {

bool isRightToLeftCodepoint(std::uint32_t codepoint)
{
    return (codepoint >= 0x0590 && codepoint <= 0x08FF)
        || (codepoint >= 0xFB1D && codepoint <= 0xFDFF)
        || (codepoint >= 0xFE70 && codepoint <= 0xFEFF);
}

bool isStrongLeftToRightCodepoint(std::uint32_t codepoint)
{
    return (codepoint >= 'A' && codepoint <= 'Z')
        || (codepoint >= 'a' && codepoint <= 'z')
        || (codepoint >= 0x0041 && codepoint <= 0x02AF)
        || (codepoint >= 0x0370 && codepoint <= 0x058F)
        || (codepoint >= 0x1E00 && codepoint <= 0x1EFF);
}

bool firstStrongDirectionIsRightToLeft(const std::vector<Utf8Codepoint> &codepoints)
{
    for (const Utf8Codepoint &codepoint : codepoints) {
        if (isRightToLeftCodepoint(codepoint.value)) {
            return true;
        }
        if (isStrongLeftToRightCodepoint(codepoint.value)) {
            return false;
        }
    }
    return false;
}

} // namespace

std::optional<ShapedTextRun> shapeTextSimple(const std::string &normalizedText,
                                             float letterSpacing,
                                             const GlyphResolver &glyphResolver)
{
    if (!glyphResolver) {
        return std::nullopt;
    }

    const float spacing = std::isfinite(letterSpacing) ? letterSpacing : 0.0f;
    const std::vector<Utf8Codepoint> codepoints = decodeUtf8(normalizedText);
    ShapedTextRun run;
    run.rightToLeft = firstStrongDirectionIsRightToLeft(codepoints);
    bool hasVisibleGlyph = false;

    for (const Utf8Codepoint &codepoint : codepoints) {
        if (codepoint.value == '\n') {
            break;
        }
        if (codepoint.value < 32) {
            continue;
        }

        const std::optional<ResolvedGlyph> resolved = glyphResolver(codepoint.value);
        if (!resolved) {
            return std::nullopt;
        }

        if (hasVisibleGlyph) {
            run.width += spacing;
        }

        ShapedGlyph glyph;
        glyph.codepoint = codepoint.value;
        glyph.glyphIndex = resolved->glyphIndex;
        glyph.sourceStart = codepoint.offset;
        glyph.sourceLength = codepoint.length;
        glyph.advanceX = std::max(0.0f, resolved->advanceX);
        glyph.visible = true;
        run.glyphs.push_back(glyph);
        run.width += glyph.advanceX;
        hasVisibleGlyph = true;
    }

    if (!hasVisibleGlyph) {
        return std::nullopt;
    }

    run.width = std::max(0.0f, run.width);
    if (run.rightToLeft) {
        std::reverse(run.glyphs.begin(), run.glyphs.end());
    }
    return run;
}

} // namespace wsc::text

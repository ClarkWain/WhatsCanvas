#include "text/TextShaper.h"

#include <algorithm>
#include <cmath>

#include "text/TextUtils.h"

namespace wsc::text {

std::optional<ShapedTextRun> shapeTextSimple(const std::string &normalizedText,
                                             float letterSpacing,
                                             const GlyphAdvanceResolver &advanceResolver)
{
    if (!advanceResolver) {
        return std::nullopt;
    }

    const float spacing = std::isfinite(letterSpacing) ? letterSpacing : 0.0f;
    ShapedTextRun run;
    bool hasVisibleGlyph = false;

    for (const Utf8Codepoint &codepoint : decodeUtf8(normalizedText)) {
        if (codepoint.value == '\n') {
            break;
        }
        if (codepoint.value < 32) {
            continue;
        }

        const std::optional<float> advance = advanceResolver(codepoint.value);
        if (!advance) {
            return std::nullopt;
        }

        if (hasVisibleGlyph) {
            run.width += spacing;
        }

        ShapedGlyph glyph;
        glyph.codepoint = codepoint.value;
        glyph.sourceStart = codepoint.offset;
        glyph.sourceLength = codepoint.length;
        glyph.advanceX = std::max(0.0f, *advance);
        glyph.visible = true;
        run.glyphs.push_back(glyph);
        run.width += glyph.advanceX;
        hasVisibleGlyph = true;
    }

    if (!hasVisibleGlyph) {
        return std::nullopt;
    }

    run.width = std::max(0.0f, run.width);
    return run;
}

} // namespace wsc::text

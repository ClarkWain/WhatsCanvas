#include "text/TextShaper.h"

#include <algorithm>
#include <cmath>

#include "text/TextUtils.h"

namespace wsc::text {

#if defined(WHATSCANVAS_HAS_HARFBUZZ)
std::unique_ptr<ITextShapingEngine> createHarfBuzzTextShapingEngine();
#endif

namespace {

class SimpleTextShapingEngine final : public ITextShapingEngine
{
public:
    TextShapingBackend backend() const override
    {
        return TextShapingBackend::Simple;
    }

    const char *name() const override
    {
        return "simple";
    }

    bool supportsOpenTypeFeatures() const override
    {
        return false;
    }

    std::optional<ShapedTextRun> shape(const TextShapeInput &input,
                                       const GlyphResolver &glyphResolver) const override
    {
        return shapeTextSimple(input.normalizedText, input.letterSpacing, glyphResolver);
    }
};

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

std::optional<bool> strongDirectionForCodepoint(std::uint32_t codepoint)
{
    if (isRightToLeftCodepoint(codepoint)) {
        return true;
    }
    if (isStrongLeftToRightCodepoint(codepoint)) {
        return false;
    }
    return std::nullopt;
}

std::uint32_t mirroredCodepointForRightToLeftRun(std::uint32_t codepoint)
{
    switch (codepoint) {
    case '(': return ')';
    case ')': return '(';
    case '<': return '>';
    case '>': return '<';
    case '[': return ']';
    case ']': return '[';
    case '{': return '}';
    case '}': return '{';
    case 0x00AB: return 0x00BB;
    case 0x00BB: return 0x00AB;
    case 0x2039: return 0x203A;
    case 0x203A: return 0x2039;
    case 0x2045: return 0x2046;
    case 0x2046: return 0x2045;
    case 0x207D: return 0x207E;
    case 0x207E: return 0x207D;
    case 0x208D: return 0x208E;
    case 0x208E: return 0x208D;
    case 0x3008: return 0x3009;
    case 0x3009: return 0x3008;
    case 0x300A: return 0x300B;
    case 0x300B: return 0x300A;
    case 0x300C: return 0x300D;
    case 0x300D: return 0x300C;
    case 0x300E: return 0x300F;
    case 0x300F: return 0x300E;
    case 0x3010: return 0x3011;
    case 0x3011: return 0x3010;
    case 0x3014: return 0x3015;
    case 0x3015: return 0x3014;
    case 0x3016: return 0x3017;
    case 0x3017: return 0x3016;
    case 0x3018: return 0x3019;
    case 0x3019: return 0x3018;
    case 0x301A: return 0x301B;
    case 0x301B: return 0x301A;
    default: return codepoint;
    }
}

} // namespace

bool isBidiControlCodepoint(std::uint32_t codepoint)
{
    return codepoint == 0x061C
        || (codepoint >= 0x200E && codepoint <= 0x200F)
        || (codepoint >= 0x202A && codepoint <= 0x202E)
        || (codepoint >= 0x2066 && codepoint <= 0x2069);
}

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
        if (codepoint.value < 32 || isBidiControlCodepoint(codepoint.value)) {
            continue;
        }

        const std::uint32_t shapedCodepoint =
            run.rightToLeft ? mirroredCodepointForRightToLeftRun(codepoint.value) : codepoint.value;
        const std::optional<ResolvedGlyph> resolved = glyphResolver(shapedCodepoint);
        if (!resolved) {
            return std::nullopt;
        }

        if (hasVisibleGlyph) {
            run.width += spacing;
        }

        ShapedGlyph glyph;
        glyph.codepoint = shapedCodepoint;
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

std::vector<BidiRun> segmentBidiRuns(const std::string &normalizedText)
{
    std::vector<BidiRun> runs;
    const std::vector<Utf8Codepoint> codepoints = decodeUtf8(normalizedText);
    std::optional<bool> currentDirection;
    std::size_t currentStart = std::string::npos;
    std::size_t currentEnd = 0;
    std::size_t visibleStart = std::string::npos;
    std::size_t visibleEnd = 0;

    const auto finishCurrent = [&]() {
        if (currentDirection && currentStart != std::string::npos && currentEnd > currentStart) {
            runs.push_back({currentStart, currentEnd, *currentDirection});
        }
    };

    for (const Utf8Codepoint &codepoint : codepoints) {
        if (codepoint.value == '\n') {
            break;
        }
        if (codepoint.value < 32 || isBidiControlCodepoint(codepoint.value)) {
            continue;
        }

        if (visibleStart == std::string::npos) {
            visibleStart = codepoint.offset;
        }
        visibleEnd = codepoint.offset + codepoint.length;

        const std::optional<bool> direction = strongDirectionForCodepoint(codepoint.value);
        if (!direction) {
            if (currentDirection) {
                currentEnd = codepoint.offset + codepoint.length;
            } else if (currentStart == std::string::npos) {
                currentStart = codepoint.offset;
            }
            continue;
        }

        if (!currentDirection) {
            currentDirection = direction;
            if (currentStart == std::string::npos) {
                currentStart = visibleStart;
            }
        } else if (*direction != *currentDirection) {
            finishCurrent();
            currentDirection = direction;
            currentStart = codepoint.offset;
        }
        currentEnd = codepoint.offset + codepoint.length;
    }

    finishCurrent();
    if (runs.empty() && visibleStart != std::string::npos && visibleEnd > visibleStart) {
        runs.push_back({visibleStart, visibleEnd, false});
    }
    return runs;
}

bool isOpenTypeShapingAvailable()
{
#if defined(WHATSCANVAS_HAS_HARFBUZZ)
    return true;
#else
    return false;
#endif
}

std::unique_ptr<ITextShapingEngine> createSimpleTextShapingEngine()
{
    return std::make_unique<SimpleTextShapingEngine>();
}

std::unique_ptr<ITextShapingEngine> createOpenTypeTextShapingEngine()
{
#if defined(WHATSCANVAS_HAS_HARFBUZZ)
    return createHarfBuzzTextShapingEngine();
#else
    return nullptr;
#endif
}

std::unique_ptr<ITextShapingEngine> createTextShapingEngine(TextShapingBackend backend)
{
    if (backend == TextShapingBackend::OpenType) {
        if (auto shaper = createOpenTypeTextShapingEngine()) {
            return shaper;
        }
    }
    return createSimpleTextShapingEngine();
}

} // namespace wsc::text

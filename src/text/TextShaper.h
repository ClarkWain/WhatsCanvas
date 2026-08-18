#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "text/FontRasterizer.h"
#include "wsc/Font.h"

namespace wsc::text {

enum class TextDirection
{
    Auto,
    LeftToRight,
    RightToLeft
};

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
    const wsc::FontFace *fontFace = nullptr;
};

struct ShapedTextRun
{
    std::vector<ShapedGlyph> glyphs;
    float width = 0.0f;
    bool rightToLeft = false;
};

struct BidiRun
{
    std::size_t sourceStart = 0;
    std::size_t sourceEnd = 0;
    bool rightToLeft = false;
};

struct ResolvedGlyph
{
    int glyphIndex = 0;
    float advanceX = 0.0f;
};

struct OpenTypeFeature
{
    std::string tag;
    std::uint32_t value = 1;
};

using GlyphResolver = std::function<std::optional<ResolvedGlyph>(std::uint32_t codepoint)>;

struct TextShapeInput
{
    std::string normalizedText;
    float letterSpacing = 0.0f;
    float pixelSize = 0.0f;
    std::string language;
    TextDirection direction = TextDirection::Auto;
    std::vector<OpenTypeFeature> openTypeFeatures;
    std::vector<wsc::FontVariationCoordinate> variationCoordinates;
    std::optional<FontDataView> fontData;
};

enum class TextShapingBackend
{
    Simple,
    OpenType
};

class ITextShapingEngine
{
public:
    virtual ~ITextShapingEngine() = default;

    virtual TextShapingBackend backend() const = 0;
    virtual const char *name() const = 0;
    virtual bool supportsOpenTypeFeatures() const = 0;
    virtual std::optional<ShapedTextRun> shape(const TextShapeInput &input,
                                               const GlyphResolver &glyphResolver) const = 0;
};

std::optional<ShapedTextRun> shapeTextSimple(const std::string &normalizedText,
                                             float letterSpacing,
                                             const GlyphResolver &glyphResolver);

bool isBidiControlCodepoint(std::uint32_t codepoint);
std::vector<BidiRun> segmentBidiRuns(const std::string &normalizedText);

bool isOpenTypeShapingAvailable();
std::unique_ptr<ITextShapingEngine> createSimpleTextShapingEngine();
std::unique_ptr<ITextShapingEngine> createOpenTypeTextShapingEngine();
std::unique_ptr<ITextShapingEngine> createTextShapingEngine(TextShapingBackend backend);

} // namespace wsc::text

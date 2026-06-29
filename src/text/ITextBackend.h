#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "canvas/base.h"

class Paint;

namespace wsc {
class FontFace;
class FontFallbackChain;
}

namespace wsc::text {

enum class TextRenderKind {
    None,
    Geometry,
    Bitmap
};

struct TextRenderResult
{
    TextRenderKind kind = TextRenderKind::None;
    float drawX = 0.0f;
    float drawY = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    int bitmapWidth = 0;
    int bitmapHeight = 0;
    std::vector<float> vertices;
    std::vector<unsigned char> bitmapPixels;
};

struct TextLineBreak
{
    std::size_t sourceStart = 0;
    std::size_t sourceLength = 0;
    float width = 0.0f;
};

struct TextBackendDiagnostic
{
    enum class Severity
    {
        Info,
        Warning,
        Error
    };

    Severity severity = Severity::Info;
    std::string message;
    std::uint32_t codepoint = 0;
    std::string fontFamily;
};

class ITextBackend
{
public:
    virtual ~ITextBackend() = default;

    virtual bool registerFontFace(const FontFace &face) = 0;
    virtual bool setFontFallbackChain(const FontFallbackChain &chain) = 0;
    virtual std::vector<std::string> resolveFontFamilies(const std::string &preferredFamily) const = 0;
    virtual std::vector<TextLineBreak> breakLines(const std::string &text, float maxWidth, const Paint &paint) const = 0;
    virtual bool hasGlyphForCodepoint(std::uint32_t codepoint, const Paint &paint) const = 0;
    virtual std::vector<TextBackendDiagnostic> diagnostics() const = 0;
    virtual float measureTextWidth(const std::string &text, const Paint &paint) const = 0;
    virtual RectF measureTextBounds(const std::string &text, const Paint &paint) const = 0;
    virtual TextRenderResult renderText(const std::string &text, float x, float y, const Paint &paint) const = 0;
};

} // namespace wsc::text

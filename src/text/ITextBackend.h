#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "canvas/base.h"

namespace wsc {
class Paint;
class FontFace;
class FontFallbackChain;
}

namespace wsc::text {

enum class TextRenderKind {
    None,
    Geometry,
    Bitmap,
    GlyphAtlas
};

enum class GlyphAtlasPixelFormat {
    Alpha,
    RGBA
};

struct TextRenderResult
{
    struct MissingGlyph
    {
        std::uint32_t codepoint = 0;
        std::size_t sourceStart = 0;
        std::size_t sourceLength = 0;
    };

    struct GlyphAtlasQuad
    {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        float u0 = 0.0f;
        float v0 = 0.0f;
        float u1 = 0.0f;
        float v1 = 0.0f;
    };

    struct GlyphAtlasDirtyRect
    {
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
    };

    TextRenderKind kind = TextRenderKind::None;
    float drawX = 0.0f;
    float drawY = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    int bitmapWidth = 0;
    int bitmapHeight = 0;
    // True when bitmapPixels contains an LCD/ClearType RGB coverage mask rather
    // than ordinary premultiplied/alpha image pixels.  It must only be
    // composited onto an opaque, axis-aligned destination using the dedicated
    // per-channel blend path; treating it as a normal RGBA image loses the
    // subpixel coverage.
    bool bitmapIsClearType = false;
    std::vector<float> vertices;
    std::vector<unsigned char> bitmapPixels;
    int atlasWidth = 0;
    int atlasHeight = 0;
    GlyphAtlasPixelFormat atlasPixelFormat = GlyphAtlasPixelFormat::Alpha;
    // Backends that retain their glyph atlas may expose a non-owning view
    // instead of copying a multi-megabyte texture for every drawText call.
    // The backend owns these vectors and guarantees they outlive this result.
    const std::vector<unsigned char> *atlasAlphaPixelsView = nullptr;
    const std::vector<unsigned char> *atlasRgbaPixelsView = nullptr;
    // Monotonically changes whenever the viewed atlas contents change. A
    // renderer can then skip re-hashing the complete atlas for every label.
    std::uint64_t atlasRevision = 0;
    std::vector<unsigned char> atlasAlphaPixels;
    std::vector<unsigned char> atlasRgbaPixels;
    std::vector<GlyphAtlasDirtyRect> atlasDirtyRects;
    std::vector<GlyphAtlasQuad> glyphAtlasQuads;
    std::vector<MissingGlyph> missingGlyphs;
};

struct TextLineBreak
{
    std::size_t sourceStart = 0;
    std::size_t sourceLength = 0;
    float width = 0.0f;
};

struct TextMetrics
{
    float width = 0.0f;
    float height = 0.0f;
    float top = 0.0f;
    float bottom = 0.0f;
    float ascent = 0.0f;
    float descent = 0.0f;
    float lineGap = 0.0f;
    float lineHeight = 0.0f;
    RectF bounds;
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
    virtual TextMetrics measureTextMetrics(const std::string &text, const Paint &paint) const = 0;
    virtual TextRenderResult renderText(const std::string &text, float x, float y, const Paint &paint) const = 0;
};

} // namespace wsc::text

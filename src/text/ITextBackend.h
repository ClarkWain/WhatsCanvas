#pragma once

#include <string>
#include <vector>

#include "canvas/base.h"

class Paint;

namespace prismcanvas::text {

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

class ITextBackend
{
public:
    virtual ~ITextBackend() = default;

    virtual float measureTextWidth(const std::string &text, const Paint &paint) const = 0;
    virtual RectF measureTextBounds(const std::string &text, const Paint &paint) const = 0;
    virtual TextRenderResult renderText(const std::string &text, float x, float y, const Paint &paint) const = 0;
};

} // namespace prismcanvas::text
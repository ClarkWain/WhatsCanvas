#pragma once

#include <string>

#include "Export.h"

namespace wsc {

/// RGBA color value used by drawing operations.
class WSC_API Color
{
public:
    static const Color RED;
    static const Color GREEN;
    static const Color BLUE;
    static const Color WHITE;
    static const Color BLACK;
    static const Color YELLOW;
    static const Color CYAN;
    static const Color MAGENTA;

    Color();
    Color(int r, int g, int b, int a = 255);

    /// Construct from float RGBA values in [0.0, 1.0] range.
    /// Values are clamped to [0, 1] and converted to 0-255 integers.
    Color(float r, float g, float b, float a = 1.0f);

    /// Factory from float RGBA values in [0.0, 1.0] range.
    static Color fromFloat(float r, float g, float b, float a = 1.0f);

    static Color fromHex(const std::string &hex);

    int getR() const;
    int getG() const;
    int getB() const;
    int getA() const;

    float a() const;
    float r() const;
    float g() const;
    float b() const;
    void getNormalized(float *rgba) const;

private:
    int r_;
    int g_;
    int b_;
    int a_;
};

} // namespace wsc

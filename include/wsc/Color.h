#pragma once

#include <string>

#include "Export.h"

namespace wsc {

/// Straight-alpha sRGB RGBA value used by drawing operations.
///
/// Integer channels are represented on the 0-255 scale and float accessors on
/// the normalized 0-1 scale. Float constructors clamp; the integer constructor
/// expects caller-provided 0-255 values and stores them verbatim.
class WSC_API Color
{
public:
    /// Predefined opaque colors.
    static const Color RED;
    static const Color GREEN;
    static const Color BLUE;
    static const Color WHITE;
    static const Color BLACK;
    static const Color YELLOW;
    static const Color CYAN;
    static const Color MAGENTA;

    /// Default-constructs opaque black.
    Color();

    /// Construct from 0-255 integer channels.
    Color(int r, int g, int b, int a = 255);

    /// Construct from float RGBA values in [0.0, 1.0] range.
    /// Values are clamped to [0, 1] and converted to 0-255 integers.
    Color(float r, float g, float b, float a = 1.0f);

    /// Factory from float RGBA values in [0.0, 1.0] range.
    static Color fromFloat(float r, float g, float b, float a = 1.0f);

    /// Parse `#RRGGBB` or `#RRGGBBAA` (alpha last).
    /// @throws std::invalid_argument when the string length is not 7 or 9.
    static Color fromHex(const std::string &hex);

    /// Channel accessors as 0-255 integers.
    int getR() const;

    int getG() const;

    int getB() const;

    int getA() const;

    /// Channel accessors as normalized [0.0, 1.0] floats.
    float a() const;

    float r() const;

    float g() const;

    float b() const;

    /// Write normalized channels in RGBA order. `rgba` must point to at least
    /// four writable floats.
    void getNormalized(float *rgba) const;

private:
    int r_;
    int g_;
    int b_;
    int a_;
};

} // namespace wsc

#pragma once

#include <cmath>
#include <algorithm>

/// Utilities for gamma-correct color rendering.
/// When gamma correction is enabled, all color values are converted
/// from sRGB to linear space before blending, then back to sRGB
/// for display. This produces visually correct alpha blending.
namespace GammaCorrect {

/// Global gamma correction switch.
/// When true, colors are converted sRGB → linear before blending.
inline bool &enabled()
{
    static bool flag = false;
    return flag;
}

/// Convert a single sRGB channel value to linear.
/// Uses the standard sRGB transfer function.
inline float srgbToLinear(float s)
{
    if (s <= 0.0f) return 0.0f;
    if (s >= 1.0f) return 1.0f;
    if (s <= 0.04045f) {
        return s / 12.92f;
    }
    return std::pow((s + 0.055f) / 1.055f, 2.4f);
}

/// Convert a single linear channel value to sRGB.
inline float linearToSrgb(float l)
{
    if (l <= 0.0f) return 0.0f;
    if (l >= 1.0f) return 1.0f;
    if (l <= 0.0031308f) {
        return l * 12.92f;
    }
    return 1.055f * std::pow(l, 1.0f / 2.4f) - 0.055f;
}

/// Convert RGBA from sRGB to linear in-place.
/// Each component is in [0.0, 1.0] range.
inline void srgbToLinear4(float *rgba)
{
    if (!enabled()) return;
    rgba[0] = srgbToLinear(rgba[0]);
    rgba[1] = srgbToLinear(rgba[1]);
    rgba[2] = srgbToLinear(rgba[2]);
    // Alpha is already in linear space — don't convert.
}

/// Convert RGBA from linear to sRGB in-place.
inline void linearToSrgb4(float *rgba)
{
    if (!enabled()) return;
    rgba[0] = linearToSrgb(rgba[0]);
    rgba[1] = linearToSrgb(rgba[1]);
    rgba[2] = linearToSrgb(rgba[2]);
    // Alpha is already in linear space — don't convert.
}

} // namespace GammaCorrect

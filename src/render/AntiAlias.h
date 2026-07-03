#pragma once

/// Tuning constants for analytic (geometry-based) anti-aliasing.
///
/// Anti-aliasing itself is controlled per draw through Paint::isAntiAlias(),
/// mirroring the Skia/Android-Canvas convention. When a paint opts in, filled
/// and stroked geometry is expanded with a feathered fringe whose coverage
/// ramps from 1.0 at the true edge to 0.0 one device pixel outside; the
/// fragment shader multiplies the output alpha by that interpolated coverage,
/// producing resolution-independent edge smoothing that does not rely on MSAA.
namespace AntiAlias {

/// Width of the anti-aliasing feather, expressed in device pixels.
inline float featherWidthPixels()
{
    return 1.0f;
}

} // namespace AntiAlias

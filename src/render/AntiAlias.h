#pragma once

/// Tuning constants for analytic (geometry-based) anti-aliasing.
///
/// Anti-aliasing itself is controlled per draw through Paint::isAntiAlias(),
/// matching the per-paint convention used by Android Canvas and other 2D APIs.
/// When a paint opts in, filled
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

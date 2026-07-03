#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace wsc::render {

/// A normalized 1-D Gaussian kernel for separable blur.
///
/// Only the non-negative half is stored: `weights[0]` is the centre tap and
/// `weights[i]` is the weight applied to the taps at offset +/- i. The weights
/// are normalized so a full symmetric convolution sums to 1
/// (`weights[0] + 2 * sum(weights[1..radius]) == 1`), which keeps a blurred
/// image's brightness unchanged.
struct GaussianKernel
{
    std::vector<float> weights; // index 0 = centre, index i = both taps at +/- i

    int radius() const { return static_cast<int>(weights.size()) - 1; }
};

/// Builds a Gaussian kernel whose effective reach is `radiusPixels`. The
/// standard deviation is derived as radius/3 so the taps cover ~3 sigma, where
/// the Gaussian has decayed to a negligible weight. A radius <= 0 yields a
/// pass-through kernel (single unit weight).
inline GaussianKernel computeGaussianKernel(float radiusPixels)
{
    GaussianKernel kernel;

    const int radius = static_cast<int>(std::lround(std::max(0.0f, radiusPixels)));
    if (radius <= 0) {
        kernel.weights.push_back(1.0f);
        return kernel;
    }

    const float sigma = std::max(static_cast<float>(radius) / 3.0f, 1e-4f);
    const float twoSigmaSq = 2.0f * sigma * sigma;

    kernel.weights.resize(static_cast<std::size_t>(radius) + 1);
    float total = 0.0f;
    for (int i = 0; i <= radius; ++i) {
        const float w = std::exp(-static_cast<float>(i) * static_cast<float>(i) / twoSigmaSq);
        kernel.weights[static_cast<std::size_t>(i)] = w;
        total += (i == 0) ? w : 2.0f * w; // side taps are applied on both sides
    }

    const float inv = total > 0.0f ? 1.0f / total : 1.0f;
    for (float &w : kernel.weights) {
        w *= inv;
    }
    return kernel;
}

} // namespace wsc::render

#pragma once
#include "text/GlyphAtlas.h"
#include "render/GaussianKernel.h"
#include <cmath>
#include <utility>

namespace wsc::text {
constexpr int kMaxGlyphMaskBlurRadius = 64;

// Apply the same finite Gaussian kernel as image filters to an alpha glyph,
// once on cache miss. Keep advance unchanged and expand bearings for the halo.
inline bool blurGlyphMask(GlyphBitmap &bitmap, int radius)
{
    if (radius <= 0) return true;
    if (radius > kMaxGlyphMaskBlurRadius || bitmap.format != GlyphBitmapFormat::Alpha) return false;
    if (bitmap.width == 0 && bitmap.height == 0 && bitmap.alphaPixels.empty()) return true;
    const int pad = radius + 1;
    if (bitmap.width <= 0 || bitmap.height <= 0 || bitmap.width > 4096 - 2 * pad
        || bitmap.height > 4096 - 2 * pad
        || bitmap.alphaPixels.size() < static_cast<std::size_t>(bitmap.width) * bitmap.height) return false;
    const int width = bitmap.width + 2 * pad, height = bitmap.height + 2 * pad;
    const auto kernel = wsc::render::computeGaussianKernel(static_cast<float>(radius));
    std::vector<float> horizontal(static_cast<std::size_t>(width) * height, 0.0f);
    for (int y = 0; y < bitmap.height; ++y) {
        for (int x = 0; x < width; ++x) {
            float sum = 0.0f;
            for (int tap = -radius; tap <= radius; ++tap) {
                const int sourceX = x - pad + tap;
                if (sourceX >= 0 && sourceX < bitmap.width)
                    sum += bitmap.alphaPixels[static_cast<std::size_t>(y) * bitmap.width + sourceX]
                        * kernel.weights[static_cast<std::size_t>(std::abs(tap))];
            }
            horizontal[static_cast<std::size_t>(y + pad) * width + x] = sum;
        }
    }
    std::vector<unsigned char> output(static_cast<std::size_t>(width) * height, 0);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float sum = 0.0f;
            for (int tap = -radius; tap <= radius; ++tap) {
                const int sourceY = y + tap;
                if (sourceY >= 0 && sourceY < height)
                    sum += horizontal[static_cast<std::size_t>(sourceY) * width + x]
                        * kernel.weights[static_cast<std::size_t>(std::abs(tap))];
            }
            output[static_cast<std::size_t>(y) * width + x] =
                static_cast<unsigned char>(std::clamp(std::lround(sum), 0L, 255L));
        }
    }
    bitmap.width = width; bitmap.height = height;
    bitmap.bearingX -= pad; bitmap.bearingY -= pad;
    bitmap.alphaPixels = std::move(output);
    return true;
}
}

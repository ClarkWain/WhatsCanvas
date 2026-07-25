#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace whatscanvas::test {

struct PixelDiff
{
    int maxDifference = 0;
    double meanDifference = 0.0;
    double badPixelRatio = 0.0;
    std::size_t badPixelCount = 0;
    std::size_t pixelCount = 0;
    int worstX = 0;
    int worstY = 0;
    int worstChannel = 0;
};

inline PixelDiff comparePremultipliedRGBA(
    const std::vector<unsigned char> &actual,
    const std::vector<unsigned char> &reference,
    int width, int height, int badChannelThreshold = 2)
{
    PixelDiff result;
    if (width <= 0 || height <= 0 || actual.size() != reference.size()
        || actual.size() != static_cast<std::size_t>(width * height * 4)) {
        result.maxDifference = 255;
        result.meanDifference = 255.0;
        result.badPixelRatio = 1.0;
        return result;
    }

    std::uint64_t totalDifference = 0;
    result.pixelCount = static_cast<std::size_t>(width * height);
    for (std::size_t pixel = 0; pixel < result.pixelCount; ++pixel) {
        bool bad = false;
        for (int channel = 0; channel < 4; ++channel) {
            const std::size_t index = pixel * 4u + static_cast<std::size_t>(channel);
            const int difference = std::abs(
                static_cast<int>(actual[index]) - static_cast<int>(reference[index]));
            totalDifference += static_cast<std::uint64_t>(difference);
            bad = bad || difference > badChannelThreshold;
            if (difference > result.maxDifference) {
                result.maxDifference = difference;
                result.worstX = static_cast<int>(pixel % static_cast<std::size_t>(width));
                result.worstY = static_cast<int>(pixel / static_cast<std::size_t>(width));
                result.worstChannel = channel;
            }
        }
        result.badPixelCount += bad ? 1u : 0u;
    }

    result.meanDifference = static_cast<double>(totalDifference)
        / static_cast<double>(result.pixelCount * 4u);
    result.badPixelRatio = static_cast<double>(result.badPixelCount)
        / static_cast<double>(result.pixelCount);
    return result;
}

inline std::uint64_t hashRGBA(const std::vector<unsigned char> &pixels)
{
    std::uint64_t hash = 14695981039346656037ull;
    for (unsigned char value : pixels) {
        hash ^= value;
        hash *= 1099511628211ull;
    }
    return hash;
}

inline bool reportPixelParity(const std::string &backend,
                              const PixelDiff &diff,
                              std::uint64_t actualHash,
                              std::uint64_t referenceHash,
                              int maxAllowed = 4,
                              double meanAllowed = 0.75,
                              double badPixelRatioAllowed = 0.005,
                              bool additionalChecksPassed = true,
                              const char *failureReason = nullptr)
{
    const bool pixelsPassed = diff.maxDifference <= maxAllowed
        && diff.meanDifference <= meanAllowed
        && diff.badPixelRatio <= badPixelRatioAllowed;
    const bool passed = pixelsPassed && additionalChecksPassed;
    std::cout << std::fixed << std::setprecision(6)
              << "FILTER_PARITY"
              << " backend=" << backend
              << " status=" << (passed ? "PASS" : "FAIL")
              << " max_difference=" << diff.maxDifference
              << " mean_difference=" << diff.meanDifference
              << " bad_pixel_ratio=" << diff.badPixelRatio
              << " bad_pixels=" << diff.badPixelCount
              << " pixels=" << diff.pixelCount
              << " worst_x=" << diff.worstX
              << " worst_y=" << diff.worstY
              << " worst_channel=" << diff.worstChannel
              << " actual_hash=" << actualHash
              << " reference_hash=" << referenceHash;
    if (!passed && failureReason != nullptr) {
        std::cout << " reason=" << failureReason;
    }
    std::cout << '\n';
    return passed;
}

} // namespace whatscanvas::test

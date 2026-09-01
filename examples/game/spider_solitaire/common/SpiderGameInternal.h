#pragma once

#include "SpiderGame.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

#include <wsc/FontSystem.h>

namespace spider {

inline float clamp01(float value) {
    return std::max(0.0f, std::min(1.0f, value));
}

inline float easeOutQuint(float value) {
    const float inverse = 1.0f - clamp01(value);
    return 1.0f - inverse * inverse * inverse * inverse * inverse;
}

inline float easeOutCubic(float value) {
    const float inverse = 1.0f - clamp01(value);
    return 1.0f - inverse * inverse * inverse;
}

inline float easeInOutCubic(float value) {
    const float t = clamp01(value);
    return t < 0.5f ? 4.0f * t * t * t
                    : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) * 0.5f;
}

inline Color mix(const Color& a, const Color& b, float amount, int alpha = -1) {
    const float t = clamp01(amount);
    const auto channel = [t](int from, int to) {
        return static_cast<int>(std::lround(from + (to - from) * t));
    };
    return Color(channel(a.getR(), b.getR()),
                 channel(a.getG(), b.getG()),
                 channel(a.getB(), b.getB()),
                 alpha >= 0 ? alpha : channel(a.getA(), b.getA()));
}

inline void useUiFont(Paint& paint, int weight = 500) {
    paint.setFont(wsc::FontSystem::kDefaultPrimaryFamily);
    paint.setFontWeight(weight);
}

inline std::string rankText(int rank) {
    if (rank == 1) return "A";
    if (rank == 11) return "J";
    if (rank == 12) return "Q";
    if (rank == 13) return "K";
    return std::to_string(rank);
}

inline std::string formatTime(float seconds) {
    const int total = std::max(0, static_cast<int>(seconds));
    std::ostringstream out;
    out << std::setfill('0') << std::setw(2) << total / 60
        << ':' << std::setw(2) << total % 60;
    return out.str();
}

inline bool inside(const RectF& rect, float x, float y) {
    return x >= rect.getX() && x <= rect.getX() + rect.getWidth() &&
           y >= rect.getY() && y <= rect.getY() + rect.getHeight();
}

namespace motion {

// Motion tokens are deliberately shared by desktop and touch hosts. Shortening
// touch animations below ~100 ms does not improve responsiveness; it merely
// reduces a smooth 60 Hz transition to two or three visible positions.
constexpr float kIntroDuration = 0.26f;
constexpr float kIntroStagger = 0.028f;
constexpr float kDealDuration = 0.22f;
constexpr float kDealStagger = 0.024f;
constexpr float kPlaceDuration = 0.17f;
constexpr float kSnapBackDuration = 0.15f;
constexpr float kDealArcHeight = 4.0f;

} // namespace motion

} // namespace spider

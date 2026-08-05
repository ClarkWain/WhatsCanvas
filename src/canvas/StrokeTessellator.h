#pragma once

#include <vector>

namespace wsc::detail {

struct Vec2 {
    Vec2() = default;
    Vec2(float xValue, float yValue) : x(xValue), y(yValue) {}

    float x = 0.0f;
    float y = 0.0f;
};

enum class StrokeJoin {
    Miter,
    Round,
    Bevel,
};

enum class StrokeCap {
    Butt,
    Round,
    Square,
    Closed,
};

struct StrokeStyle {
    float width = 1.0f;
    StrokeJoin join = StrokeJoin::Miter;
    StrokeCap cap = StrokeCap::Butt;
    bool allowOverlap = false;
    float miterLimit = 4.0f;
};

// Expands a center-line polyline into an unindexed triangle list. Consecutive
// points within 1e-4 are treated as duplicates. Invalid coordinates and
// non-positive widths produce an empty mesh.
std::vector<Vec2> tessellateStroke(
    const std::vector<Vec2> &points,
    const StrokeStyle &style);

} // namespace wsc::detail

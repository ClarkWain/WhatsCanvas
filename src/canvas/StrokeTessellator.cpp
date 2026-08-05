#include "StrokeTessellator.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace wsc::detail {
namespace {

constexpr float kPointEpsilon = 0.0001f;
constexpr float kPi = 3.14159265358979323846f;
constexpr float kRoundStep = 0.174533f; // Approximately ten degrees.

bool isFinite(const Vec2 &point)
{
    return std::isfinite(point.x) && std::isfinite(point.y);
}

bool nearlyEqual(const Vec2 &a, const Vec2 &b)
{
    return std::fabs(a.x - b.x) <= kPointEpsilon
        && std::fabs(a.y - b.y) <= kPointEpsilon;
}

Vec2 add(const Vec2 &a, const Vec2 &b)
{
    return {a.x + b.x, a.y + b.y};
}

Vec2 subtract(const Vec2 &a, const Vec2 &b)
{
    return {a.x - b.x, a.y - b.y};
}

Vec2 multiply(const Vec2 &value, float factor)
{
    return {value.x * factor, value.y * factor};
}

float dot(const Vec2 &a, const Vec2 &b)
{
    return a.x * b.x + a.y * b.y;
}

float cross(const Vec2 &a, const Vec2 &b)
{
    return a.x * b.y - a.y * b.x;
}

float length(const Vec2 &value)
{
    return std::sqrt(dot(value, value));
}

Vec2 normalized(const Vec2 &value)
{
    const float magnitude = length(value);
    if (magnitude <= kPointEpsilon) {
        return {};
    }
    return {value.x / magnitude, value.y / magnitude};
}

float angleBetween(const Vec2 &a, const Vec2 &b)
{
    const float magnitudes = length(a) * length(b);
    if (magnitudes <= kPointEpsilon) {
        return 0.0f;
    }
    return std::acos(std::clamp(dot(a, b) / magnitudes, -1.0f, 1.0f));
}

struct Line {
    Vec2 a;
    Vec2 b;
};

std::optional<Vec2> intersect(const Line &first, const Line &second, bool infiniteLines)
{
    const Vec2 firstDirection = subtract(first.b, first.a);
    const Vec2 secondDirection = subtract(second.b, second.a);
    const Vec2 originDistance = subtract(second.a, first.a);
    const float denominator = cross(firstDirection, secondDirection);
    if (std::fabs(denominator) < kPointEpsilon) {
        return std::nullopt;
    }

    const float firstPosition = cross(originDistance, secondDirection) / denominator;
    const float secondPosition = cross(originDistance, firstDirection) / denominator;
    if (!infiniteLines
        && (firstPosition < 0.0f || firstPosition > 1.0f
            || secondPosition < 0.0f || secondPosition > 1.0f)) {
        return std::nullopt;
    }
    return add(first.a, multiply(firstDirection, firstPosition));
}

struct Segment {
    Line center;
    Line left;
    Line right;
    Vec2 direction;
};

Segment makeSegment(const Vec2 &start, const Vec2 &end, float halfWidth)
{
    const Vec2 direction = normalized(subtract(end, start));
    const Vec2 offset{-direction.y * halfWidth, direction.x * halfWidth};
    return {
        {start, end},
        {add(start, offset), add(end, offset)},
        {subtract(start, offset), subtract(end, offset)},
        direction,
    };
}

void appendTriangleFan(std::vector<Vec2> &vertices, const Vec2 &connectTo,
                       const Vec2 &origin, const Vec2 &start, const Vec2 &end,
                       bool clockwise)
{
    const Vec2 startOffset = subtract(start, origin);
    const Vec2 endOffset = subtract(end, origin);
    float startAngle = std::atan2(startOffset.y, startOffset.x);
    float endAngle = std::atan2(endOffset.y, endOffset.x);

    if (clockwise) {
        if (endAngle > startAngle) {
            endAngle -= 2.0f * kPi;
        }
    } else if (startAngle > endAngle) {
        startAngle -= 2.0f * kPi;
    }

    const float sweep = endAngle - startAngle;
    const int triangleCount = std::max(1, static_cast<int>(std::floor(std::fabs(sweep) / kRoundStep)));
    const float step = sweep / static_cast<float>(triangleCount);
    Vec2 triangleStart = start;

    for (int triangle = 0; triangle < triangleCount; ++triangle) {
        Vec2 triangleEnd;
        if (triangle + 1 == triangleCount) {
            triangleEnd = end;
        } else {
            const float rotation = static_cast<float>(triangle + 1) * step;
            const float cosine = std::cos(rotation);
            const float sine = std::sin(rotation);
            triangleEnd = {
                cosine * startOffset.x - sine * startOffset.y + origin.x,
                sine * startOffset.x + cosine * startOffset.y + origin.y,
            };
        }
        vertices.push_back(triangleStart);
        vertices.push_back(triangleEnd);
        vertices.push_back(connectTo);
        triangleStart = triangleEnd;
    }
}

struct JoinGeometry {
    Vec2 endLeft;
    Vec2 endRight;
    Vec2 nextLeft;
    Vec2 nextRight;
};

JoinGeometry appendJoin(std::vector<Vec2> &vertices, const Segment &first,
                        const Segment &second, StrokeJoin requestedJoin,
                        bool allowOverlap, float miterLimit)
{
    const float angle = angleBetween(first.direction, second.direction);
    const float wrappedAngle = angle > kPi / 2.0f ? kPi - angle : angle;
    const float safeMiterLimit = std::max(1.0f, miterLimit);
    const float minimumMiterAngle = 2.0f * std::asin(std::min(1.0f, 1.0f / safeMiterLimit));
    StrokeJoin join = requestedJoin;
    if (join == StrokeJoin::Miter && wrappedAngle < minimumMiterAngle) {
        join = StrokeJoin::Bevel;
    }

    if (join == StrokeJoin::Miter) {
        const auto leftIntersection = intersect(first.left, second.left, true);
        const auto rightIntersection = intersect(first.right, second.right, true);
        const Vec2 left = leftIntersection.value_or(first.left.b);
        const Vec2 right = rightIntersection.value_or(first.right.b);
        return {left, right, left, right};
    }

    const bool clockwise = cross(first.direction, second.direction) < 0.0f;
    const Line &outerFirst = clockwise ? first.left : first.right;
    const Line &outerSecond = clockwise ? second.left : second.right;
    const Line &innerFirst = clockwise ? first.right : first.left;
    const Line &innerSecond = clockwise ? second.right : second.left;
    const auto innerIntersection = intersect(innerFirst, innerSecond, allowOverlap);
    const Vec2 inner = innerIntersection.value_or(innerFirst.b);
    Vec2 innerStart = inner;
    if (!innerIntersection) {
        innerStart = angle > kPi / 2.0f ? outerFirst.b : innerFirst.b;
    }

    JoinGeometry geometry;
    if (clockwise) {
        geometry = {outerFirst.b, inner, outerSecond.a, innerStart};
    } else {
        geometry = {inner, outerFirst.b, innerStart, outerSecond.a};
    }

    if (join == StrokeJoin::Bevel) {
        vertices.push_back(outerFirst.b);
        vertices.push_back(outerSecond.a);
        vertices.push_back(inner);
    } else {
        appendTriangleFan(vertices, inner, first.center.b,
                          outerFirst.b, outerSecond.a, clockwise);
    }
    return geometry;
}

} // namespace

std::vector<Vec2> tessellateStroke(const std::vector<Vec2> &points, const StrokeStyle &style)
{
    if (points.size() < 2 || !std::isfinite(style.width) || style.width <= 0.0f) {
        return {};
    }
    if (!std::all_of(points.begin(), points.end(), isFinite)) {
        return {};
    }

    const float halfWidth = style.width * 0.5f;
    std::vector<Segment> segments;
    segments.reserve(points.size());
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        if (!nearlyEqual(points[i], points[i + 1])) {
            segments.push_back(makeSegment(points[i], points[i + 1], halfWidth));
        }
    }
    if (style.cap == StrokeCap::Closed && !nearlyEqual(points.back(), points.front())) {
        segments.push_back(makeSegment(points.back(), points.front(), halfWidth));
    }
    if (segments.empty()) {
        return {};
    }

    const Segment &first = segments.front();
    const Segment &last = segments.back();
    Vec2 pathStartLeft = first.left.a;
    Vec2 pathStartRight = first.right.a;
    Vec2 pathEndLeft = last.left.b;
    Vec2 pathEndRight = last.right.b;
    std::vector<Vec2> vertices;
    vertices.reserve(segments.size() * 9);

    if (style.cap == StrokeCap::Square) {
        pathStartLeft = subtract(pathStartLeft, multiply(first.direction, halfWidth));
        pathStartRight = subtract(pathStartRight, multiply(first.direction, halfWidth));
        pathEndLeft = add(pathEndLeft, multiply(last.direction, halfWidth));
        pathEndRight = add(pathEndRight, multiply(last.direction, halfWidth));
    } else if (style.cap == StrokeCap::Round) {
        appendTriangleFan(vertices, first.center.a, first.center.a,
                          first.left.a, first.right.a, false);
        appendTriangleFan(vertices, last.center.b, last.center.b,
                          last.left.b, last.right.b, true);
    } else if (style.cap == StrokeCap::Closed) {
        const JoinGeometry closure = appendJoin(vertices, last, first, style.join,
                                                style.allowOverlap, style.miterLimit);
        pathEndLeft = closure.endLeft;
        pathEndRight = closure.endRight;
        pathStartLeft = closure.nextLeft;
        pathStartRight = closure.nextRight;
    }

    Vec2 startLeft = pathStartLeft;
    Vec2 startRight = pathStartRight;
    Vec2 nextLeft{};
    Vec2 nextRight{};
    for (std::size_t i = 0; i < segments.size(); ++i) {
        Vec2 endLeft;
        Vec2 endRight;
        if (i + 1 == segments.size()) {
            endLeft = pathEndLeft;
            endRight = pathEndRight;
        } else {
            const JoinGeometry join = appendJoin(vertices, segments[i], segments[i + 1],
                                                 style.join, style.allowOverlap, style.miterLimit);
            endLeft = join.endLeft;
            endRight = join.endRight;
            nextLeft = join.nextLeft;
            nextRight = join.nextRight;
        }

        vertices.push_back(startLeft);
        vertices.push_back(startRight);
        vertices.push_back(endLeft);
        vertices.push_back(endLeft);
        vertices.push_back(startRight);
        vertices.push_back(endRight);
        startLeft = nextLeft;
        startRight = nextRight;
    }
    return vertices;
}

} // namespace wsc::detail

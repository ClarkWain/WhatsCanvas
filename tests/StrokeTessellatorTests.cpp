#include "canvas/StrokeTessellator.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <vector>

namespace {

using wsc::detail::StrokeCap;
using wsc::detail::StrokeJoin;
using wsc::detail::StrokeStyle;
using wsc::detail::Vec2;

bool expect(bool condition, const std::string &message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        return false;
    }
    return true;
}

bool near(float actual, float expected, float epsilon = 0.0002f)
{
    return std::fabs(actual - expected) <= epsilon;
}

bool testKnownStraightLine()
{
    const std::vector<Vec2> points = {{0.0f, 0.0f}, {10.0f, 0.0f}};
    const StrokeStyle style{10.0f, StrokeJoin::Miter, StrokeCap::Butt, false, 4.0f};
    const auto vertices = wsc::detail::tessellateStroke(points, style);
    const std::vector<Vec2> expected = {
        {0.0f, 5.0f}, {0.0f, -5.0f}, {10.0f, 5.0f},
        {10.0f, 5.0f}, {0.0f, -5.0f}, {10.0f, -5.0f},
    };

    bool ok = expect(vertices.size() == expected.size(), "straight butt line should produce two triangles");
    if (vertices.size() == expected.size()) {
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            ok = expect(near(vertices[i].x, expected[i].x) && near(vertices[i].y, expected[i].y),
                        "straight butt line vertex mismatch at " + std::to_string(i)) && ok;
        }
    }
    return ok;
}

bool testDegenerateInputs()
{
    bool ok = expect(wsc::detail::tessellateStroke({}, {}).empty(),
                     "empty input should produce no vertices");
    ok = expect(wsc::detail::tessellateStroke({{1.0f, 2.0f}}, {}).empty(),
                "one point should produce no vertices") && ok;
    ok = expect(wsc::detail::tessellateStroke({{1.0f, 2.0f}, {1.0f, 2.0f}}, {}).empty(),
                "duplicate points should produce no vertices") && ok;
    ok = expect(wsc::detail::tessellateStroke(
                    {{0.0f, 0.0f}, {10.0f, 0.0f}},
                    {0.0f, StrokeJoin::Miter, StrokeCap::Butt, false, 4.0f}).empty(),
                "zero width should produce no vertices") && ok;
    ok = expect(wsc::detail::tessellateStroke(
                    {{0.0f, 0.0f}, {std::numeric_limits<float>::infinity(), 0.0f}}, {}).empty(),
                "non-finite input should be rejected") && ok;
    return ok;
}

bool testCapsJoinsAndDuplicateFiltering()
{
    const std::vector<Vec2> line = {{0.0f, 0.0f}, {10.0f, 0.0f}};
    const auto square = wsc::detail::tessellateStroke(
        line, {10.0f, StrokeJoin::Miter, StrokeCap::Square, false, 4.0f});
    bool ok = expect(square.size() == 6, "square-capped line should still contain two triangles");
    if (square.size() == 6) {
        float minimumX = square.front().x;
        float maximumX = square.front().x;
        for (const Vec2 &vertex : square) {
            minimumX = std::min(minimumX, vertex.x);
            maximumX = std::max(maximumX, vertex.x);
        }
        ok = expect(near(minimumX, -5.0f) && near(maximumX, 15.0f),
                    "square caps should extend by half the stroke width") && ok;
    }

    const auto round = wsc::detail::tessellateStroke(
        line, {10.0f, StrokeJoin::Miter, StrokeCap::Round, false, 4.0f});
    ok = expect(round.size() > square.size() && round.size() % 3 == 0,
                "round caps should add complete fan triangles") && ok;

    const std::vector<Vec2> corner = {{0.0f, 0.0f}, {20.0f, 0.0f}, {20.0f, 20.0f}};
    const auto miter = wsc::detail::tessellateStroke(
        corner, {10.0f, StrokeJoin::Miter, StrokeCap::Butt, false, 64.0f});
    const auto limitedMiter = wsc::detail::tessellateStroke(
        corner, {10.0f, StrokeJoin::Miter, StrokeCap::Butt, false, 1.0f});
    const auto bevel = wsc::detail::tessellateStroke(
        corner, {10.0f, StrokeJoin::Bevel, StrokeCap::Butt, false, 4.0f});
    const auto roundJoin = wsc::detail::tessellateStroke(
        corner, {10.0f, StrokeJoin::Round, StrokeCap::Butt, false, 4.0f});
    ok = expect(miter.size() == 12, "unclipped miter should only need segment triangles") && ok;
    ok = expect(limitedMiter.size() == bevel.size() && bevel.size() == 15,
                "a low miter limit should fall back to one bevel triangle") && ok;
    ok = expect(roundJoin.size() > bevel.size() && roundJoin.size() % 3 == 0,
                "round joins should add a triangle fan") && ok;

    const auto duplicate = wsc::detail::tessellateStroke(
        {{0.0f, 0.0f}, {20.0f, 0.0f}, {20.00001f, 0.00001f}, {20.0f, 20.0f}},
        {10.0f, StrokeJoin::Miter, StrokeCap::Butt, false, 64.0f});
    ok = expect(duplicate.size() == miter.size(), "near-duplicate points should be ignored") && ok;
    if (duplicate.size() == miter.size()) {
        for (std::size_t i = 0; i < miter.size(); ++i) {
            ok = expect(near(duplicate[i].x, miter[i].x) && near(duplicate[i].y, miter[i].y),
                        "duplicate filtering should preserve the original mesh") && ok;
        }
    }

    const auto closed = wsc::detail::tessellateStroke(
        {{0.0f, 0.0f}, {30.0f, 0.0f}, {15.0f, 25.0f}},
        {6.0f, StrokeJoin::Round, StrokeCap::Closed, false, 4.0f});
    ok = expect(!closed.empty() && closed.size() % 3 == 0,
                "closed paths should generate complete triangles") && ok;
    return ok;
}

bool testDeterministicRandomRobustness()
{
    std::mt19937 rng(0x57435332u);
    std::uniform_real_distribution<float> coordinate(-200.0f, 200.0f);
    std::uniform_real_distribution<float> width(0.25f, 40.0f);
    std::uniform_real_distribution<float> miterLimit(1.0f, 32.0f);
    std::uniform_int_distribution<int> pointCount(2, 12);
    std::uniform_int_distribution<int> joinChoice(0, 2);
    std::uniform_int_distribution<int> capChoice(0, 3);

    bool ok = true;
    for (int caseIndex = 0; caseIndex < 300; ++caseIndex) {
        std::vector<Vec2> points;
        const int count = pointCount(rng);
        points.reserve(static_cast<std::size_t>(count + 2));
        for (int i = 0; i < count; ++i) {
            Vec2 point{coordinate(rng), coordinate(rng)};
            points.push_back(point);
            if (i == 1 && caseIndex % 7 == 0) {
                points.push_back(point);
            } else if (i == 2 && caseIndex % 11 == 0) {
                points.push_back({point.x + 0.00001f, point.y - 0.00001f});
            }
        }

        const StrokeStyle style{
            width(rng),
            static_cast<StrokeJoin>(joinChoice(rng)),
            static_cast<StrokeCap>(capChoice(rng)),
            caseIndex % 5 == 0,
            miterLimit(rng),
        };
        const auto vertices = wsc::detail::tessellateStroke(points, style);
        const auto repeated = wsc::detail::tessellateStroke(points, style);
        ok = expect(!vertices.empty(), "random case should produce geometry") && ok;
        ok = expect(vertices.size() % 3 == 0, "random case should produce complete triangles") && ok;
        ok = expect(vertices.size() == repeated.size(), "random case should be deterministic") && ok;
        for (std::size_t i = 0; i < vertices.size() && ok; ++i) {
            ok = expect(std::isfinite(vertices[i].x) && std::isfinite(vertices[i].y),
                        "random case should contain only finite vertices") && ok;
            ok = expect(near(vertices[i].x, repeated[i].x) && near(vertices[i].y, repeated[i].y),
                        "random case should repeat exactly") && ok;
        }
        if (!ok) {
            break;
        }
    }
    return ok;
}

std::uint64_t compatibilityFingerprint()
{
    const std::vector<std::vector<Vec2>> paths = {
        {{0.0f, 0.0f}, {10.0f, 0.0f}},
        {{0.0f, 0.0f}, {50.0f, 0.0f}, {52.0f, 100.0f}},
        {{0.0f, 0.0f}, {20.0f, 0.0f}, {20.0f, 20.0f}},
        {{0.0f, 0.0f}, {20.0f, 0.0f}, {0.0f, 0.0f}},
        {{-10.0f, -5.0f}, {0.0f, 0.0f}, {0.00005f, 0.00005f}, {10.0f, 8.0f}},
        {{0.0f, 0.0f}, {40.0f, 0.0f}, {40.0f, 40.0f}, {0.0f, 40.0f}},
    };
    const StrokeJoin joins[] = {StrokeJoin::Miter, StrokeJoin::Round, StrokeJoin::Bevel};
    const StrokeCap caps[] = {StrokeCap::Butt, StrokeCap::Round, StrokeCap::Square, StrokeCap::Closed};
    const float miterLimits[] = {1.0f, 4.0f, 64.0f};
    std::uint64_t hash = 1469598103934665603ull;
    const auto mix = [&hash](std::uint64_t value) {
        for (int byte = 0; byte < 8; ++byte) {
            hash ^= (value >> (byte * 8)) & 0xffu;
            hash *= 1099511628211ull;
        }
    };

    for (const auto &path : paths) {
        for (StrokeJoin join : joins) {
            for (StrokeCap cap : caps) {
                if (cap == StrokeCap::Closed && path.size() < 3) {
                    continue;
                }
                for (float miterLimit : miterLimits) {
                    const auto vertices = wsc::detail::tessellateStroke(
                        path, {7.5f, join, cap, false, miterLimit});
                    mix(vertices.size());
                    for (const Vec2 &vertex : vertices) {
                        const auto x = static_cast<std::int64_t>(std::llround(vertex.x * 1000.0f));
                        const auto y = static_cast<std::int64_t>(std::llround(vertex.y * 1000.0f));
                        mix(static_cast<std::uint64_t>(x));
                        mix(static_cast<std::uint64_t>(y));
                    }
                }
            }
        }
    }
    return hash;
}

bool testLegacyCompatibilityFingerprint()
{
    constexpr std::uint64_t kExpectedFingerprint = 519859228437137184ull;
    const std::uint64_t actual = compatibilityFingerprint();
    return expect(actual == kExpectedFingerprint,
                  "curated compatibility fingerprint changed: " + std::to_string(actual));
}

} // namespace

int main()
{
    bool ok = true;
    ok = testKnownStraightLine() && ok;
    ok = testDegenerateInputs() && ok;
    ok = testCapsJoinsAndDuplicateFiltering() && ok;
    ok = testLegacyCompatibilityFingerprint() && ok;
    ok = testDeterministicRandomRobustness() && ok;
    return ok ? 0 : 1;
}

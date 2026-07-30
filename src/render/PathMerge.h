#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

#include "command/DrawData.h"

/// Batch-merge compatibility for consecutive DrawPath commands.
///
/// Renderer::flush() merges adjacent path draws into a single draw call by
/// keeping the first command's uniforms/attributes and appending the geometry
/// of the following commands. That is only valid when the two commands share
/// every piece of per-draw state. The strict predicate retains the original
/// uniform merge contract; the broader renderer predicate permits color and
/// coverage differences because Renderer expands them to aligned per-vertex
/// attributes. Shader gradients remain unbatchable because their coordinates
/// and uniforms are per shape.
namespace wsc::render {

inline constexpr float kPathMergeEpsilon = 0.001f;

inline bool isAffine2DPathTransform(const glm::mat4 &transform)
{
    return std::abs(transform[0][2]) <= kPathMergeEpsilon
        && std::abs(transform[0][3]) <= kPathMergeEpsilon
        && std::abs(transform[1][2]) <= kPathMergeEpsilon
        && std::abs(transform[1][3]) <= kPathMergeEpsilon
        && std::abs(transform[2][0]) <= kPathMergeEpsilon
        && std::abs(transform[2][1]) <= kPathMergeEpsilon
        && std::abs(transform[2][2] - 1.0f) <= kPathMergeEpsilon
        && std::abs(transform[2][3]) <= kPathMergeEpsilon
        && std::abs(transform[3][2]) <= kPathMergeEpsilon
        && std::abs(transform[3][3] - 1.0f) <= kPathMergeEpsilon;
}

struct PathDeviceBounds
{
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

/// Resolve conservative device-space bounds for a path that can be safely
/// reordered relative to other non-overlapping paths. Complex state remains
/// ordered even when its geometry happens to be disjoint.
inline bool getReorderablePathBounds(
    const DrawPathData &data, PathDeviceBounds &bounds)
{
    if (data.drawMode != PathDrawMode::Fill
        || data.hasShaderGradient()
        || data.scissor.enabled
        || data.clipMask.hasPaths()
        || !isAffine2DPathTransform(data.transform)) {
        return false;
    }
    float left = std::numeric_limits<float>::max();
    float top = std::numeric_limits<float>::max();
    float right = std::numeric_limits<float>::lowest();
    float bottom = std::numeric_limits<float>::lowest();
    const auto includePoint = [&](float localX, float localY) {
        const float x = data.transform[0][0] * localX
            + data.transform[1][0] * localY
            + data.transform[3][0];
        const float y = data.transform[0][1] * localX
            + data.transform[1][1] * localY
            + data.transform[3][1];
        if (!std::isfinite(x) || !std::isfinite(y)) {
            return false;
        }
        left = std::min(left, x);
        top = std::min(top, y);
        right = std::max(right, x);
        bottom = std::max(bottom, y);
        return true;
    };

    if (data.sharedGeometry && data.sharedGeometry->hasBounds) {
        const DrawPathGeometry &geometry = *data.sharedGeometry;
        if (!includePoint(geometry.boundsLeft, geometry.boundsTop)
            || !includePoint(geometry.boundsRight, geometry.boundsTop)
            || !includePoint(geometry.boundsRight, geometry.boundsBottom)
            || !includePoint(geometry.boundsLeft, geometry.boundsBottom)) {
            return false;
        }
    } else {
        const std::vector<float> &points = data.pointData();
        if (points.size() < 6u || (points.size() % 2u) != 0u) {
            return false;
        }
        for (std::size_t index = 0; index < points.size(); index += 2u) {
            if (!includePoint(points[index], points[index + 1u])) {
                return false;
            }
        }
    }
    if (!(right > left) || !(bottom > top)) {
        return false;
    }
    bounds = {left, top, right, bottom};
    return true;
}

inline bool pathDeviceBoundsOverlap(
    const PathDeviceBounds &a, const PathDeviceBounds &b)
{
    return !(a.right + kPathMergeEpsilon < b.left
        || b.right + kPathMergeEpsilon < a.left
        || a.bottom + kPathMergeEpsilon < b.top
        || b.bottom + kPathMergeEpsilon < a.top);
}

inline bool hasCompatiblePathBatchStateWithoutTransform(
    const DrawPathData &a, const DrawPathData &b)
{
    if (a.hasShaderGradient() || b.hasShaderGradient()) {
        return false;
    }
    if (a.drawMode != b.drawMode || a.capStyle != b.capStyle) {
        return false;
    }
    if (std::abs(a.width - b.width) > kPathMergeEpsilon) {
        return false;
    }
    if (a.blendMode != b.blendMode) {
        return false;
    }
    if (a.scissor.enabled != b.scissor.enabled
        || a.scissor.x != b.scissor.x
        || a.scissor.y != b.scissor.y
        || a.scissor.width != b.scissor.width
        || a.scissor.height != b.scissor.height) {
        return false;
    }
    if (a.clipMask.fingerprint != b.clipMask.fingerprint) {
        return false;
    }
    return true;
}

inline bool hasCompatiblePathBatchState(
    const DrawPathData &a, const DrawPathData &b)
{
    return hasCompatiblePathBatchStateWithoutTransform(a, b)
        && a.transform == b.transform;
}

/// Broader compatibility used by the renderer's per-vertex path batch. Solid
/// colors and analytic coverage may differ because they are expanded to
/// aligned per-vertex attributes before submission. Different 2D affine
/// transforms are flattened into the merged vertex stream; perspective and
/// 3D transforms remain separate to preserve clip-space behavior.
inline bool canBatchPathData(
    const DrawPathData &a, const DrawPathData &b)
{
    return hasCompatiblePathBatchStateWithoutTransform(a, b)
        && (a.transform == b.transform
            || (isAffine2DPathTransform(a.transform)
                && isAffine2DPathTransform(b.transform)));
}

inline bool canMergePathData(const DrawPathData &a, const DrawPathData &b)
{
    // The legacy uniform merge keeps the first command's color and attribute
    // layout, so these fields must still match exactly.
    if (!hasCompatiblePathBatchState(a, b)
        || a.hasVertexColors() || b.hasVertexColors()
        || a.hasCoverage() != b.hasCoverage()) {
        return false;
    }
    for (int c = 0; c < 4; ++c) {
        if (std::abs(a.color[c] - b.color[c]) > kPathMergeEpsilon) {
            return false;
        }
    }
    return true;
}

} // namespace wsc::render

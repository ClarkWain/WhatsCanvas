#pragma once

#include <cmath>

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

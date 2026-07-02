#pragma once

#include <cmath>

#include "command/DrawData.h"

/// Batch-merge compatibility for consecutive DrawPath commands.
///
/// Renderer::flush() merges adjacent path draws into a single draw call by
/// keeping the first command's uniforms/attributes and appending the geometry
/// of the following commands. That is only valid when the two commands share
/// every piece of per-draw state. In particular, per-shape state that a merged
/// draw cannot represent — per-vertex colours, shader gradients, and the
/// presence of analytic-AA coverage — must match (and gradients/vertex colours
/// disqualify merging entirely), otherwise the appended geometry would render
/// with the wrong state.
namespace wsc::render {

inline constexpr float kPathMergeEpsilon = 0.001f;

inline bool canMergePathData(const DrawPathData &a, const DrawPathData &b)
{
    // Per-vertex colours and shader gradients carry per-shape data that cannot
    // be shared across a merged draw call.
    if (a.hasVertexColors() || b.hasVertexColors()) {
        return false;
    }
    if (a.hasShaderGradient() || b.hasShaderGradient()) {
        return false;
    }

    // Analytic-AA coverage is a per-vertex attribute; it must be present (or
    // absent) on both so the concatenated coverage stays aligned with points.
    if (a.hasCoverage() != b.hasCoverage()) {
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
    if (a.transform != b.transform) {
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
    for (int c = 0; c < 4; ++c) {
        if (std::abs(a.color[c] - b.color[c]) > kPathMergeEpsilon) {
            return false;
        }
    }
    return true;
}

} // namespace wsc::render

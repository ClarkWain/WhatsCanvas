#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "canvas/base.h"
#include "canvas/Path.h"
#include "render/RenderTypes.h"

struct ClipPathState
{
    Path path;
    glm::mat4 transform = glm::mat4(1.0f);
    RectF deviceBounds;
    mutable ClipMaskPath mask;
    mutable SharedClipMaskResource resource;
};

struct ClipState
{
    bool enabled = false;
    RectF rect;
    std::vector<ClipPathState> paths;
};

struct GraphicsState
{
    glm::mat4 matrix = glm::mat4(1.0f);
    ClipState clip;

    /// Current blend mode (saved/restored with the graphics state stack).
    DrawBlendMode blendMode = DrawBlendMode::SrcOver;

    /// Current fill color (saved/restored with the graphics state stack).
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};

    /// Device pixel ratio baked into `matrix`. Stored here (not on the Canvas
    /// Impl) so it is pushed/popped in lockstep with the transform by
    /// save()/restore(); a restore() that crosses a setDevicePixelRatio() call
    /// then keeps the ratio and the matrix consistent. Default 1.0 leaves the
    /// baked-in scale as an exact identity, so behavior is bit-identical to a
    /// canvas that never touches the device pixel ratio.
    float devicePixelRatio = 1.0f;
};
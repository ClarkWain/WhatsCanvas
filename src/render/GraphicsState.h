#pragma once

#include <vector>

#include <glm.hpp>

#include "base.h"
#include "canvas/Path.h"
#include "render/RenderTypes.h"

struct ClipPathState
{
    Path path;
    glm::mat4 transform = glm::mat4(1.0f);
    RectF deviceBounds;
    ClipMaskPath mask;
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
};
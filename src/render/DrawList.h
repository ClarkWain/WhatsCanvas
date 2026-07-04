#pragma once

#include <vector>

#include "RenderTypes.h"

// Backend-neutral draw representation (first slice of ADR-006). Commands will
// eventually emit these primitives instead of calling a graphics API directly,
// and each backend translates them into its own draw calls. For now only the
// Vulkan backend consumes them (VulkanRenderDevice::executeDrawList), which
// keeps the shipping OpenGL command path untouched.
namespace wsc {

enum class DrawPrimitiveKind
{
    SolidTriangles, ///< Triangle list, single solid color.
    TexturedQuad,   ///< Full-target quad sampling an image resource.
};

/// One backend-neutral draw primitive.
struct DrawPrimitive
{
    DrawPrimitiveKind kind = DrawPrimitiveKind::SolidTriangles;

    /// Fixed-function blend mode index (matches VulkanRenderDevice::SolidBlendMode).
    int blendMode = 0;

    /// SolidTriangles: interleaved x,y vertex positions in normalized device
    /// coordinates (3 vertices per triangle).
    std::vector<float> positions;

    /// SolidTriangles: RGBA fill color in [0,1].
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};

    /// TexturedQuad: the image sampled across the full target.
    SharedImageResource texture;
};

using DrawList = std::vector<DrawPrimitive>;

} // namespace wsc

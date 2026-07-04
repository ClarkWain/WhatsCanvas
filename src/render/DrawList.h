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
    ClipFill,       ///< Full-target solid fill, clipped by a coverage mask (red channel).
};

/// One backend-neutral draw primitive.
struct DrawPrimitive
{
    DrawPrimitiveKind kind = DrawPrimitiveKind::SolidTriangles;

    /// Fixed-function blend mode index (matches VulkanRenderDevice::SolidBlendMode).
    int blendMode = 0;

    /// SolidTriangles: interleaved x,y vertex positions in normalized device
    /// coordinates (3 vertices per triangle).
    /// TexturedQuad: optional explicit NDC quad as a triangle list (x,y pairs);
    /// when empty, the full target is used.
    std::vector<float> positions;

    /// TexturedQuad: optional per-vertex UVs (u,v pairs) matching `positions`;
    /// when empty, full 0..1 UVs are used.
    std::vector<float> uvs;

    /// SolidTriangles / ClipFill: RGBA fill color in [0,1].
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};

    /// SolidTriangles: optional per-vertex RGBA colors (4 floats per vertex,
    /// matching the vertex count implied by `positions`). When present, they
    /// override the uniform `color` and are fragment-interpolated.
    std::vector<float> colors;

    /// TexturedQuad: alpha multiplier applied to the sampled texture.
    float layerAlpha = 1.0f;

    /// TexturedQuad: RGBA tint multiplied into the sampled texture.
    float tint[4] = {1.0f, 1.0f, 1.0f, 1.0f};

    /// TexturedQuad: optional 4x4 color matrix (+ offset) applied after tint.
    bool hasColorMatrix = false;
    float colorMatrix[16] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                             0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    float colorMatrixOffset[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    /// TexturedQuad: sampling filter (0 = linear, 1 = nearest, 2 = mipmap-linear).
    int sampling = 0;

    /// TexturedQuad: tile/address mode (0 = clamp, 1 = repeat, 2 = mirror, 3 = decal).
    int tileMode = 0;

    /// TexturedQuad: when false, the texture's own sampler is used (backward
    /// compatible); when true, a sampler matching `sampling`/`tileMode` is used.
    bool useCustomSampler = false;

    /// TexturedQuad: the image sampled across the quad.
    /// ClipFill: the coverage mask; its red channel modulates the fill alpha.
    SharedImageResource texture;
};

using DrawList = std::vector<DrawPrimitive>;

} // namespace wsc

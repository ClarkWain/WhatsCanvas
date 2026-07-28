#pragma once

#include <cstdint>
#include <vector>

#include "RenderTypes.h"

// Backend-neutral draw representation (ADR-006). The shared command encoder
// emits these primitives for portable offscreen command replay; Vulkan consumes
// them as its main command path, and OpenGL consumes them for layer/snapshot
// rendering while the shipping onscreen OpenGL command path remains direct.
namespace wsc {

enum class DrawPrimitiveKind
{
    SolidTriangles, ///< Triangle list, single solid color.
    TexturedQuad,   ///< Full-target quad sampling an image resource.
    ClipFill,       ///< Full-target solid fill, clipped by a coverage mask (red channel).
    GradientFill,   ///< Triangle list filled with a fragment-evaluated gradient.
};

/// Compact axis-aligned textured quad consumed by instanced backends.
struct TexturedQuadInstance
{
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 1.0f;
    float v1 = 1.0f;
    std::uint32_t packedTint = 0xffffffffu;
};

/// Compact solid vertex shared by streaming backends.
struct CompactSolidVertex
{
    float x = 0.0f;
    float y = 0.0f;
    std::uint32_t color = 0xffffffffu;
    std::uint8_t coverage = 255u;
    std::uint8_t padding[3] = {};
};
static_assert(
    sizeof(CompactSolidVertex) == 16,
    "compact solid vertices must remain 16 bytes");

/// One backend-neutral draw primitive.
struct DrawPrimitive
{
    DrawPrimitiveKind kind = DrawPrimitiveKind::SolidTriangles;

    /// Fixed-function blend mode index (matches VulkanRenderDevice::SolidBlendMode).
    int blendMode = 0;

    /// Optional scissor rectangle in framebuffer top-left coordinates. Backends
    /// should use a full-target scissor when disabled.
    bool scissorEnabled = false;
    int scissorX = 0;
    int scissorY = 0;
    int scissorWidth = 0;
    int scissorHeight = 0;

    /// SolidTriangles: interleaved x,y vertex positions in normalized device
    /// coordinates. When `indices` is empty, positions contain 3 vertices per
    /// triangle.
    /// TexturedQuad: optional explicit NDC quad as a triangle list (x,y pairs);
    /// when empty, the full target is used.
    std::vector<float> positions;

    /// SolidTriangles: final compact position/color/coverage stream. When
    /// present, backends can upload it without re-interleaving attributes.
    std::vector<CompactSolidVertex> compactVertices;

    /// SolidTriangles: optional triangle-list indices into `positions`.
    std::vector<std::uint32_t> indices;

    /// SolidTriangles: compact triangle-list indices used when the primitive
    /// contains no more than 65,536 vertices.
    std::vector<std::uint16_t> shortIndices;

    /// The producer already validated all triangle indices. Release backends
    /// may skip another full scan; debug builds still validate command input.
    bool indicesTrusted = false;

    /// TexturedQuad: optional per-vertex UVs (u,v pairs) matching `positions`;
    /// when empty, full 0..1 UVs are used.
    std::vector<float> uvs;

    /// TexturedQuad: optional compact instances for axis-aligned quads. When
    /// present, `positions` and `uvs` remain empty.
    std::vector<TexturedQuadInstance> texturedInstances;

    /// SolidTriangles / ClipFill: RGBA fill color in [0,1].
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};

    /// SolidTriangles: optional per-vertex RGBA colors overriding `color`.
    std::vector<float> colors;

    /// SolidTriangles: optional normalized RGBA8 colors matching `positions`.
    /// Backends prefer this stream when `compactSolidAttributes` is true.
    std::vector<std::uint8_t> packedColors;

    /// TexturedQuad: optional packed RGBA8_UNORM tint multiplied with `tint`.
    /// Packing keeps color data compact while allowing adjacent quads with
    /// different tint/alpha values to share one backend draw.
    std::vector<std::uint32_t> packedTints;

    /// SolidTriangles: optional per-vertex analytic-AA coverage in [0,1] (1 per
    /// vertex). When present, it modulates the fill alpha (edge feathering).
    std::vector<float> coverage;

    /// SolidTriangles: optional normalized coverage8 matching `positions`.
    std::vector<std::uint8_t> packedCoverage;

    /// SolidTriangles: the color and coverage streams originate from normalized
    /// 8-bit canvas data and may use a compact backend vertex format. Leave
    /// false for arbitrary high-precision per-vertex attributes.
    bool compactSolidAttributes = false;

    /// TexturedQuad: alpha multiplier applied to the sampled texture.
    float layerAlpha = 1.0f;

    /// TexturedQuad: optional uniform rounded-rectangle clip in local pixels.
    float roundedRadius = 0.0f;
    float roundedWidth = 0.0f;
    float roundedHeight = 0.0f;

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

    /// GradientFill: canvas-space local positions (x,y per vertex) matching the
    /// NDC vertices in `positions`; used to evaluate the gradient parameter.
    std::vector<float> localPositions;

    /// GradientFill parameters (mirroring DrawPathData's gradient fields).
    int gradientType = 0; ///< 1 = linear, 2 = radial.
    int gradientTileMode = 0;
    float linearStart[2] = {0.0f, 0.0f};
    float linearEnd[2] = {1.0f, 0.0f};
    float radialCenter[2] = {0.0f, 0.0f};
    float radialRadius = 1.0f;
    int gradientStopCount = 0;
    float gradientStopPositions[8] = {};
    float gradientStopColors[32] = {};

    /// TexturedQuad: the image sampled across the quad.
    /// ClipFill: the coverage mask; its red channel modulates the fill alpha.
    SharedImageResource texture;

    /// TexturedQuad: optional full-target coverage mask. Its red channel
    /// modulates the sampled image alpha in backends that support direct
    /// texture clipping.
    SharedImageResource clipTexture;

    /// TexturedQuad: maps framebuffer fragment coordinates into `clipTexture`
    /// UVs. Cropped layer targets use a non-zero offset to retain canvas-space
    /// clip coordinates.
    float clipUvScale[2] = {1.0f, 1.0f};
    float clipUvOffset[2] = {0.0f, 0.0f};
};

using DrawList = std::vector<DrawPrimitive>;

} // namespace wsc

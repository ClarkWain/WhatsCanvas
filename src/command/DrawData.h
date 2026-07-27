#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#include <glm/glm.hpp>
#include "render/RenderTypes.h"

struct DrawPointsData {
    std::vector<float> points;  // Each point stores x/y coordinates
    float size;
    float color[4];
    glm::mat4 transform = glm::mat4(1.0f);
    ScissorState scissor;
    DrawBlendMode blendMode = DrawBlendMode::SrcOver;
    ClipMaskState clipMask;
    size_t getPointCount() const { return points.size() / 2; }
};


struct DrawLinesData {
    std::vector<float> points;  // Each point stores x/y coordinates
    float width;
    float color[4];
    glm::mat4 transform = glm::mat4(1.0f);
    ScissorState scissor;
    DrawBlendMode blendMode = DrawBlendMode::SrcOver;
    ClipMaskState clipMask;
    size_t getLineCount() const { return points.size() / 4; }
};

enum class PathDrawMode {
    Fill,
    Stroke,
    FillAndStroke
};

enum class PathCapStyle {
    Round,
    Square,
    Bevel
};

enum class DrawGradientType {
    None,
    Linear,
    Radial
};

enum class DrawGradientTileMode {
    Clamp,
    Repeat,
    Mirror,
    Decal
};

struct DrawPathGeometry {
    std::vector<float> points;
    std::vector<float> coverage;
    std::vector<std::uint32_t> indices;

    std::size_t residentBytes() const
    {
        return points.capacity() * sizeof(float)
            + coverage.capacity() * sizeof(float)
            + indices.capacity() * sizeof(std::uint32_t);
    }
};

struct DrawPathData {
    static constexpr std::size_t kMaxGradientStops = 8;

    std::vector<float> points;    // Path points, each storing x/y coordinates
    std::vector<float> colors;    // Optional per-vertex colors, each storing r/g/b/a
    std::vector<float> coverage;  // Optional per-vertex analytic-AA coverage in [0,1]
    std::vector<std::uint32_t> indices; // Optional triangle indices into points
    std::vector<std::uint16_t> shortIndices; // Compact merged packet indices
    std::shared_ptr<const DrawPathGeometry> sharedGeometry;
    float width = 1.0f;           // Stroke width
    float color[4];               // RGBA color
    PathDrawMode drawMode;        // Draw mode
    PathCapStyle capStyle;        // Cap style
    glm::mat4 transform = glm::mat4(1.0f);
    ScissorState scissor;
    DrawBlendMode blendMode = DrawBlendMode::SrcOver;
    ClipMaskState clipMask;
    DrawGradientType gradientType = DrawGradientType::None;
    DrawGradientTileMode gradientTileMode = DrawGradientTileMode::Clamp;
    float gradientStart[2] = {0.0f, 0.0f};
    float gradientEnd[2] = {1.0f, 0.0f};
    float radialCenter[2] = {0.0f, 0.0f};
    float radialRadius = 1.0f;
    int gradientStopCount = 0;
    float gradientStopPositions[kMaxGradientStops] = {};
    float gradientStopColors[kMaxGradientStops * 4] = {};
    bool vertexColorsLinear = false; // OpenGL upload can skip repeated conversion
    const std::vector<float> &pointData() const
    {
        return sharedGeometry ? sharedGeometry->points : points;
    }
    const std::vector<float> &coverageData() const
    {
        return sharedGeometry ? sharedGeometry->coverage : coverage;
    }
    const std::vector<std::uint32_t> &indexData() const
    {
        return sharedGeometry ? sharedGeometry->indices : indices;
    }
    size_t getPointCount() const { return pointData().size() / 2; }
    bool hasVertexColors() const { return colors.size() == getPointCount() * 4; }
    bool hasCoverage() const { return coverageData().size() == getPointCount(); }
    bool hasShortIndices() const { return !shortIndices.empty(); }
    bool hasLongIndices() const { return !indexData().empty(); }
    bool hasIndices() const { return hasShortIndices() || hasLongIndices(); }
    size_t getElementCount() const
    {
        if (hasShortIndices()) {
            return shortIndices.size();
        }
        return hasLongIndices() ? indexData().size() : getPointCount();
    }
    std::uint32_t getIndex(size_t element) const
    {
        return hasShortIndices()
            ? static_cast<std::uint32_t>(shortIndices[element])
            : indexData()[element];
    }
    bool hasShaderGradient() const { return gradientType != DrawGradientType::None && gradientStopCount > 0; }
};

struct DrawImageData {
    static constexpr std::size_t kMaxGradientStops = 8;

    SharedImageResource imageResource;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 1.0f;
    float v1 = 1.0f;
    // Uniform rounded-rectangle clip evaluated directly by image backends.
    // Complex/non-uniform rounded clips continue to use ClipMaskState.
    float roundedRadius = 0.0f;
    float tintColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    bool hasColorMatrix = false;
    float colorMatrix[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    float colorMatrixOffset[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float alpha = 1.0f;
    // RGB LCD coverage mask generated by DirectWrite ClearType.  This is not a
    // general image mode: OpenGL uses dual-source blending so each colour
    // channel attenuates the opaque destination independently.
    bool clearTypeMask = false;
    // The source texture contains RGB LCD coverage even if dual-source
    // composition is ineligible or unavailable. The ordinary path must then
    // collapse it to texColor.a instead of treating the RGB channels as color.
    bool rgbCoverageMask = false;
    DrawImageSampling sampling = DrawImageSampling::Linear;
    DrawImageTileMode tileMode = DrawImageTileMode::Clamp;
    bool mipmapsReady = false;
    glm::mat4 transform = glm::mat4(1.0f);
    ScissorState scissor;
    DrawBlendMode blendMode = DrawBlendMode::SrcOver;
    ClipMaskState clipMask;
    DrawGradientType gradientType = DrawGradientType::None;
    DrawGradientTileMode gradientTileMode = DrawGradientTileMode::Clamp;
    float gradientStart[2] = {0.0f, 0.0f};
    float gradientEnd[2] = {1.0f, 0.0f};
    float radialCenter[2] = {0.0f, 0.0f};
    float radialRadius = 1.0f;
    int gradientStopCount = 0;
    float gradientStopPositions[kMaxGradientStops] = {};
    float gradientStopColors[kMaxGradientStops * 4] = {};
    bool hasShaderGradient() const { return gradientType != DrawGradientType::None && gradientStopCount > 0; }
    bool hasRoundedCorners() const { return roundedRadius > 0.0f; }
};

struct DrawImageBatchQuad {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 1.0f;
    float v1 = 1.0f;
};

/// Compact batch for atlas-backed glyph runs and other compatible image
/// quads. Complex image state remains represented by DrawImageData.
struct DrawImageBatchData {
    SharedImageResource imageResource;
    std::vector<DrawImageBatchQuad> quads;
    float tintColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float alpha = 1.0f;
    glm::mat4 transform = glm::mat4(1.0f);
    ScissorState scissor;
    DrawBlendMode blendMode = DrawBlendMode::SrcOver;
    ClipMaskState clipMask;
};

struct DrawTextData {
    static constexpr std::size_t kMaxGradientStops = 8;

    std::vector<float> vertices; // Triangles, interleaved x,y.
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    glm::mat4 transform = glm::mat4(1.0f);
    ScissorState scissor;
    DrawBlendMode blendMode = DrawBlendMode::SrcOver;
    ClipMaskState clipMask;
    DrawGradientType gradientType = DrawGradientType::None;
    DrawGradientTileMode gradientTileMode = DrawGradientTileMode::Clamp;
    float gradientStart[2] = {0.0f, 0.0f};
    float gradientEnd[2] = {1.0f, 0.0f};
    float radialCenter[2] = {0.0f, 0.0f};
    float radialRadius = 1.0f;
    int gradientStopCount = 0;
    float gradientStopPositions[kMaxGradientStops] = {};
    float gradientStopColors[kMaxGradientStops * 4] = {};
    size_t getVertexCount() const { return vertices.size() / 2; }
    bool hasShaderGradient() const { return gradientType != DrawGradientType::None && gradientStopCount > 0; }
};

/// A true (separable Gaussian) blurred shadow. The silhouette is a white fill
/// (with the shadow offset baked into its transform) rendered offscreen, blurred
/// on the GPU, then composited into the frame tinted with `color`. Resolved
/// entirely at flush time so no transient blur target outlives its use.
struct DrawShadowData {
    DrawPathData silhouette;      // white fill of the shape at the shadow offset
    // Optional textured silhouette (glyph atlas / bitmap text). When non-empty
    // these are rendered (their texture alpha becomes coverage) instead of the
    // path silhouette, so texture-based text also casts a true Gaussian shadow.
    std::vector<DrawImageData> imageSilhouette;
    float color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float blurRadius = 0.0f;      // device pixels
    int canvasWidth = 0;
    int canvasHeight = 0;
    ScissorState scissor;
    DrawBlendMode blendMode = DrawBlendMode::SrcOver;
};

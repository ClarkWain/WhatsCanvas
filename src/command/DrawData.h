#pragma once
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

struct DrawPathData {
    static constexpr std::size_t kMaxGradientStops = 8;

    std::vector<float> points;    // Path points, each storing x/y coordinates
    std::vector<float> colors;    // Optional per-vertex colors, each storing r/g/b/a
    std::vector<float> coverage;  // Optional per-vertex analytic-AA coverage in [0,1]
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
    size_t getPointCount() const { return points.size() / 2; }
    bool hasVertexColors() const { return colors.size() == getPointCount() * 4; }
    bool hasCoverage() const { return coverage.size() == getPointCount(); }
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

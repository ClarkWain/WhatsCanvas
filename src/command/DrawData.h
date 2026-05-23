#pragma once
#include <vector>
#include <glm.hpp>

struct ScissorState {
    bool enabled = false;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

enum class DrawBlendMode {
    SrcOver,
    Add,
    Multiply,
    Screen
};

enum class DrawImageSampling {
    Linear,
    Nearest,
    MipmapLinear
};

enum class DrawImageTileMode {
    Clamp,
    Repeat,
    Mirror,
    Decal
};

struct DrawPointsData {
    std::vector<float> points;  // 每个点包含x,y坐标
    float size;
    float color[4];
    glm::mat4 transform = glm::mat4(1.0f);
    ScissorState scissor;
    DrawBlendMode blendMode = DrawBlendMode::SrcOver;
    size_t getPointCount() const { return points.size() / 2; }
};


struct DrawLinesData {
    std::vector<float> points;  // 每个点包含x,y坐标
    float width;
    float color[4];
    glm::mat4 transform = glm::mat4(1.0f);
    ScissorState scissor;
    DrawBlendMode blendMode = DrawBlendMode::SrcOver;
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

struct DrawPathData {
    std::vector<float> points;    // 路径点，每个点包含 x, y 坐标
    std::vector<float> colors;    // 可选顶点颜色，每个顶点包含 r,g,b,a
    float width = 1.0f;           // 线条宽度
    float color[4];               // 颜色 RGBA
    PathDrawMode drawMode;        // 绘制模式
    PathCapStyle capStyle;        // 笔锋样式
    glm::mat4 transform = glm::mat4(1.0f);
    ScissorState scissor;
    DrawBlendMode blendMode = DrawBlendMode::SrcOver;
    size_t getPointCount() const { return points.size() / 2; }
    bool hasVertexColors() const { return colors.size() == getPointCount() * 4; }
};

struct DrawImageData {
    unsigned int textureID = 0;
    bool ownsTexture = false;
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
};

struct DrawTextData {
    std::vector<float> vertices; // Triangles, interleaved x,y.
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    glm::mat4 transform = glm::mat4(1.0f);
    ScissorState scissor;
    DrawBlendMode blendMode = DrawBlendMode::SrcOver;
    size_t getVertexCount() const { return vertices.size() / 2; }
};
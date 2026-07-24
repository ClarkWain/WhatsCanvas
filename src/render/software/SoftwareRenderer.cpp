#include "SoftwareRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <glm/glm.hpp>

#include "command/DrawCommand.h"
#include "render/GammaCorrect.h"
#include "render/GaussianKernel.h"
#include "render/software/SoftwarePresent.h"

namespace wsc::software {
namespace {

/// CPU-side image resource holding a straight-alpha RGBA8 buffer. sample()
/// supports nearest/bilinear filtering and clamp/repeat/mirror/decal tile
/// modes; it backs both image draws and bitmap-text atlas resources.
class SoftwareImageResource final : public ImageResource
{
public:
    SoftwareImageResource(int width, int height, std::vector<std::uint8_t> pixels)
        : width_(width), height_(height), pixels_(std::move(pixels))
    {
    }

    bool isValid() const override
    {
        return width_ > 0 && height_ > 0
            && pixels_.size() >= static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 4u;
    }

    void bind(const RenderContext &) const override {}

    bool updateRGBA(int x, int y, int width, int height, const unsigned char *pixels, bool) override
    {
        if (pixels == nullptr) {
            return false;
        }
        for (int row = 0; row < height; ++row) {
            const int dy = y + row;
            if (dy < 0 || dy >= height_) {
                continue;
            }
            for (int col = 0; col < width; ++col) {
                const int dx = x + col;
                if (dx < 0 || dx >= width_) {
                    continue;
                }
                const unsigned char *src = pixels + (static_cast<std::size_t>(row) * width + col) * 4u;
                std::uint8_t *dst = pixels_.data() + (static_cast<std::size_t>(dy) * width_ + dx) * 4u;
                dst[0] = src[0];
                dst[1] = src[1];
                dst[2] = src[2];
                dst[3] = src[3];
            }
        }
        return true;
    }

    int width() const { return width_; }
    int height() const { return height_; }
    const std::vector<std::uint8_t> &pixels() const { return pixels_; }

    /// Sample straight-alpha RGBA in [0,1] at texture coordinate (u,v).
    /// samplingMode: 0/2 = bilinear, 1 = nearest. tileMode: 0 clamp, 1 repeat,
    /// 2 mirror, 3 decal (transparent outside [0,1]).
    void sample(float u, float v, int samplingMode, int tileMode, float out[4]) const
    {
        if (tileMode == 3 && (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f)) {
            out[0] = out[1] = out[2] = out[3] = 0.0f;
            return;
        }
        u = wrap(u, tileMode);
        v = wrap(v, tileMode);
        const float fx = u * static_cast<float>(width_) - 0.5f;
        const float fy = v * static_cast<float>(height_) - 0.5f;
        if (samplingMode == 1) {
            fetch(static_cast<int>(std::lround(fx)), static_cast<int>(std::lround(fy)), out);
            return;
        }
        const int x0 = static_cast<int>(std::floor(fx));
        const int y0 = static_cast<int>(std::floor(fy));
        const float tx = fx - x0;
        const float ty = fy - y0;
        float c00[4];
        float c10[4];
        float c01[4];
        float c11[4];
        fetch(x0, y0, c00);
        fetch(x0 + 1, y0, c10);
        fetch(x0, y0 + 1, c01);
        fetch(x0 + 1, y0 + 1, c11);
        for (int c = 0; c < 4; ++c) {
            const float top = c00[c] + (c10[c] - c00[c]) * tx;
            const float bottom = c01[c] + (c11[c] - c01[c]) * tx;
            out[c] = top + (bottom - top) * ty;
        }
    }

private:
    static float wrap(float t, int tileMode)
    {
        if (tileMode == 1) { // repeat
            return t - std::floor(t);
        }
        if (tileMode == 2) { // mirror
            const float period = std::floor(t);
            float localT = t - period;
            if (std::fmod(std::fabs(period), 2.0f) > 0.5f) {
                localT = 1.0f - localT;
            }
            return localT;
        }
        return std::clamp(t, 0.0f, 1.0f); // clamp / decal (decal handled by caller)
    }

    void fetch(int x, int y, float out[4]) const
    {
        x = std::clamp(x, 0, width_ - 1);
        y = std::clamp(y, 0, height_ - 1);
        const std::uint8_t *p = pixels_.data() + (static_cast<std::size_t>(y) * width_ + x) * 4u;
        out[0] = p[0] / 255.0f;
        out[1] = p[1] / 255.0f;
        out[2] = p[2] / 255.0f;
        out[3] = p[3] / 255.0f;
    }

private:
    int width_ = 0;
    int height_ = 0;
    std::vector<std::uint8_t> pixels_;
};

class SoftwareClipMaskResource final : public ClipMaskResource
{
public:
    explicit SoftwareClipMaskResource(const ClipMaskPath &maskPath)
        : points_(maskPath.points), coverage_(maskPath.coverage), transform_(maskPath.transform)
    {
    }

    bool isValid() const override { return !points_.empty(); }
    void apply(const RenderContext &, const ScissorState &, std::size_t) const override {}

    const std::vector<float> &points() const { return points_; }
    const std::vector<float> &coverage() const { return coverage_; }
    const glm::mat4 &transform() const { return transform_; }

private:
    std::vector<float> points_;
    std::vector<float> coverage_;
    glm::mat4 transform_ = glm::mat4(1.0f);
};

inline std::uint8_t toByte(float value)
{
    return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

/// Blend a straight-alpha source over the destination pixel, matching the GL
/// backend's glBlendFuncSeparate settings (see RenderContext::applyBlendMode)
/// for all 14 Porter-Duff / separable modes. When gamma-correct rendering is
/// enabled the RGB channels are blended in linear space and the stored result
/// is re-encoded to sRGB, mirroring the GL backend's sRGB source colors plus
/// GL_FRAMEBUFFER_SRGB. Alpha is always linear and never gamma-converted.
inline void blendPixel(std::uint8_t *dst, float sr, float sg, float sb, float sa, DrawBlendMode mode)
{
    const bool gamma = GammaCorrect::enabled();
    float dr = dst[0] / 255.0f;
    float dg = dst[1] / 255.0f;
    float db = dst[2] / 255.0f;
    const float da = dst[3] / 255.0f;

    if (gamma) {
        sr = GammaCorrect::srgbToLinear(sr);
        sg = GammaCorrect::srgbToLinear(sg);
        sb = GammaCorrect::srgbToLinear(sb);
        dr = GammaCorrect::srgbToLinear(dr);
        dg = GammaCorrect::srgbToLinear(dg);
        db = GammaCorrect::srgbToLinear(db);
    }

    float orr;
    float og;
    float ob;
    float oa;
    switch (mode) {
    case DrawBlendMode::Src:
        orr = sr; og = sg; ob = sb; oa = sa;
        break;
    case DrawBlendMode::Dst:
        orr = dr; og = dg; ob = db; oa = da;
        break;
    case DrawBlendMode::Clear:
        orr = og = ob = oa = 0.0f;
        break;
    case DrawBlendMode::SrcIn:
        orr = sr * da; og = sg * da; ob = sb * da; oa = sa * da;
        break;
    case DrawBlendMode::DstIn:
        orr = dr * sa; og = dg * sa; ob = db * sa; oa = da * sa;
        break;
    case DrawBlendMode::SrcOut:
        orr = sr * (1.0f - da); og = sg * (1.0f - da); ob = sb * (1.0f - da); oa = sa * (1.0f - da);
        break;
    case DrawBlendMode::DstOut:
        orr = dr * (1.0f - sa); og = dg * (1.0f - sa); ob = db * (1.0f - sa); oa = da * (1.0f - sa);
        break;
    case DrawBlendMode::SrcAtop:
        orr = sr * da + dr * (1.0f - sa); og = sg * da + dg * (1.0f - sa); ob = sb * da + db * (1.0f - sa);
        oa = sa * da + da * (1.0f - sa);
        break;
    case DrawBlendMode::DstAtop:
        orr = sr * (1.0f - da) + dr * sa; og = sg * (1.0f - da) + dg * sa; ob = sb * (1.0f - da) + db * sa;
        oa = sa * (1.0f - da) + da * sa;
        break;
    case DrawBlendMode::Xor:
        orr = sr * (1.0f - da) + dr * (1.0f - sa); og = sg * (1.0f - da) + dg * (1.0f - sa);
        ob = sb * (1.0f - da) + db * (1.0f - sa); oa = sa * (1.0f - da) + da * (1.0f - sa);
        break;
    case DrawBlendMode::Add:
        orr = sr * sa + dr; og = sg * sa + dg; ob = sb * sa + db; oa = sa + da;
        break;
    case DrawBlendMode::Multiply:
        orr = sr * dr; og = sg * dg; ob = sb * db; oa = sa * da;
        break;
    case DrawBlendMode::Screen:
        orr = sr + dr * (1.0f - sr); og = sg + dg * (1.0f - sg); ob = sb + db * (1.0f - sb);
        oa = sa + da * (1.0f - sa);
        break;
    case DrawBlendMode::SrcOver:
    default:
        oa = sa + da * (1.0f - sa);
        orr = sr * sa + dr * (1.0f - sa);
        og = sg * sa + dg * (1.0f - sa);
        ob = sb * sa + db * (1.0f - sa);
        break;
    }

    if (gamma) {
        orr = GammaCorrect::linearToSrgb(orr);
        og = GammaCorrect::linearToSrgb(og);
        ob = GammaCorrect::linearToSrgb(ob);
    }

    dst[0] = toByte(orr);
    dst[1] = toByte(og);
    dst[2] = toByte(ob);
    dst[3] = toByte(oa);
}

/// Per-pixel gradient parameters mirroring DrawPathData's gradient fields.
struct GradientDesc
{
    int type = 0; // 0 none, 1 linear, 2 radial
    int tileMode = 0;
    float linearStart[2] = {0.0f, 0.0f};
    float linearEnd[2] = {1.0f, 0.0f};
    float radialCenter[2] = {0.0f, 0.0f};
    float radialRadius = 1.0f;
    int stopCount = 0;
    float stopPositions[8] = {};
    float stopColors[32] = {};
};

inline float applyGradientTile(float t, int tileMode, float &visibility)
{
    visibility = 1.0f;
    if (tileMode == 1) { // repeat
        return t - std::floor(t);
    }
    if (tileMode == 2) { // mirror
        const float period = std::floor(t);
        float localT = t - period;
        if (std::fmod(std::fabs(period), 2.0f) > 0.5f) {
            localT = 1.0f - localT;
        }
        return localT;
    }
    if (tileMode == 3) { // decal
        visibility = (t >= 0.0f && t <= 1.0f) ? 1.0f : 0.0f;
        return std::clamp(t, 0.0f, 1.0f);
    }
    return std::clamp(t, 0.0f, 1.0f);
}

/// Evaluate the gradient at a local-space position, returning straight RGBA.
void sampleGradient(const GradientDesc &grad, float lx, float ly, float out[4])
{
    float t = 0.0f;
    if (grad.type == 1) {
        const float dx = grad.linearEnd[0] - grad.linearStart[0];
        const float dy = grad.linearEnd[1] - grad.linearStart[1];
        const float lenSq = std::max(dx * dx + dy * dy, 0.0001f);
        t = ((lx - grad.linearStart[0]) * dx + (ly - grad.linearStart[1]) * dy) / lenSq;
    } else if (grad.type == 2) {
        const float dx = lx - grad.radialCenter[0];
        const float dy = ly - grad.radialCenter[1];
        t = std::sqrt(dx * dx + dy * dy) / std::max(grad.radialRadius, 0.0001f);
    }

    float visibility = 1.0f;
    t = applyGradientTile(t, grad.tileMode, visibility);
    if (visibility <= 0.0f || grad.stopCount <= 0) {
        out[0] = out[1] = out[2] = out[3] = 0.0f;
        return;
    }

    auto stopColor = [&](int i, int c) { return grad.stopColors[i * 4 + c]; };
    if (grad.stopCount == 1 || t <= grad.stopPositions[0]) {
        for (int c = 0; c < 4; ++c) out[c] = stopColor(0, c) * visibility;
        return;
    }
    for (int i = 1; i < grad.stopCount; ++i) {
        if (t <= grad.stopPositions[i]) {
            const float startPos = grad.stopPositions[i - 1];
            const float endPos = grad.stopPositions[i];
            const float span = std::max(endPos - startPos, 0.0001f);
            const float localT = std::clamp((t - startPos) / span, 0.0f, 1.0f);
            for (int c = 0; c < 4; ++c) {
                out[c] = (stopColor(i - 1, c) + (stopColor(i, c) - stopColor(i - 1, c)) * localT) * visibility;
            }
            return;
        }
    }
    for (int c = 0; c < 4; ++c) out[c] = stopColor(grad.stopCount - 1, c) * visibility;
}

inline float edge(float ax, float ay, float bx, float by, float cx, float cy)
{
    return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
}

/// Top-left fill rule for an edge A->B (triangle wound so area > 0, y-down).
/// A pixel exactly on an edge is covered only if the edge is top-left, so a
/// boundary shared by two triangles is rasterized by exactly one of them and
/// never double-composited.
inline bool edgeIsTopLeft(float ax, float ay, float bx, float by)
{
    const float dx = bx - ax;
    const float dy = by - ay;
    return dy < 0.0f || (dy == 0.0f && dx > 0.0f);
}

/// Per-pixel clip: an optional canvas-sized coverage buffer (from clip paths)
/// and an optional scissor rectangle (already converted to top-down pixels).
struct RasterClip
{
    const float *coverage = nullptr; // width*height, or null
    int width = 0;
    bool hasScissor = false;
    int sx0 = 0;
    int sy0 = 0;
    int sx1 = 0;
    int sy1 = 0;

    inline float at(int px, int py) const
    {
        if (hasScissor && (px < sx0 || px >= sx1 || py < sy0 || py >= sy1)) {
            return 0.0f;
        }
        if (coverage != nullptr) {
            return coverage[static_cast<std::size_t>(py) * width + px];
        }
        return 1.0f;
    }
};

/// Rasterize a clip path's analytic-AA coverage triangles into `dst`, keeping
/// the maximum coverage where interior and fringe triangles overlap.
void rasterizeCoverageTriangles(float *dst, int width, int height, const std::vector<float> &points,
                                const std::vector<float> &coverage, const glm::mat4 &transform)
{
    const std::size_t vertexCount = points.size() / 2;
    if (vertexCount < 3) {
        return;
    }
    const bool hasCoverage = coverage.size() >= vertexCount;

    struct CV { float x; float y; float c; };
    auto makeVertex = [&](std::size_t index) {
        const glm::vec4 device = transform * glm::vec4(points[index * 2], points[index * 2 + 1], 0.0f, 1.0f);
        return CV{device.x, device.y, hasCoverage ? coverage[index] : 1.0f};
    };

    for (std::size_t t = 0; t + 2 < vertexCount; t += 3) {
        const CV v0 = makeVertex(t);
        const CV v1 = makeVertex(t + 1);
        const CV v2 = makeVertex(t + 2);
        const float area = edge(v0.x, v0.y, v1.x, v1.y, v2.x, v2.y);
        if (std::fabs(area) < 1e-7f) {
            continue;
        }
        const float invArea = 1.0f / area;
        int minX = std::max(0, static_cast<int>(std::floor(std::min({v0.x, v1.x, v2.x}))));
        int maxX = std::min(width - 1, static_cast<int>(std::ceil(std::max({v0.x, v1.x, v2.x}))));
        int minY = std::max(0, static_cast<int>(std::floor(std::min({v0.y, v1.y, v2.y}))));
        int maxY = std::min(height - 1, static_cast<int>(std::ceil(std::max({v0.y, v1.y, v2.y}))));
        for (int py = minY; py <= maxY; ++py) {
            for (int px = minX; px <= maxX; ++px) {
                const float sx = px + 0.5f;
                const float sy = py + 0.5f;
                const float b0 = edge(v1.x, v1.y, v2.x, v2.y, sx, sy) * invArea;
                const float b1 = edge(v2.x, v2.y, v0.x, v0.y, sx, sy) * invArea;
                const float b2 = edge(v0.x, v0.y, v1.x, v1.y, sx, sy) * invArea;
                if (b0 < 0.0f || b1 < 0.0f || b2 < 0.0f) {
                    continue;
                }
                const float cov = std::clamp(b0 * v0.c + b1 * v1.c + b2 * v2.c, 0.0f, 1.0f);
                float &slot = dst[static_cast<std::size_t>(py) * width + px];
                slot = std::max(slot, cov);
            }
        }
    }
}

struct Vertex
{
    float x;
    float y;
    float lx; // local (pre-transform) position, for gradient evaluation
    float ly;
    float r;
    float g;
    float b;
    float a;
    float coverage;
};

/// Rasterize an already-tessellated triangle list. `points` is interleaved x,y
/// in canvas space; `transform` maps it to device pixels. Per-vertex `colors`
/// (RGBA) and `coverage` are optional; when absent the uniform color and full
/// coverage are used. When `grad.type != 0` the fill colour is evaluated per
/// pixel from the gradient instead of the interpolated vertex colour.
void rasterizeTriangles(std::uint8_t *framebuffer, int width, int height,
                        const std::vector<float> &points, const std::vector<float> &colors,
                        const std::vector<float> &coverage, const float uniformColor[4],
                        const glm::mat4 &transform, DrawBlendMode blendMode, const GradientDesc &grad,
                        const RasterClip &clip)
{
    const std::size_t vertexCount = points.size() / 2;
    if (vertexCount < 3) {
        return;
    }
    const bool hasColors = colors.size() >= vertexCount * 4;
    const bool hasCoverage = coverage.size() >= vertexCount;

    auto makeVertex = [&](std::size_t index) {
        const glm::vec4 device = transform * glm::vec4(points[index * 2], points[index * 2 + 1], 0.0f, 1.0f);
        Vertex v;
        v.x = device.x;
        v.y = device.y;
        v.lx = points[index * 2];
        v.ly = points[index * 2 + 1];
        if (hasColors) {
            v.r = colors[index * 4 + 0];
            v.g = colors[index * 4 + 1];
            v.b = colors[index * 4 + 2];
            v.a = colors[index * 4 + 3];
        } else {
            v.r = uniformColor[0];
            v.g = uniformColor[1];
            v.b = uniformColor[2];
            v.a = uniformColor[3];
        }
        v.coverage = hasCoverage ? coverage[index] : 1.0f;
        return v;
    };

    for (std::size_t t = 0; t + 2 < vertexCount; t += 3) {
        Vertex v0 = makeVertex(t);
        Vertex v1 = makeVertex(t + 1);
        Vertex v2 = makeVertex(t + 2);

        float area = edge(v0.x, v0.y, v1.x, v1.y, v2.x, v2.y);
        if (std::fabs(area) < 1e-7f) {
            continue;
        }
        if (area < 0.0f) {
            std::swap(v1, v2);
            area = -area;
        }
        const float invArea = 1.0f / area;
        const bool tl0 = edgeIsTopLeft(v1.x, v1.y, v2.x, v2.y);
        const bool tl1 = edgeIsTopLeft(v2.x, v2.y, v0.x, v0.y);
        const bool tl2 = edgeIsTopLeft(v0.x, v0.y, v1.x, v1.y);

        int minX = static_cast<int>(std::floor(std::min({v0.x, v1.x, v2.x})));
        int maxX = static_cast<int>(std::ceil(std::max({v0.x, v1.x, v2.x})));
        int minY = static_cast<int>(std::floor(std::min({v0.y, v1.y, v2.y})));
        int maxY = static_cast<int>(std::ceil(std::max({v0.y, v1.y, v2.y})));
        minX = std::max(minX, 0);
        minY = std::max(minY, 0);
        maxX = std::min(maxX, width - 1);
        maxY = std::min(maxY, height - 1);

        for (int py = minY; py <= maxY; ++py) {
            for (int px = minX; px <= maxX; ++px) {
                const float sx = px + 0.5f;
                const float sy = py + 0.5f;
                const float e0 = edge(v1.x, v1.y, v2.x, v2.y, sx, sy);
                const float e1 = edge(v2.x, v2.y, v0.x, v0.y, sx, sy);
                const float e2 = edge(v0.x, v0.y, v1.x, v1.y, sx, sy);
                const bool in0 = e0 > 0.0f || (e0 == 0.0f && tl0);
                const bool in1 = e1 > 0.0f || (e1 == 0.0f && tl1);
                const bool in2 = e2 > 0.0f || (e2 == 0.0f && tl2);
                if (!(in0 && in1 && in2)) {
                    continue;
                }
                const float b0 = e0 * invArea;
                const float b1 = e1 * invArea;
                const float b2 = e2 * invArea;

                float r;
                float g;
                float bch;
                float a;
                if (grad.type != 0) {
                    const float lx = b0 * v0.lx + b1 * v1.lx + b2 * v2.lx;
                    const float ly = b0 * v0.ly + b1 * v1.ly + b2 * v2.ly;
                    float gc[4];
                    sampleGradient(grad, lx, ly, gc);
                    r = gc[0];
                    g = gc[1];
                    bch = gc[2];
                    a = gc[3];
                } else {
                    r = b0 * v0.r + b1 * v1.r + b2 * v2.r;
                    g = b0 * v0.g + b1 * v1.g + b2 * v2.g;
                    bch = b0 * v0.b + b1 * v1.b + b2 * v2.b;
                    a = b0 * v0.a + b1 * v1.a + b2 * v2.a;
                }
                const float cov = std::clamp(b0 * v0.coverage + b1 * v1.coverage + b2 * v2.coverage, 0.0f, 1.0f);
                const float clipCov = clip.at(px, py);
                if (clipCov <= 0.0f) {
                    continue;
                }
                const float srcA = a * cov * clipCov;
                if (srcA <= 0.0f && blendMode != DrawBlendMode::Clear) {
                    continue;
                }
                std::uint8_t *dst = framebuffer + (static_cast<std::size_t>(py) * width + px) * 4u;
                blendPixel(dst, r, g, bch, srcA, blendMode);
            }
        }
    }
}

GradientDesc makeGradientDesc(const DrawPathData &data)
{
    GradientDesc grad;
    grad.type = static_cast<int>(data.gradientType);
    if (grad.type == 0) {
        return grad;
    }
    grad.tileMode = static_cast<int>(data.gradientTileMode);
    grad.linearStart[0] = data.gradientStart[0];
    grad.linearStart[1] = data.gradientStart[1];
    grad.linearEnd[0] = data.gradientEnd[0];
    grad.linearEnd[1] = data.gradientEnd[1];
    grad.radialCenter[0] = data.radialCenter[0];
    grad.radialCenter[1] = data.radialCenter[1];
    grad.radialRadius = data.radialRadius;
    grad.stopCount = std::min(data.gradientStopCount, 8);
    for (int i = 0; i < grad.stopCount; ++i) {
        grad.stopPositions[i] = data.gradientStopPositions[i];
        for (int c = 0; c < 4; ++c) {
            grad.stopColors[i * 4 + c] = data.gradientStopColors[i * 4 + c];
        }
    }
    return grad;
}

/// Expand a solid line list into quads (matching DrawLinesProgram) and raster.
void rasterizeLines(std::uint8_t *framebuffer, int width, int height, const DrawLinesData &data,
                    const RasterClip &clip)
{
    std::vector<float> quads;
    for (std::size_t i = 0; i + 3 < data.points.size(); i += 4) {
        const float x1 = data.points[i];
        const float y1 = data.points[i + 1];
        const float x2 = data.points[i + 2];
        const float y2 = data.points[i + 3];
        float dx = x2 - x1;
        float dy = y2 - y1;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-6f) {
            continue;
        }
        dx /= len;
        dy /= len;
        const float nx = -dy * data.width * 0.5f;
        const float ny = dx * data.width * 0.5f;
        const float verts[8] = {
            x1 + nx, y1 + ny, x1 - nx, y1 - ny, x2 - nx, y2 - ny, x2 + nx, y2 + ny};
        const int order[6] = {0, 1, 2, 0, 2, 3};
        for (int idx : order) {
            quads.push_back(verts[idx * 2]);
            quads.push_back(verts[idx * 2 + 1]);
        }
    }
    rasterizeTriangles(framebuffer, width, height, quads, {}, {}, data.color, data.transform, data.blendMode,
                       GradientDesc{}, clip);
}

/// Draw each point as a device-space square of side `size` (matching gl_PointSize).
void rasterizePoints(std::uint8_t *framebuffer, int width, int height, const DrawPointsData &data,
                     const RasterClip &clip)
{
    const float half = std::max(data.size, 1.0f) * 0.5f;
    std::vector<float> quads;
    for (std::size_t i = 0; i + 1 < data.points.size(); i += 2) {
        const glm::vec4 device = data.transform * glm::vec4(data.points[i], data.points[i + 1], 0.0f, 1.0f);
        const float cx = device.x;
        const float cy = device.y;
        const float verts[8] = {
            cx - half, cy - half, cx + half, cy - half, cx + half, cy + half, cx - half, cy + half};
        const int order[6] = {0, 1, 2, 0, 2, 3};
        for (int idx : order) {
            quads.push_back(verts[idx * 2]);
            quads.push_back(verts[idx * 2 + 1]);
        }
    }
    // The square is already in device space, so raster with an identity transform.
    rasterizeTriangles(framebuffer, width, height, quads, {}, {}, data.color, glm::mat4(1.0f), data.blendMode,
                       GradientDesc{}, clip);
}

/// Sample a textured quad (with tint, alpha, optional color matrix and the
/// requested sampling/tile modes) matching DrawImageProgram.
void rasterizeImage(std::uint8_t *framebuffer, int width, int height, const DrawImageData &data,
                    const RasterClip &clip)
{
    const auto *image = dynamic_cast<const SoftwareImageResource *>(data.imageResource.get());
    if (image == nullptr || !image->isValid()) {
        return;
    }

    struct IV
    {
        float x;
        float y;
        float u;
        float v;
    };
    auto corner = [&](float cx, float cy, float u, float v) {
        const glm::vec4 d = data.transform * glm::vec4(cx, cy, 0.0f, 1.0f);
        return IV{d.x, d.y, u, v};
    };
    const IV c0 = corner(data.x, data.y, data.u0, data.v0);
    const IV c1 = corner(data.x + data.width, data.y, data.u1, data.v0);
    const IV c2 = corner(data.x + data.width, data.y + data.height, data.u1, data.v1);
    const IV c3 = corner(data.x, data.y + data.height, data.u0, data.v1);
    const IV tris[6] = {c0, c1, c2, c0, c2, c3};

    const int samplingMode = static_cast<int>(data.sampling);
    const int tileMode = static_cast<int>(data.tileMode);

    for (int t = 0; t < 6; t += 3) {
        IV v0 = tris[t];
        IV v1 = tris[t + 1];
        IV v2 = tris[t + 2];
        float area = edge(v0.x, v0.y, v1.x, v1.y, v2.x, v2.y);
        if (std::fabs(area) < 1e-7f) {
            continue;
        }
        if (area < 0.0f) {
            std::swap(v1, v2);
            area = -area;
        }
        const float invArea = 1.0f / area;
        const bool tl0 = edgeIsTopLeft(v1.x, v1.y, v2.x, v2.y);
        const bool tl1 = edgeIsTopLeft(v2.x, v2.y, v0.x, v0.y);
        const bool tl2 = edgeIsTopLeft(v0.x, v0.y, v1.x, v1.y);

        int minX = std::max(0, static_cast<int>(std::floor(std::min({v0.x, v1.x, v2.x}))));
        int maxX = std::min(width - 1, static_cast<int>(std::ceil(std::max({v0.x, v1.x, v2.x}))));
        int minY = std::max(0, static_cast<int>(std::floor(std::min({v0.y, v1.y, v2.y}))));
        int maxY = std::min(height - 1, static_cast<int>(std::ceil(std::max({v0.y, v1.y, v2.y}))));

        for (int py = minY; py <= maxY; ++py) {
            for (int px = minX; px <= maxX; ++px) {
                const float sx = px + 0.5f;
                const float sy = py + 0.5f;
                const float e0 = edge(v1.x, v1.y, v2.x, v2.y, sx, sy);
                const float e1 = edge(v2.x, v2.y, v0.x, v0.y, sx, sy);
                const float e2 = edge(v0.x, v0.y, v1.x, v1.y, sx, sy);
                const bool in0 = e0 > 0.0f || (e0 == 0.0f && tl0);
                const bool in1 = e1 > 0.0f || (e1 == 0.0f && tl1);
                const bool in2 = e2 > 0.0f || (e2 == 0.0f && tl2);
                if (!(in0 && in1 && in2)) {
                    continue;
                }
                const float b0 = e0 * invArea;
                const float b1 = e1 * invArea;
                const float b2 = e2 * invArea;
                const float u = b0 * v0.u + b1 * v1.u + b2 * v2.u;
                const float v = b0 * v0.v + b1 * v1.v + b2 * v2.v;
                float tex[4];
                image->sample(u, v, samplingMode, tileMode, tex);

                float r = tex[0] * data.tintColor[0];
                float g = tex[1] * data.tintColor[1];
                float bch = tex[2] * data.tintColor[2];
                float a = tex[3] * data.tintColor[3] * data.alpha;
                if (data.hasColorMatrix) {
                    const float *m = data.colorMatrix;
                    const float nr = m[0] * r + m[4] * g + m[8] * bch + m[12] * a + data.colorMatrixOffset[0];
                    const float ng = m[1] * r + m[5] * g + m[9] * bch + m[13] * a + data.colorMatrixOffset[1];
                    const float nb = m[2] * r + m[6] * g + m[10] * bch + m[14] * a + data.colorMatrixOffset[2];
                    const float na = m[3] * r + m[7] * g + m[11] * bch + m[15] * a + data.colorMatrixOffset[3];
                    r = std::clamp(nr, 0.0f, 1.0f);
                    g = std::clamp(ng, 0.0f, 1.0f);
                    bch = std::clamp(nb, 0.0f, 1.0f);
                    a = std::clamp(na, 0.0f, 1.0f);
                }
                const float clipCov = clip.at(px, py);
                if (clipCov <= 0.0f) {
                    continue;
                }
                a *= clipCov;
                if (a <= 0.0f && data.blendMode != DrawBlendMode::Clear) {
                    continue;
                }
                std::uint8_t *dst = framebuffer + (static_cast<std::size_t>(py) * width + px) * 4u;
                blendPixel(dst, r, g, bch, a, data.blendMode);
            }
        }
    }
}

/// Separable Gaussian blur of a single-channel (alpha) buffer, clamped at edges.
void blurAlpha(std::vector<float> &buffer, int width, int height, const wsc::render::GaussianKernel &kernel)
{
    const int radius = kernel.radius();
    if (radius <= 0) {
        return;
    }
    std::vector<float> temp(buffer.size(), 0.0f);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float sum = buffer[static_cast<std::size_t>(y) * width + x] * kernel.weights[0];
            for (int i = 1; i <= radius; ++i) {
                const int xl = std::clamp(x - i, 0, width - 1);
                const int xr = std::clamp(x + i, 0, width - 1);
                sum += (buffer[static_cast<std::size_t>(y) * width + xl]
                        + buffer[static_cast<std::size_t>(y) * width + xr])
                       * kernel.weights[static_cast<std::size_t>(i)];
            }
            temp[static_cast<std::size_t>(y) * width + x] = sum;
        }
    }
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float sum = temp[static_cast<std::size_t>(y) * width + x] * kernel.weights[0];
            for (int i = 1; i <= radius; ++i) {
                const int yl = std::clamp(y - i, 0, height - 1);
                const int yr = std::clamp(y + i, 0, height - 1);
                sum += (temp[static_cast<std::size_t>(yl) * width + x]
                        + temp[static_cast<std::size_t>(yr) * width + x])
                       * kernel.weights[static_cast<std::size_t>(i)];
            }
            buffer[static_cast<std::size_t>(y) * width + x] = sum;
        }
    }
}

/// Blur straight-alpha RGBA without introducing transparent-edge color halos.
/// RGB is premultiplied before convolution and unpremultiplied afterwards.
void blurRGBA(std::vector<std::uint8_t> &pixels, int width, int height,
              const wsc::render::GaussianKernel &kernelX,
              const wsc::render::GaussianKernel &kernelY,
              wsc::ImageFilter::TileMode tileMode)
{
    if (width <= 0 || height <= 0
        || pixels.size() < static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u) {
        return;
    }

    const std::size_t componentCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    std::vector<float> source(componentCount, 0.0f);
    std::vector<float> temp(componentCount, 0.0f);
    std::vector<float> output(componentCount, 0.0f);
    for (std::size_t i = 0; i < componentCount; i += 4u) {
        const float alpha = pixels[i + 3u] / 255.0f;
        source[i] = (pixels[i] / 255.0f) * alpha;
        source[i + 1u] = (pixels[i + 1u] / 255.0f) * alpha;
        source[i + 2u] = (pixels[i + 2u] / 255.0f) * alpha;
        source[i + 3u] = alpha;
    }

    const bool decal = tileMode == wsc::ImageFilter::TileMode::Decal;
    auto accumulate = [&](const std::vector<float> &input, int x, int y, int component,
                          bool horizontal, const wsc::render::GaussianKernel &kernel) {
        float sum = input[(static_cast<std::size_t>(y) * width + x) * 4u + component] * kernel.weights[0];
        for (int i = 1; i <= kernel.radius(); ++i) {
            int x0 = horizontal ? x - i : x;
            int x1 = horizontal ? x + i : x;
            int y0 = horizontal ? y : y - i;
            int y1 = horizontal ? y : y + i;
            const bool valid0 = x0 >= 0 && x0 < width && y0 >= 0 && y0 < height;
            const bool valid1 = x1 >= 0 && x1 < width && y1 >= 0 && y1 < height;
            if (!decal || valid0) {
                x0 = std::clamp(x0, 0, width - 1);
                y0 = std::clamp(y0, 0, height - 1);
                sum += input[(static_cast<std::size_t>(y0) * width + x0) * 4u + component]
                     * kernel.weights[static_cast<std::size_t>(i)];
            }
            if (!decal || valid1) {
                x1 = std::clamp(x1, 0, width - 1);
                y1 = std::clamp(y1, 0, height - 1);
                sum += input[(static_cast<std::size_t>(y1) * width + x1) * 4u + component]
                     * kernel.weights[static_cast<std::size_t>(i)];
            }
        }
        return sum;
    };

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            for (int c = 0; c < 4; ++c) {
                temp[(static_cast<std::size_t>(y) * width + x) * 4u + c] =
                    accumulate(source, x, y, c, true, kernelX);
            }
        }
    }
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            for (int c = 0; c < 4; ++c) {
                output[(static_cast<std::size_t>(y) * width + x) * 4u + c] =
                    accumulate(temp, x, y, c, false, kernelY);
            }
        }
    }

    for (std::size_t i = 0; i < componentCount; i += 4u) {
        const float alpha = std::clamp(output[i + 3u], 0.0f, 1.0f);
        const float invAlpha = alpha > 1e-6f ? 1.0f / alpha : 0.0f;
        pixels[i] = toByte(output[i] * invAlpha);
        pixels[i + 1u] = toByte(output[i + 1u] * invAlpha);
        pixels[i + 2u] = toByte(output[i + 2u] * invAlpha);
        pixels[i + 3u] = toByte(alpha);
    }
}

void adjustRGBA(std::vector<std::uint8_t> &pixels, const wsc::ImageFilter &filter)
{
    if (!filter.hasColorAdjustment() && !filter.hasGrain()) {
        return;
    }

    constexpr float lumaR = 0.2126f;
    constexpr float lumaG = 0.7152f;
    constexpr float lumaB = 0.0722f;
    for (std::size_t i = 0; i + 3u < pixels.size(); i += 4u) {
        float r = pixels[i] / 255.0f;
        float g = pixels[i + 1u] / 255.0f;
        float b = pixels[i + 2u] / 255.0f;
        const float luma = r * lumaR + g * lumaG + b * lumaB;
        r = luma + (r - luma) * filter.saturation();
        g = luma + (g - luma) * filter.saturation();
        b = luma + (b - luma) * filter.saturation();
        r = ((r - 0.5f) * filter.contrast() + 0.5f) * filter.brightness();
        g = ((g - 0.5f) * filter.contrast() + 0.5f) * filter.brightness();
        b = ((b - 0.5f) * filter.contrast() + 0.5f) * filter.brightness();
        std::uint32_t hash = static_cast<std::uint32_t>(i / 4u) * 747796405u + 2891336453u;
        hash = ((hash >> ((hash >> 28u) + 4u)) ^ hash) * 277803737u;
        hash = (hash >> 22u) ^ hash;
        const float grain = (static_cast<float>(hash & 0xffffu) / 65535.0f - 0.5f)
            * filter.grain();
        r += grain;
        g += grain;
        b += grain;
        pixels[i] = toByte(r);
        pixels[i + 1u] = toByte(g);
        pixels[i + 2u] = toByte(b);
    }
}

/// True separable-Gaussian drop shadow: rasterize the silhouette coverage, blur
/// it, then composite the tinted blurred coverage into the framebuffer. `extra`
/// offsets the silhouette and scissor for offscreen layers; `canvasHeight` is
/// the flip height for the GL bottom-up scissor.
void rasterizeShadow(std::uint8_t *framebuffer, int width, int height, int canvasHeight,
                     const glm::mat4 &extra, const DrawShadowData &data)
{
    if (width <= 0 || height <= 0 || !(data.blurRadius > 0.0f)) {
        return;
    }
    std::vector<float> alpha(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0.0f);

    if (!data.silhouette.points.empty()) {
        rasterizeCoverageTriangles(alpha.data(), width, height, data.silhouette.points,
                                   data.silhouette.coverage, extra * data.silhouette.transform);
    }
    for (const DrawImageData &quad : data.imageSilhouette) {
        const auto *image = dynamic_cast<const SoftwareImageResource *>(quad.imageResource.get());
        if (image == nullptr || !image->isValid()) {
            continue;
        }
        struct IV { float x; float y; float u; float v; };
        auto corner = [&](float cx, float cy, float u, float v) {
            const glm::vec4 d = extra * quad.transform * glm::vec4(cx, cy, 0.0f, 1.0f);
            return IV{d.x, d.y, u, v};
        };
        const IV c0 = corner(quad.x, quad.y, quad.u0, quad.v0);
        const IV c1 = corner(quad.x + quad.width, quad.y, quad.u1, quad.v0);
        const IV c2 = corner(quad.x + quad.width, quad.y + quad.height, quad.u1, quad.v1);
        const IV c3 = corner(quad.x, quad.y + quad.height, quad.u0, quad.v1);
        const IV tris[6] = {c0, c1, c2, c0, c2, c3};
        const int samplingMode = static_cast<int>(quad.sampling);
        const int tileMode = static_cast<int>(quad.tileMode);
        for (int t = 0; t < 6; t += 3) {
            const IV &v0 = tris[t];
            const IV &v1 = tris[t + 1];
            const IV &v2 = tris[t + 2];
            const float area = edge(v0.x, v0.y, v1.x, v1.y, v2.x, v2.y);
            if (std::fabs(area) < 1e-7f) {
                continue;
            }
            const float invArea = 1.0f / area;
            int minX = std::max(0, static_cast<int>(std::floor(std::min({v0.x, v1.x, v2.x}))));
            int maxX = std::min(width - 1, static_cast<int>(std::ceil(std::max({v0.x, v1.x, v2.x}))));
            int minY = std::max(0, static_cast<int>(std::floor(std::min({v0.y, v1.y, v2.y}))));
            int maxY = std::min(height - 1, static_cast<int>(std::ceil(std::max({v0.y, v1.y, v2.y}))));
            for (int py = minY; py <= maxY; ++py) {
                for (int px = minX; px <= maxX; ++px) {
                    const float sx = px + 0.5f;
                    const float sy = py + 0.5f;
                    const float b0 = edge(v1.x, v1.y, v2.x, v2.y, sx, sy) * invArea;
                    const float b1 = edge(v2.x, v2.y, v0.x, v0.y, sx, sy) * invArea;
                    const float b2 = edge(v0.x, v0.y, v1.x, v1.y, sx, sy) * invArea;
                    if (b0 < 0.0f || b1 < 0.0f || b2 < 0.0f) {
                        continue;
                    }
                    float tex[4];
                    image->sample(b0 * v0.u + b1 * v1.u + b2 * v2.u,
                                  b0 * v0.v + b1 * v1.v + b2 * v2.v, samplingMode, tileMode, tex);
                    float &slot = alpha[static_cast<std::size_t>(py) * width + px];
                    slot = std::max(slot, tex[3]);
                }
            }
        }
    }

    blurAlpha(alpha, width, height, wsc::render::computeGaussianKernel(data.blurRadius));

    RasterClip clip;
    clip.width = width;
    if (data.scissor.enabled) {
        const int ox = static_cast<int>(std::lround(extra[3][0]));
        const int oy = static_cast<int>(std::lround(extra[3][1]));
        clip.hasScissor = true;
        clip.sx0 = data.scissor.x + ox;
        clip.sx1 = data.scissor.x + data.scissor.width + ox;
        clip.sy0 = canvasHeight - data.scissor.y - data.scissor.height + oy;
        clip.sy1 = canvasHeight - data.scissor.y + oy;
    }

    for (int py = 0; py < height; ++py) {
        for (int px = 0; px < width; ++px) {
            const float coverage = alpha[static_cast<std::size_t>(py) * width + px];
            const float srcA = coverage * data.color[3] * clip.at(px, py);
            if (srcA <= 0.0f) {
                continue;
            }
            std::uint8_t *dst = framebuffer + (static_cast<std::size_t>(py) * width + px) * 4u;
            blendPixel(dst, data.color[0], data.color[1], data.color[2], srcA, data.blendMode);
        }
    }
}

/// Combine all active clip paths into a single coverage buffer (1 inside the
/// intersection, 0 outside, with an anti-aliased ramp along every edge).
/// `extra` is an additional device-space transform (used for offscreen layers).
std::vector<float> buildClipCoverage(const ClipMaskState &clip, int width, int height, const glm::mat4 &extra)
{
    std::vector<float> combined(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 1.0f);
    std::vector<float> temp(combined.size());
    for (const auto &res : clip.resources) {
        const auto *mask = dynamic_cast<const SoftwareClipMaskResource *>(res.get());
        if (mask == nullptr || !mask->isValid()) {
            continue;
        }
        std::fill(temp.begin(), temp.end(), 0.0f);
        rasterizeCoverageTriangles(temp.data(), width, height, mask->points(), mask->coverage(),
                                   extra * mask->transform());
        for (std::size_t i = 0; i < combined.size(); ++i) {
            combined[i] *= temp[i];
        }
    }
    return combined;
}

/// Mutable clip-coverage cache keyed by clip fingerprint (+ target size).
struct ClipCache
{
    std::vector<float> coverage;
    std::uint64_t fingerprint = 0;
    bool valid = false;
};

RasterClip makeRasterClip(const ScissorState &scissor, const ClipMaskState &clipMask, int width, int height,
                          int flipHeight, const glm::mat4 &extra, ClipCache *cache)
{
    RasterClip rc;
    rc.width = width;
    if (scissor.enabled) {
        // The scissor is a GL bottom-up rectangle in canvas space; flip it with
        // the canvas height, then apply the same device-space offset as the
        // geometry (identity for the main framebuffer, a translation for an
        // offscreen layer target) so partial layers clip in target space.
        const int ox = static_cast<int>(std::lround(extra[3][0]));
        const int oy = static_cast<int>(std::lround(extra[3][1]));
        rc.hasScissor = true;
        rc.sx0 = scissor.x + ox;
        rc.sx1 = scissor.x + scissor.width + ox;
        rc.sy0 = flipHeight - scissor.y - scissor.height + oy;
        rc.sy1 = flipHeight - scissor.y + oy;
    }
    if (clipMask.hasPaths()) {
        if (cache != nullptr) {
            if (!cache->valid || cache->fingerprint != clipMask.fingerprint
                || cache->coverage.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) {
                cache->coverage = buildClipCoverage(clipMask, width, height, extra);
                cache->fingerprint = clipMask.fingerprint;
                cache->valid = true;
            }
            rc.coverage = cache->coverage.data();
        } else {
            // No caching (offscreen path): the caller owns the returned storage.
            rc.coverage = nullptr;
        }
    }
    return rc;
}

/// Execute a command list into a target framebuffer. `extra` offsets geometry
/// (for offscreen layers); `cache` (optional) reuses clip coverage across
/// consecutive commands that share a clip.
void executeCommandList(std::uint8_t *framebuffer, int width, int height, int canvasHeight, const glm::mat4 &extra,
                        const std::vector<std::unique_ptr<Command>> &commands, ClipCache *cache)
{
    std::vector<float> localClip; // owns clip coverage when no cache is provided
    auto clipFor = [&](const ScissorState &scissor, const ClipMaskState &clipMask) -> RasterClip {
        RasterClip rc = makeRasterClip(scissor, clipMask, width, height, canvasHeight, extra, cache);
        if (cache == nullptr && clipMask.hasPaths()) {
            localClip = buildClipCoverage(clipMask, width, height, extra);
            rc.coverage = localClip.data();
        }
        return rc;
    };

    for (const auto &commandPtr : commands) {
        if (!commandPtr) {
            continue;
        }
        const Command &command = *commandPtr;
        switch (command.type()) {
        case Command::Type::Path: {
            const DrawPathData &data = static_cast<const DrawPathCommand &>(command).data();
            rasterizeTriangles(framebuffer, width, height, data.points, data.colors, data.coverage, data.color,
                               extra * data.transform, data.blendMode, makeGradientDesc(data),
                               clipFor(data.scissor, data.clipMask));
            break;
        }
        case Command::Type::Text: {
            const DrawTextData &data = static_cast<const DrawTextCommand &>(command).data();
            rasterizeTriangles(framebuffer, width, height, data.vertices, {}, {}, data.color,
                               extra * data.transform, data.blendMode, GradientDesc{},
                               clipFor(data.scissor, data.clipMask));
            break;
        }
        case Command::Type::Points: {
            DrawPointsData data = static_cast<const DrawPointsCommand &>(command).data();
            data.transform = extra * data.transform;
            rasterizePoints(framebuffer, width, height, data, clipFor(data.scissor, data.clipMask));
            break;
        }
        case Command::Type::Lines: {
            DrawLinesData data = static_cast<const DrawLinesCommand &>(command).data();
            data.transform = extra * data.transform;
            rasterizeLines(framebuffer, width, height, data, clipFor(data.scissor, data.clipMask));
            break;
        }
        case Command::Type::Image: {
            DrawImageData data = static_cast<const DrawImageCommand &>(command).data();
            data.transform = extra * data.transform;
            rasterizeImage(framebuffer, width, height, data, clipFor(data.scissor, data.clipMask));
            break;
        }
        case Command::Type::Shadow: {
            rasterizeShadow(framebuffer, width, height, canvasHeight, extra,
                            static_cast<const DrawShadowCommand &>(command).data());
            break;
        }
        default:
            break;
        }
    }
}

} // namespace

SoftwareRenderer::SoftwareRenderer(int width, int height)
    : width_(std::max(0, width)), height_(std::max(0, height))
{
    ensureFramebuffer();
}

void SoftwareRenderer::initializeBackend() {}
void SoftwareRenderer::finalizeBackend() {}

void SoftwareRenderer::setViewport(int width, int height)
{
    width_ = std::max(0, width);
    height_ = std::max(0, height);
    ensureFramebuffer();
}

void SoftwareRenderer::ensureFramebuffer()
{
    const std::size_t needed = static_cast<std::size_t>(std::max(0, width_))
                             * static_cast<std::size_t>(std::max(0, height_)) * 4u;
    if (framebuffer_.size() != needed) {
        framebuffer_.assign(needed, 0);
    }
}

void SoftwareRenderer::clearFramebuffer()
{
    std::fill(framebuffer_.begin(), framebuffer_.end(), static_cast<std::uint8_t>(0));
}

void SoftwareRenderer::submit(std::unique_ptr<Command> &&command)
{
    commands_.push_back(std::move(command));
}

size_t SoftwareRenderer::commandCount() const
{
    return commands_.size();
}

std::vector<std::unique_ptr<Command>> SoftwareRenderer::takeCommandsFrom(size_t index)
{
    std::vector<std::unique_ptr<Command>> taken;
    if (index >= commands_.size()) {
        return taken;
    }
    taken.reserve(commands_.size() - index);
    for (std::size_t i = index; i < commands_.size(); ++i) {
        taken.push_back(std::move(commands_[i]));
    }
    commands_.erase(commands_.begin() + static_cast<std::ptrdiff_t>(index), commands_.end());
    return taken;
}

void SoftwareRenderer::appendCommands(std::vector<std::unique_ptr<Command>> &&commands)
{
    for (auto &command : commands) {
        commands_.push_back(std::move(command));
    }
}

bool SoftwareRenderer::readPixelsRGBA(std::vector<unsigned char> &pixels) const
{
    if (width_ <= 0 || height_ <= 0) {
        pixels.clear();
        return false;
    }
    pixels.assign(framebuffer_.begin(), framebuffer_.end());
    return true;
}

SharedClipMaskResource SoftwareRenderer::createClipMaskResource(const ClipMaskPath &maskPath) const
{
    return std::make_shared<SoftwareClipMaskResource>(maskPath);
}

SharedImageResource SoftwareRenderer::createImageResourceRGBA(int width, int height,
                                                              const std::vector<unsigned char> &pixels) const
{
    std::vector<std::uint8_t> copy(pixels.begin(), pixels.end());
    const std::size_t needed = static_cast<std::size_t>(std::max(0, width))
                             * static_cast<std::size_t>(std::max(0, height)) * 4u;
    copy.resize(needed, 0);
    return std::make_shared<SoftwareImageResource>(width, height, std::move(copy));
}

SharedImageResource SoftwareRenderer::createImageResourceFromImageData(int width, int height, int channels,
                                                                       const unsigned char *pixels, bool) const
{
    const std::size_t pixelCount = static_cast<std::size_t>(std::max(0, width)) * static_cast<std::size_t>(std::max(0, height));
    std::vector<std::uint8_t> rgba(pixelCount * 4u, 0);
    if (pixels != nullptr && channels > 0) {
        for (std::size_t i = 0; i < pixelCount; ++i) {
            const unsigned char *src = pixels + i * static_cast<std::size_t>(channels);
            std::uint8_t *dst = rgba.data() + i * 4u;
            if (channels == 1) {
                // Grayscale: replicate luminance, opaque.
                dst[0] = dst[1] = dst[2] = src[0];
                dst[3] = 255;
            } else if (channels == 2) {
                // Grayscale + alpha (stbi semantics: channel 1 is alpha).
                dst[0] = dst[1] = dst[2] = src[0];
                dst[3] = src[1];
            } else {
                dst[0] = src[0];
                dst[1] = src[1];
                dst[2] = src[2];
                dst[3] = channels > 3 ? src[3] : 255;
            }
        }
    }
    return std::make_shared<SoftwareImageResource>(width, height, std::move(rgba));
}

bool SoftwareRenderer::updateImageResourceRGBA(const SharedImageResource &imageResource, int x, int y, int width,
                                               int height, const unsigned char *pixels, bool regenerateMipmaps) const
{
    if (!imageResource) {
        return false;
    }
    return const_cast<ImageResource &>(*imageResource).updateRGBA(x, y, width, height, pixels, regenerateMipmaps);
}

SharedImageResource SoftwareRenderer::wrapExternalImageResource(ImageResourceHandle) const
{
    return {};
}

const FrameStats &SoftwareRenderer::frameStats() const
{
    return stats_;
}

void SoftwareRenderer::resetFrameStats()
{
    stats_.reset();
}

RenderResourceStats SoftwareRenderer::resourceStats() const
{
    return {};
}

SharedImageResource SoftwareRenderer::renderCommandsToImageResource(const std::vector<std::unique_ptr<Command>> &commands,
                                                                    const OffscreenRenderRequest &request) const
{
    if (commands.empty() || request.canvasWidth <= 0 || request.canvasHeight <= 0
        || request.targetWidth <= 0 || request.targetHeight <= 0) {
        return {};
    }
    const int tw = request.targetWidth;
    const int th = request.targetHeight;
    std::vector<std::uint8_t> target(static_cast<std::size_t>(tw) * static_cast<std::size_t>(th) * 4u, 0);

    // Position the canvas-space content within the (possibly bounds-sized)
    // target. The GL backend uses glViewport(viewportX, viewportY, canvasW,
    // canvasH); the equivalent top-down translation is derived below.
    glm::mat4 extra(1.0f);
    extra[3][0] = static_cast<float>(request.viewportX);
    extra[3][1] = static_cast<float>(th - request.viewportY - request.canvasHeight);
    executeCommandList(target.data(), tw, th, request.canvasHeight, extra, commands, nullptr);

    return std::make_shared<SoftwareImageResource>(tw, th, std::move(target));
}

SharedImageResource SoftwareRenderer::renderQueuedCommandsToImageResource(
    size_t commandEnd, const OffscreenRenderRequest &request) const
{
    if (commandEnd == 0 || commands_.empty()) {
        return {};
    }
    const size_t boundedEnd = std::min(commandEnd, commands_.size());
    if (boundedEnd != commands_.size()) {
        return {};
    }
    return renderCommandsToImageResource(commands_, request);
}

SharedImageResource SoftwareRenderer::filterImageResource(const SharedImageResource &source,
                                                          int width, int height,
                                                          const wsc::ImageFilter &filter) const
{
    const auto *softwareImage = dynamic_cast<const SoftwareImageResource *>(source.get());
    if (softwareImage == nullptr || !softwareImage->isValid()
        || softwareImage->width() != width || softwareImage->height() != height
        || !filter.isValid() || filter.type() != wsc::ImageFilter::Type::Blur) {
        return {};
    }

    std::vector<std::uint8_t> filtered = softwareImage->pixels();
    blurRGBA(filtered, width, height,
             wsc::render::computeGaussianKernel(filter.radiusX()),
             wsc::render::computeGaussianKernel(filter.radiusY()),
             filter.tileMode());
    adjustRGBA(filtered, filter);
    return std::make_shared<SoftwareImageResource>(width, height, std::move(filtered));
}

void SoftwareRenderer::resetRenderState() {}

void SoftwareRenderer::clear()
{
    commands_.clear();
}

void SoftwareRenderer::flush()
{
    ensureFramebuffer();
    if (width_ <= 0 || height_ <= 0) {
        return;
    }
    clearFramebuffer();
    stats_.commandCount += commands_.size();
    stats_.drawCallCount += commands_.size();
    ClipCache cache;
    executeCommandList(framebuffer_.data(), width_, height_, height_, glm::mat4(1.0f), commands_, &cache);
}

bool SoftwareRenderer::supportsPresentation() const
{
    return softwarePresentSupported();
}

std::unique_ptr<ISwapchain> SoftwareRenderer::createSwapchain(const NativeSurface &surface,
                                                             const SwapchainConfig &config)
{
    // The swapchain must not outlive this renderer; it reads the CPU framebuffer
    // on demand via readPixelsRGBA.
    PixelSource source = [this](std::vector<unsigned char> &pixels, int &w, int &h) -> bool {
        if (!readPixelsRGBA(pixels)) {
            return false;
        }
        w = width_;
        h = height_;
        return true;
    };
    return makeSoftwareSwapchain(surface, config, std::move(source));
}

} // namespace wsc::software

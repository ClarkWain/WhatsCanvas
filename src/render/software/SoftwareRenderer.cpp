#include "SoftwareRenderer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <thread>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "command/DrawCommand.h"
#include "command/DrawPathCommandPool.h"
#include "render/GammaCorrect.h"
#include "render/GaussianKernel.h"
#include "render/software/SoftwarePresent.h"

namespace wsc::software {
namespace {

template <typename Function>
void parallelForRows(int rowCount, std::size_t estimatedWork, Function &&function)
{
    constexpr std::size_t kMinParallelWork = 256u * 1024u;
    constexpr unsigned int kMaxWorkers = 8;
    const unsigned int hardwareWorkers = std::thread::hardware_concurrency();
    if (rowCount <= 1 || estimatedWork < kMinParallelWork || hardwareWorkers < 2) {
        function(0, rowCount);
        return;
    }

    const unsigned int workerCount = std::min(
        {hardwareWorkers, kMaxWorkers, static_cast<unsigned int>(rowCount)});
    const int rowsPerWorker =
        (rowCount + static_cast<int>(workerCount) - 1)
        / static_cast<int>(workerCount);
    std::vector<std::thread> workers;
    workers.reserve(workerCount - 1u);
    for (unsigned int worker = 1; worker < workerCount; ++worker) {
        const int begin = static_cast<int>(worker) * rowsPerWorker;
        const int end = std::min(begin + rowsPerWorker, rowCount);
        if (begin < end) {
            workers.emplace_back([begin, end, &function]() {
                function(begin, end);
            });
        }
    }

    function(0, std::min(rowsPerWorker, rowCount));
    for (std::thread &worker : workers) {
        worker.join();
    }
}

/// CPU-side RGBA8 image. Uploaded images use straight alpha; offscreen layer
/// captures use premultiplied alpha and are converted by sample().
class SoftwareImageResource final : public ImageResource
{
public:
    SoftwareImageResource(int width, int height, std::vector<std::uint8_t> pixels,
                          ImageAlphaType alphaType = ImageAlphaType::Straight)
        : width_(width),
          height_(height),
          pixels_(std::move(pixels)),
          alphaType_(alphaType)
    {
    }

    bool isValid() const override
    {
        return width_ > 0 && height_ > 0
            && pixels_.size() >= static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 4u;
    }
    ImageAlphaType alphaType() const override { return alphaType_; }

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

    void samplePixel(int x, int y, float out[4]) const
    {
        fetch(x, y, out);
        unpremultiply(out);
    }

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
            unpremultiply(out);
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
        unpremultiply(out);
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

    void unpremultiply(float out[4]) const
    {
        if (alphaType_ != ImageAlphaType::Premultiplied) {
            return;
        }
        if (out[3] <= 1e-6f) {
            out[0] = out[1] = out[2] = 0.0f;
            return;
        }
        out[0] = std::clamp(out[0] / out[3], 0.0f, 1.0f);
        out[1] = std::clamp(out[1] / out[3], 0.0f, 1.0f);
        out[2] = std::clamp(out[2] / out[3], 0.0f, 1.0f);
    }

private:
    int width_ = 0;
    int height_ = 0;
    std::vector<std::uint8_t> pixels_;
    ImageAlphaType alphaType_ = ImageAlphaType::Straight;
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
    const float scaled = std::clamp(value, 0.0f, 1.0f) * 255.0f;
    return static_cast<std::uint8_t>(scaled + 0.5f);
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
    if (!gamma) {
        if (mode == DrawBlendMode::Dst) {
            return;
        }
        if (mode == DrawBlendMode::Clear) {
            dst[0] = dst[1] = dst[2] = dst[3] = 0;
            return;
        }
        if (mode == DrawBlendMode::Src) {
            dst[0] = toByte(sr);
            dst[1] = toByte(sg);
            dst[2] = toByte(sb);
            dst[3] = toByte(sa);
            return;
        }
        if (mode == DrawBlendMode::SrcOver) {
            if (sa <= 0.0f) {
                return;
            }
            if (sa >= 1.0f) {
                dst[0] = toByte(sr);
                dst[1] = toByte(sg);
                dst[2] = toByte(sb);
                dst[3] = 255;
                return;
            }
            const float inverseAlpha = 1.0f - sa;
            const float dr = dst[0] / 255.0f;
            const float dg = dst[1] / 255.0f;
            const float db = dst[2] / 255.0f;
            const float da = dst[3] / 255.0f;
            dst[0] = toByte(sr * sa + dr * inverseAlpha);
            dst[1] = toByte(sg * sa + dg * inverseAlpha);
            dst[2] = toByte(sb * sa + db * inverseAlpha);
            dst[3] = toByte(sa + da * inverseAlpha);
            return;
        }
    }

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

void compositeImagePixel(std::uint8_t *framebuffer, int width, int px, int py,
                         const float tex[4], const DrawImageData &data,
                         const RasterClip &clip, float shapeCoverage = 1.0f,
                         const GradientDesc *grad = nullptr,
                         float localX = 0.0f, float localY = 0.0f)
{
    float tintR = data.tintColor[0];
    float tintG = data.tintColor[1];
    float tintB = data.tintColor[2];
    float tintA = data.tintColor[3];
    if (grad != nullptr && grad->type != 0) {
        float gc[4];
        sampleGradient(*grad, localX, localY, gc);
        tintR = gc[0];
        tintG = gc[1];
        tintB = gc[2];
        tintA = gc[3];
    }
    float r = tex[0] * tintR;
    float g = tex[1] * tintG;
    float b = tex[2] * tintB;
    float a = tex[3] * tintA * data.alpha;
    if (data.hasColorMatrix) {
        const float *m = data.colorMatrix;
        const float nr =
            m[0] * r + m[4] * g + m[8] * b + m[12] * a
            + data.colorMatrixOffset[0];
        const float ng =
            m[1] * r + m[5] * g + m[9] * b + m[13] * a
            + data.colorMatrixOffset[1];
        const float nb =
            m[2] * r + m[6] * g + m[10] * b + m[14] * a
            + data.colorMatrixOffset[2];
        const float na =
            m[3] * r + m[7] * g + m[11] * b + m[15] * a
            + data.colorMatrixOffset[3];
        r = std::clamp(nr, 0.0f, 1.0f);
        g = std::clamp(ng, 0.0f, 1.0f);
        b = std::clamp(nb, 0.0f, 1.0f);
        a = std::clamp(na, 0.0f, 1.0f);
    }
    const float clipCov = clip.at(px, py);
    if (clipCov <= 0.0f) {
        return;
    }
    a *= clipCov * std::clamp(shapeCoverage, 0.0f, 1.0f);
    if (a <= 0.0f && data.blendMode != DrawBlendMode::Clear) {
        return;
    }
    std::uint8_t *dst =
        framebuffer + (static_cast<std::size_t>(py) * width + px) * 4u;
    blendPixel(dst, r, g, b, a, data.blendMode);
}

float roundedImageCoverage(
    const DrawImageData &data, float localX, float localY)
{
    if (!data.hasRoundedCorners()) {
        return 1.0f;
    }
    const float halfWidth = data.width * 0.5f;
    const float halfHeight = data.height * 0.5f;
    const float radius = std::min(
        data.roundedRadius, std::min(halfWidth, halfHeight));
    const float qx =
        std::abs(localX - (data.x + halfWidth)) - (halfWidth - radius);
    const float qy =
        std::abs(localY - (data.y + halfHeight)) - (halfHeight - radius);
    const float outsideX = std::max(qx, 0.0f);
    const float outsideY = std::max(qy, 0.0f);
    const float distanceToEdge =
        std::sqrt(outsideX * outsideX + outsideY * outsideY)
        + std::min(std::max(qx, qy), 0.0f) - radius;
    const float scaleX = std::hypot(data.transform[0][0], data.transform[0][1]);
    const float scaleY = std::hypot(data.transform[1][0], data.transform[1][1]);
    const float deviceScale = std::max(0.5f * (scaleX + scaleY), 1e-4f);
    const float localAaWidth = 1.0f / deviceScale;
    return std::clamp(
        0.5f - distanceToEdge / localAaWidth, 0.0f, 1.0f);
}

/// Rasterize an already-tessellated triangle list. `points` is interleaved x,y
/// in canvas space; `transform` maps it to device pixels. Per-vertex `colors`
/// (RGBA) and `coverage` are optional; when absent the uniform color and full
/// coverage are used. When `grad.type != 0` the fill colour is evaluated per
/// pixel from the gradient instead of the interpolated vertex colour.
void rasterizeTriangles(std::uint8_t *framebuffer, int width, int height,
                        const std::vector<float> &points, const std::vector<float> &colors,
                        const std::vector<float> &coverage,
                        const std::vector<std::uint32_t> &indices,
                        const std::vector<std::uint16_t> &shortIndices,
                        const float uniformColor[4],
                        const glm::mat4 &transform, DrawBlendMode blendMode, const GradientDesc &grad,
                        const RasterClip &clip)
{
    const std::size_t vertexCount = points.size() / 2;
    if (vertexCount < 3) {
        return;
    }
    const bool hasColors = colors.size() >= vertexCount * 4;
    const bool hasCoverage = coverage.size() >= vertexCount;
    const std::size_t elementCount =
        !shortIndices.empty() ? shortIndices.size()
        : (indices.empty() ? vertexCount : indices.size());

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

    const auto indexAt = [&](std::size_t element) {
        if (!shortIndices.empty()) {
            return static_cast<std::size_t>(
                shortIndices[element]);
        }
        return indices.empty()
            ? element
            : static_cast<std::size_t>(
                indices[element]);
    };
    for (std::size_t t = 0; t + 2 < elementCount; t += 3) {
        const std::size_t i0 = indexAt(t);
        const std::size_t i1 = indexAt(t + 1u);
        const std::size_t i2 = indexAt(t + 2u);
        if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount) {
            continue;
        }
        Vertex v0 = makeVertex(i0);
        Vertex v1 = makeVertex(i1);
        Vertex v2 = makeVertex(i2);

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
        const bool triangleNeedsBarycentrics =
            grad.type != 0 || hasColors
            || v0.coverage != 1.0f
            || v1.coverage != 1.0f
            || v2.coverage != 1.0f;

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
                const float clipCov = clip.at(px, py);
                if (clipCov <= 0.0f) {
                    continue;
                }
                std::uint8_t *dst =
                    framebuffer
                    + (static_cast<std::size_t>(py) * width + px) * 4u;
                if (!triangleNeedsBarycentrics) {
                    const float srcA = uniformColor[3] * clipCov;
                    if (srcA > 0.0f || blendMode == DrawBlendMode::Clear) {
                        blendPixel(dst, uniformColor[0], uniformColor[1],
                                   uniformColor[2], srcA, blendMode);
                    }
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
                const float srcA = a * cov * clipCov;
                if (srcA <= 0.0f && blendMode != DrawBlendMode::Clear) {
                    continue;
                }
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
    const DrawPathGradientStops *stops =
        data.gradientStopData();
    // Producers that set `gradientType`/`gradientStopCount` without also
    // allocating the stop table via `writableGradientStops()` leave `stops`
    // null. Drop the gradient rather than dereference through it.
    if (stops == nullptr) {
        grad.stopCount = 0;
        return grad;
    }
    for (int i = 0; i < grad.stopCount; ++i) {
        grad.stopPositions[i] = stops->positions[i];
        for (int c = 0; c < 4; ++c) {
            grad.stopColors[i * 4 + c] =
                stops->colors[i * 4 + c];
        }
    }
    return grad;
}

// Image-command variant. Copies the same gradient fields into GradientDesc so
// the textured raster paths can sample it at each pixel's canvas-logical
// position.
GradientDesc makeGradientDesc(const DrawImageData &data)
{
    GradientDesc grad;
    grad.type = static_cast<int>(data.gradientType);
    if (grad.type == 0) return grad;
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
            grad.stopColors[i * 4 + c] =
                data.gradientStopColors[i * 4 + c];
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
    rasterizeTriangles(framebuffer, width, height, quads, {}, {}, {}, {}, data.color, data.transform, data.blendMode,
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
    rasterizeTriangles(framebuffer, width, height, quads, {}, {}, {}, {}, data.color, glm::mat4(1.0f), data.blendMode,
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

    // Gradient tint mirrors the GradientFill pipeline so a bitmap fill (e.g. a
    // CoreText/DirectWrite glyph atlas) can be modulated by the same Paint
    // linear/radial gradient shapes use, instead of collapsing to the uniform
    // `tintColor`.
    const GradientDesc gradient = makeGradientDesc(data);
    const GradientDesc *gradientPtr = gradient.type != 0 ? &gradient : nullptr;

    struct IV
    {
        float x;
        float y;
        float u;
        float v;
        float localX;
        float localY;
    };
    auto corner = [&](float cx, float cy, float u, float v) {
        const glm::vec4 d = data.transform * glm::vec4(cx, cy, 0.0f, 1.0f);
        return IV{d.x, d.y, u, v, cx, cy};
    };
    const IV c0 = corner(data.x, data.y, data.u0, data.v0);
    const IV c1 = corner(data.x + data.width, data.y, data.u1, data.v0);
    const IV c2 = corner(data.x + data.width, data.y + data.height, data.u1, data.v1);
    const IV c3 = corner(data.x, data.y + data.height, data.u0, data.v1);
    const IV tris[6] = {c0, c1, c2, c0, c2, c3};

    const int samplingMode = static_cast<int>(data.sampling);
    const int tileMode = static_cast<int>(data.tileMode);
    const float rectWidth = c1.x - c0.x;
    const float rectHeight = c3.y - c0.y;
    const bool integerOrigin =
        c0.x == std::floor(c0.x) && c0.y == std::floor(c0.y);
    const bool exactAxisAligned =
        c0.y == c1.y && c1.x == c2.x && c2.y == c3.y && c3.x == c0.x
        && rectWidth == static_cast<float>(image->width())
        && rectHeight == static_cast<float>(image->height())
        && integerOrigin
        && data.u0 == 0.0f && data.u1 == 1.0f
        && (data.v0 == 0.0f || data.v0 == 1.0f)
        && (data.v1 == 0.0f || data.v1 == 1.0f)
        && data.v0 != data.v1
        && data.sampling == DrawImageSampling::Linear
        && data.tileMode == DrawImageTileMode::Clamp;
    if (exactAxisAligned) {
        const int left = static_cast<int>(c0.x);
        const int top = static_cast<int>(c0.y);
        const int beginX = std::max(left, 0);
        const int endX = std::min(left + image->width(), width);
        const int beginY = std::max(top, 0);
        const int endY = std::min(top + image->height(), height);
        const bool flipY = data.v0 > data.v1;
        for (int py = beginY; py < endY; ++py) {
            const int sourceY =
                flipY ? image->height() - 1 - (py - top) : py - top;
            for (int px = beginX; px < endX; ++px) {
                float tex[4];
                image->samplePixel(px - left, sourceY, tex);
                const float localX = static_cast<float>(px - left) + data.x + 0.5f;
                const float localY = static_cast<float>(py - top) + data.y + 0.5f;
                compositeImagePixel(
                    framebuffer, width, px, py, tex, data, clip,
                    roundedImageCoverage(data, localX, localY),
                    gradientPtr, localX, localY);
            }
        }
        return;
    }

    const bool axisAligned =
        c0.y == c1.y && c1.x == c2.x
        && c2.y == c3.y && c3.x == c0.x
        && rectWidth > 0.0f && rectHeight > 0.0f;
    if (axisAligned) {
        const int beginX = std::max(
            0, static_cast<int>(std::ceil(c0.x - 0.5f)));
        const int endX = std::min(
            width, static_cast<int>(std::ceil(c1.x - 0.5f)));
        const int beginY = std::max(
            0, static_cast<int>(std::ceil(c0.y - 0.5f)));
        const int endY = std::min(
            height, static_cast<int>(std::ceil(c3.y - 0.5f)));
        const float inverseWidth = 1.0f / rectWidth;
        const float inverseHeight = 1.0f / rectHeight;
        for (int py = beginY; py < endY; ++py) {
            const float ty =
                (static_cast<float>(py) + 0.5f - c0.y)
                * inverseHeight;
            const float v = data.v0 + (data.v1 - data.v0) * ty;
            const float localY =
                data.y + data.height * ty;
            for (int px = beginX; px < endX; ++px) {
                const float tx =
                    (static_cast<float>(px) + 0.5f - c0.x)
                    * inverseWidth;
                const float u =
                    data.u0 + (data.u1 - data.u0) * tx;
                float tex[4];
                image->sample(u, v, samplingMode, tileMode, tex);
                const float localX = data.x + data.width * tx;
                compositeImagePixel(
                    framebuffer, width, px, py, tex, data, clip,
                    roundedImageCoverage(data, localX, localY),
                    gradientPtr, localX, localY);
            }
        }
        return;
    }

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
                const float localX =
                    b0 * v0.localX + b1 * v1.localX + b2 * v2.localX;
                const float localY =
                    b0 * v0.localY + b1 * v1.localY + b2 * v2.localY;
                float tex[4];
                image->sample(u, v, samplingMode, tileMode, tex);
                compositeImagePixel(
                    framebuffer, width, px, py, tex, data, clip,
                    roundedImageCoverage(data, localX, localY),
                    gradientPtr, localX, localY);
            }
        }
    }
}

void rasterizeImageBatch(
    std::uint8_t *framebuffer, int width, int height,
    const DrawImageBatchData &batch, const glm::mat4 &extra,
    const RasterClip &clip)
{
    DrawImageData image;
    image.imageResource = batch.imageResource;
    std::copy(
        std::begin(batch.tintColor), std::end(batch.tintColor),
        std::begin(image.tintColor));
    image.alpha = batch.alpha;
    image.transform = extra * batch.transform;
    image.scissor = batch.scissor;
    image.blendMode = batch.blendMode;
    image.clipMask = batch.clipMask;
    for (const DrawImageBatchQuad &quad : batch.quads) {
        image.tintColor[0] = batch.tintColor[0]
            * static_cast<float>(quad.packedTint & 0xffu) / 255.0f;
        image.tintColor[1] = batch.tintColor[1]
            * static_cast<float>((quad.packedTint >> 8u) & 0xffu)
                / 255.0f;
        image.tintColor[2] = batch.tintColor[2]
            * static_cast<float>((quad.packedTint >> 16u) & 0xffu)
                / 255.0f;
        image.tintColor[3] = batch.tintColor[3];
        image.alpha = batch.alpha
            * static_cast<float>((quad.packedTint >> 24u) & 0xffu)
                / 255.0f;
        image.x = quad.x;
        image.y = quad.y;
        image.width = quad.width;
        image.height = quad.height;
        image.u0 = quad.u0;
        image.v0 = quad.v0;
        image.u1 = quad.u1;
        image.v1 = quad.v1;
        rasterizeImage(framebuffer, width, height, image, clip);
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
              wsc::ImageFilter::TileMode tileMode,
              ImageAlphaType sourceAlphaType)
{
    if (width <= 0 || height <= 0
        || pixels.size() < static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u) {
        return;
    }

    const std::size_t componentCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    std::vector<float> source(componentCount, 0.0f);
    std::vector<float> temp(componentCount, 0.0f);
    for (std::size_t i = 0; i < componentCount; i += 4u) {
        const float alpha = pixels[i + 3u] / 255.0f;
        const float alphaScale =
            sourceAlphaType == ImageAlphaType::Premultiplied ? 1.0f : alpha;
        source[i] = (pixels[i] / 255.0f) * alphaScale;
        source[i + 1u] = (pixels[i + 1u] / 255.0f) * alphaScale;
        source[i + 2u] = (pixels[i + 2u] / 255.0f) * alphaScale;
        source[i + 3u] = alpha;
    }

    const bool decal = tileMode == wsc::ImageFilter::TileMode::Decal;
    const int radiusX = kernelX.radius();
    const int radiusY = kernelY.radius();
    const std::size_t horizontalWork =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height)
        * static_cast<std::size_t>(std::max(radiusX, 1));
    parallelForRows(height, horizontalWork, [&](int beginY, int endY) {
        for (int y = beginY; y < endY; ++y) {
            const float *sourceRow =
                source.data() + static_cast<std::size_t>(y) * width * 4u;
            float *tempRow =
                temp.data() + static_cast<std::size_t>(y) * width * 4u;
            for (int x = 0; x < width; ++x) {
                const float *center = sourceRow + static_cast<std::size_t>(x) * 4u;
                const float centerWeight = kernelX.weights[0];
                float sum0 = center[0] * centerWeight;
                float sum1 = center[1] * centerWeight;
                float sum2 = center[2] * centerWeight;
                float sum3 = center[3] * centerWeight;

                for (int i = 1; i <= radiusX; ++i) {
                    const float weight = kernelX.weights[static_cast<std::size_t>(i)];
                    const int leftX = x - i;
                    const int rightX = x + i;
                    if (!decal || leftX >= 0) {
                        const int sampleX = leftX >= 0 ? leftX : 0;
                        const float *sample =
                            sourceRow + static_cast<std::size_t>(sampleX) * 4u;
                        sum0 += sample[0] * weight;
                        sum1 += sample[1] * weight;
                        sum2 += sample[2] * weight;
                        sum3 += sample[3] * weight;
                    }
                    if (!decal || rightX < width) {
                        const int sampleX = rightX < width ? rightX : width - 1;
                        const float *sample =
                            sourceRow + static_cast<std::size_t>(sampleX) * 4u;
                        sum0 += sample[0] * weight;
                        sum1 += sample[1] * weight;
                        sum2 += sample[2] * weight;
                        sum3 += sample[3] * weight;
                    }
                }
                float *output = tempRow + static_cast<std::size_t>(x) * 4u;
                output[0] = sum0;
                output[1] = sum1;
                output[2] = sum2;
                output[3] = sum3;
            }
        }
    });

    const std::size_t verticalWork =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height)
        * static_cast<std::size_t>(std::max(radiusY, 1));
    parallelForRows(height, verticalWork, [&](int beginY, int endY) {
        for (int y = beginY; y < endY; ++y) {
            for (int x = 0; x < width; ++x) {
                const float *center =
                    temp.data() + (static_cast<std::size_t>(y) * width + x) * 4u;
                const float centerWeight = kernelY.weights[0];
                float sum0 = center[0] * centerWeight;
                float sum1 = center[1] * centerWeight;
                float sum2 = center[2] * centerWeight;
                float sum3 = center[3] * centerWeight;

                for (int i = 1; i <= radiusY; ++i) {
                    const float weight = kernelY.weights[static_cast<std::size_t>(i)];
                    const int topY = y - i;
                    const int bottomY = y + i;
                    if (!decal || topY >= 0) {
                        const int sampleY = topY >= 0 ? topY : 0;
                        const float *sample =
                            temp.data()
                            + (static_cast<std::size_t>(sampleY) * width + x) * 4u;
                        sum0 += sample[0] * weight;
                        sum1 += sample[1] * weight;
                        sum2 += sample[2] * weight;
                        sum3 += sample[3] * weight;
                    }
                    if (!decal || bottomY < height) {
                        const int sampleY = bottomY < height ? bottomY : height - 1;
                        const float *sample =
                            temp.data()
                            + (static_cast<std::size_t>(sampleY) * width + x) * 4u;
                        sum0 += sample[0] * weight;
                        sum1 += sample[1] * weight;
                        sum2 += sample[2] * weight;
                        sum3 += sample[3] * weight;
                    }
                }

                const float alpha = std::clamp(sum3, 0.0f, 1.0f);
                const float invAlpha = alpha > 1e-6f ? 1.0f / alpha : 0.0f;
                const std::size_t pixel =
                    (static_cast<std::size_t>(y) * width + x) * 4u;
                pixels[pixel] = toByte(sum0 * invAlpha);
                pixels[pixel + 1u] = toByte(sum1 * invAlpha);
                pixels[pixel + 2u] = toByte(sum2 * invAlpha);
                pixels[pixel + 3u] = toByte(alpha);
            }
        }
    });
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

void applyInnerShadow(std::vector<std::uint8_t> &pixels, int width, int height,
                      const wsc::ImageFilter &filter,
                      ImageAlphaType sourceAlphaType)
{
    if (width <= 0 || height <= 0
        || pixels.size()
            < static_cast<std::size_t>(width)
                * static_cast<std::size_t>(height) * 4u) {
        return;
    }

    std::vector<float> sourceAlpha(
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0.0f);
    for (std::size_t i = 0; i < sourceAlpha.size(); ++i) {
        sourceAlpha[i] = pixels[i * 4u + 3u] / 255.0f;
    }

    std::vector<float> temp(sourceAlpha.size(), 0.0f);
    const auto kernelX = wsc::render::computeGaussianKernel(filter.radiusX());
    const auto kernelY = wsc::render::computeGaussianKernel(filter.radiusY());
    for (int y = 0; y < height; ++y) {
        const float *sourceRow =
            sourceAlpha.data() + static_cast<std::size_t>(y) * width;
        float *tempRow =
            temp.data() + static_cast<std::size_t>(y) * width;
        for (int x = 0; x < width; ++x) {
            float sum = sourceRow[x] * kernelX.weights[0];
            for (int i = 1; i <= kernelX.radius(); ++i) {
                const float weight = kernelX.weights[static_cast<std::size_t>(i)];
                const int leftX = x - i;
                const int rightX = x + i;
                float pair = 0.0f;
                if (leftX >= 0) {
                    pair += sourceRow[leftX];
                }
                if (rightX < width) {
                    pair += sourceRow[rightX];
                }
                sum += pair * weight;
            }
            tempRow[x] = sum;
        }
    }
    for (int y = 0; y < height; ++y) {
        float *outputRow =
            sourceAlpha.data() + static_cast<std::size_t>(y) * width;
        for (int x = 0; x < width; ++x) {
            float sum =
                temp[static_cast<std::size_t>(y) * width + x]
                * kernelY.weights[0];
            for (int i = 1; i <= kernelY.radius(); ++i) {
                const float weight = kernelY.weights[static_cast<std::size_t>(i)];
                const int topY = y - i;
                const int bottomY = y + i;
                float pair = 0.0f;
                if (topY >= 0) {
                    pair += temp[static_cast<std::size_t>(topY) * width + x];
                }
                if (bottomY < height) {
                    pair += temp[static_cast<std::size_t>(bottomY) * width + x];
                }
                sum += pair * weight;
            }
            outputRow[x] = sum;
        }
    }

    const Color shadow = filter.shadowColor();
    const float shadowR = shadow.r();
    const float shadowG = shadow.g();
    const float shadowB = shadow.b();
    const float shadowA = shadow.a();
    const float sampleOffsetX = -filter.offsetX();
    const float sampleOffsetY = -filter.offsetY();
    const int sampleBaseX = static_cast<int>(std::floor(sampleOffsetX));
    const int sampleBaseY = static_cast<int>(std::floor(sampleOffsetY));
    const float sampleFractionX = sampleOffsetX - static_cast<float>(sampleBaseX);
    const float sampleFractionY = sampleOffsetY - static_cast<float>(sampleBaseY);
    const bool integerOffset =
        sampleFractionX == 0.0f && sampleFractionY == 0.0f;
    auto sampleShifted = [&](int x, int y) {
        const int x0 = x + sampleBaseX;
        const int y0 = y + sampleBaseY;
        auto sample = [&](int sx, int sy) {
            if (sx < 0 || sx >= width || sy < 0 || sy >= height) {
                return 0.0f;
            }
            return sourceAlpha[static_cast<std::size_t>(sy) * width + sx];
        };
        if (integerOffset) {
            return sample(x0, y0);
        }
        const float top =
            sample(x0, y0) * (1.0f - sampleFractionX)
            + sample(x0 + 1, y0) * sampleFractionX;
        const float bottom =
            sample(x0, y0 + 1) * (1.0f - sampleFractionX)
            + sample(x0 + 1, y0 + 1) * sampleFractionX;
        return top * (1.0f - sampleFractionY)
            + bottom * sampleFractionY;
    };
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t pixelIndex =
                (static_cast<std::size_t>(y) * width + x) * 4u;
            const float alpha = pixels[pixelIndex + 3u] / 255.0f;
            if (alpha <= 1e-6f) {
                pixels[pixelIndex] = 0;
                pixels[pixelIndex + 1u] = 0;
                pixels[pixelIndex + 2u] = 0;
                pixels[pixelIndex + 3u] = 0;
                continue;
            }

            const float shiftedAlpha = sampleShifted(x, y);
            const float coverage =
                std::clamp((alpha - shiftedAlpha) / alpha, 0.0f, 1.0f)
                * shadowA;
            const float alphaScale =
                sourceAlphaType == ImageAlphaType::Premultiplied
                ? 1.0f / alpha : 1.0f;
            const float sourceR =
                std::clamp((pixels[pixelIndex] / 255.0f) * alphaScale, 0.0f, 1.0f);
            const float sourceG =
                std::clamp((pixels[pixelIndex + 1u] / 255.0f) * alphaScale, 0.0f, 1.0f);
            const float sourceB =
                std::clamp((pixels[pixelIndex + 2u] / 255.0f) * alphaScale, 0.0f, 1.0f);
            pixels[pixelIndex] =
                toByte(sourceR * (1.0f - coverage) + shadowR * coverage);
            pixels[pixelIndex + 1u] =
                toByte(sourceG * (1.0f - coverage) + shadowG * coverage);
            pixels[pixelIndex + 2u] =
                toByte(sourceB * (1.0f - coverage) + shadowB * coverage);
            pixels[pixelIndex + 3u] = toByte(alpha);
        }
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

    float minX = static_cast<float>(width);
    float minY = static_cast<float>(height);
    float maxX = 0.0f;
    float maxY = 0.0f;
    bool hasBounds = false;
    auto includePoint = [&](const glm::mat4 &transform, float x, float y) {
        const glm::vec4 point = transform * glm::vec4(x, y, 0.0f, 1.0f);
        minX = std::min(minX, point.x);
        minY = std::min(minY, point.y);
        maxX = std::max(maxX, point.x);
        maxY = std::max(maxY, point.y);
        hasBounds = true;
    };

    const glm::mat4 silhouetteTransform = extra * data.silhouette.transform;
    for (std::size_t i = 0; i + 1 < data.silhouette.points.size(); i += 2) {
        includePoint(
            silhouetteTransform,
            data.silhouette.points[i],
            data.silhouette.points[i + 1]);
    }
    for (const DrawImageData &quad : data.imageSilhouette) {
        const glm::mat4 transform = extra * quad.transform;
        includePoint(transform, quad.x, quad.y);
        includePoint(transform, quad.x + quad.width, quad.y);
        includePoint(transform, quad.x + quad.width, quad.y + quad.height);
        includePoint(transform, quad.x, quad.y + quad.height);
    }
    if (!hasBounds) {
        return;
    }

    const wsc::render::GaussianKernel kernel =
        wsc::render::computeGaussianKernel(data.blurRadius);
    const int outset = kernel.radius() + 1;
    const int left = std::clamp(
        static_cast<int>(std::floor(minX)) - outset, 0, width);
    const int top = std::clamp(
        static_cast<int>(std::floor(minY)) - outset, 0, height);
    const int right = std::clamp(
        static_cast<int>(std::ceil(maxX)) + outset, 0, width);
    const int bottom = std::clamp(
        static_cast<int>(std::ceil(maxY)) + outset, 0, height);
    const int shadowWidth = right - left;
    const int shadowHeight = bottom - top;
    if (shadowWidth <= 0 || shadowHeight <= 0) {
        return;
    }

    std::vector<float> alpha(
        static_cast<std::size_t>(shadowWidth)
            * static_cast<std::size_t>(shadowHeight),
        0.0f);
    const glm::mat4 toShadow =
        glm::translate(
            glm::mat4(1.0f),
            glm::vec3(
                -static_cast<float>(left),
                -static_cast<float>(top),
                0.0f))
        * extra;

    if (!data.silhouette.points.empty()) {
        rasterizeCoverageTriangles(
            alpha.data(), shadowWidth, shadowHeight,
            data.silhouette.points, data.silhouette.coverage,
            toShadow * data.silhouette.transform);
    }
    for (const DrawImageData &quad : data.imageSilhouette) {
        const auto *image = dynamic_cast<const SoftwareImageResource *>(quad.imageResource.get());
        if (image == nullptr || !image->isValid()) {
            continue;
        }
        struct IV { float x; float y; float u; float v; };
        auto corner = [&](float cx, float cy, float u, float v) {
            const glm::vec4 d =
                toShadow * quad.transform * glm::vec4(cx, cy, 0.0f, 1.0f);
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
            int maxX = std::min(shadowWidth - 1, static_cast<int>(std::ceil(std::max({v0.x, v1.x, v2.x}))));
            int minY = std::max(0, static_cast<int>(std::floor(std::min({v0.y, v1.y, v2.y}))));
            int maxY = std::min(shadowHeight - 1, static_cast<int>(std::ceil(std::max({v0.y, v1.y, v2.y}))));
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
                    float &slot =
                        alpha[static_cast<std::size_t>(py) * shadowWidth + px];
                    slot = std::max(slot, tex[3]);
                }
            }
        }
    }

    blurAlpha(alpha, shadowWidth, shadowHeight, kernel);

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

    for (int localY = 0; localY < shadowHeight; ++localY) {
        const int py = top + localY;
        for (int localX = 0; localX < shadowWidth; ++localX) {
            const int px = left + localX;
            const float coverage =
                alpha[static_cast<std::size_t>(localY) * shadowWidth + localX];
            const float srcA =
                coverage * data.color[3] * clip.at(px, py);
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
            rasterizeTriangles(
                framebuffer, width, height, data.pointData(),
                data.colors, data.coverageData(), data.indexData(),
                data.shortIndices, data.color,
                               extra * data.transform, data.blendMode, makeGradientDesc(data),
                               clipFor(data.scissor, data.clipMask));
            break;
        }
        case Command::Type::Text: {
            const DrawTextData &data = static_cast<const DrawTextCommand &>(command).data();
            rasterizeTriangles(framebuffer, width, height, data.vertices, {}, {}, {}, {}, data.color,
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
        case Command::Type::ImageBatch: {
            const DrawImageBatchData &data =
                static_cast<const DrawImageBatchCommand &>(command).data();
            rasterizeImageBatch(
                framebuffer, width, height, data, extra,
                clipFor(data.scissor, data.clipMask));
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

void accumulatePathPipelineStats(
    const std::vector<std::unique_ptr<Command>> &commands,
    FrameStats &stats)
{
    for (const auto &command : commands) {
        if (command == nullptr
            || command->type() != Command::Type::Path) {
            continue;
        }
        const DrawPathData &path =
            static_cast<const DrawPathCommand &>(*command).data();
        stats.pathInputVertexCount +=
            path.sourceVertexCount;
        stats.pathTessellatedVertexCount +=
            path.tessellatedVertexCount;
        stats.pathAaExpandedVertexCount +=
            path.aaExpandedVertexCount;
        stats.pathVertexCount += path.getPointCount();
        stats.pathUploadedVertexCount +=
            path.getPointCount();
        stats.pathIndexCount += path.hasIndices()
            ? path.getElementCount() : 0u;
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
    if (command != nullptr) {
        ++stats_.commandObjectCount;
        if (command->type() == Command::Type::Path
            && wsc::detail::lastDrawPathCommandAllocationReused()) {
            ++stats_.commandPoolReuseCount;
        } else {
            ++stats_.commandAllocationCount;
        }
    }
    commands_.push_back(std::move(command));
}

void SoftwareRenderer::recordCommandClone(
    std::size_t payloadBytes,
    bool pathCommand)
{
    ++stats_.commandObjectCount;
    if (pathCommand
        && wsc::detail::lastDrawPathCommandAllocationReused()) {
        ++stats_.commandPoolReuseCount;
    } else {
        ++stats_.commandAllocationCount;
    }
    ++stats_.commandCloneCount;
    stats_.payloadCopyBytes += payloadBytes;
}

std::vector<DrawImageBatchQuad> *SoftwareRenderer::tryGetImageBatchAppendTarget(
    const DrawImageBatchData &batch,
    std::size_t additionalQuadCount)
{
    if (commands_.size() <= imageBatchAppendFloor_
        || additionalQuadCount == 0
        || batch.scissor.enabled || batch.clipMask.hasPaths()
        || batch.tintColor[0] != 1.0f
        || batch.tintColor[1] != 1.0f
        || batch.tintColor[2] != 1.0f
        || batch.tintColor[3] != 1.0f
        || batch.alpha != 1.0f
        || commands_.back()->type() != Command::Type::ImageBatch) {
        return nullptr;
    }
    auto &previous = static_cast<DrawImageBatchCommand *>(
                         commands_.back().get())
                         ->data();
    if (previous.imageResource != batch.imageResource
        || previous.scissor.enabled
        || previous.clipMask.hasPaths()
        || previous.blendMode != batch.blendMode
        || previous.transform != batch.transform
        || previous.tintColor[0] != 1.0f
        || previous.tintColor[1] != 1.0f
        || previous.tintColor[2] != 1.0f
        || previous.tintColor[3] != 1.0f
        || previous.alpha != 1.0f) {
        return nullptr;
    }

    const std::size_t required =
        previous.quads.size() + additionalQuadCount;
    if (required > previous.quads.capacity()) {
        const std::size_t geometricCapacity =
            previous.quads.capacity() > 0
            ? previous.quads.capacity() * 2u
            : additionalQuadCount;
        previous.quads.reserve(
            std::max(required, geometricCapacity));
    }
    return &previous.quads;
}

size_t SoftwareRenderer::commandCount() const
{
    imageBatchAppendFloor_ = commands_.size();
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
    imageBatchAppendFloor_ = commands_.size();
    return taken;
}

void SoftwareRenderer::appendCommands(std::vector<std::unique_ptr<Command>> &&commands)
{
    for (auto &command : commands) {
        commands_.push_back(std::move(command));
    }
    imageBatchAppendFloor_ = commands_.size();
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

    return std::make_shared<SoftwareImageResource>(
        tw, th, std::move(target), ImageAlphaType::Premultiplied);
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
                                                          const wsc::ImageFilter &filter,
                                                          FilterExecutionStats *executionStats) const
{
    if (executionStats != nullptr) {
        *executionStats = {};
    }
    const auto *softwareImage = dynamic_cast<const SoftwareImageResource *>(source.get());
    if (softwareImage == nullptr || !softwareImage->isValid()
        || softwareImage->width() != width || softwareImage->height() != height
        || !filter.isValid()) {
        return {};
    }

    std::vector<std::uint8_t> filtered = softwareImage->pixels();
    std::size_t passCount = 0;
    if (filter.type() == wsc::ImageFilter::Type::Blur) {
        blurRGBA(filtered, width, height,
                 wsc::render::computeGaussianKernel(filter.radiusX()),
                 wsc::render::computeGaussianKernel(filter.radiusY()),
                 filter.tileMode(), source->alphaType());
        adjustRGBA(filtered, filter);
        const bool adjustmentPass = filter.hasColorAdjustment() || filter.hasGrain();
        passCount = adjustmentPass ? 3u : 2u;
    } else if (filter.type() == wsc::ImageFilter::Type::InnerShadow) {
        applyInnerShadow(filtered, width, height, filter, source->alphaType());
        passCount = 3u;
    } else {
        return {};
    }
    if (executionStats != nullptr) {
        executionStats->passCount = passCount;
        executionStats->pixelPassCount =
            static_cast<std::size_t>(width) * static_cast<std::size_t>(height)
            * passCount;
    }
    ++stats_.filterCount;
    stats_.filterPassCount += passCount;
    stats_.filterInputPixelCount +=
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    stats_.filterPixelPassCount +=
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * passCount;
    return std::make_shared<SoftwareImageResource>(
        width, height, std::move(filtered), ImageAlphaType::Straight);
}

void SoftwareRenderer::recordGenericFilterPass(
    int width, int height) const
{
    ++stats_.filterCount;
    ++stats_.filterPassCount;
    const std::size_t pixels =
        static_cast<std::size_t>(std::max(width, 0))
        * static_cast<std::size_t>(std::max(height, 0));
    stats_.filterInputPixelCount += pixels;
    stats_.filterPixelPassCount += pixels;
}

void SoftwareRenderer::resetRenderState() {}

void SoftwareRenderer::clear()
{
    commands_.clear();
    imageBatchAppendFloor_ = 0;
}

void SoftwareRenderer::flush()
{
    const auto start = std::chrono::steady_clock::now();
    ensureFramebuffer();
    if (width_ <= 0 || height_ <= 0) {
        return;
    }
    clearFramebuffer();
    stats_.commandCount += commands_.size();
    stats_.drawCallCount += commands_.size();
    accumulatePathPipelineStats(commands_, stats_);
    ClipCache cache;
    executeCommandList(framebuffer_.data(), width_, height_, height_, glm::mat4(1.0f), commands_, &cache);
    const auto end = std::chrono::steady_clock::now();
    const std::uint64_t elapsedNs = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            end - start).count());
    stats_.flushCpuTimeNs += elapsedNs;
    stats_.deviceExecutionCpuTimeNs += elapsedNs;
    stats_.compiledPacketCount += commands_.size();
    std::size_t commandStagingBytes = commands_.capacity()
        * sizeof(std::unique_ptr<Command>);
    for (const auto &command : commands_) {
        if (command != nullptr
            && command->type() == Command::Type::ImageBatch) {
            commandStagingBytes +=
                static_cast<const DrawImageBatchCommand *>(
                    command.get())->data().quads.capacity()
                * sizeof(DrawImageBatchQuad);
        }
    }
    stats_.stagingCapacityBytes = std::max(
        stats_.stagingCapacityBytes,
        commandStagingBytes);
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

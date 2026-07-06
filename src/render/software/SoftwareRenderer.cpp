#include "SoftwareRenderer.h"

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

#include "command/DrawCommand.h"

namespace wsc::software {
namespace {

/// CPU-side image resource holding a straight-alpha RGBA8 buffer. Sampling is
/// added in a later milestone; for now it just stores the pixels so image and
/// bitmap-text resource creation succeeds.
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

private:
    int width_ = 0;
    int height_ = 0;
    std::vector<std::uint8_t> pixels_;
};

class SoftwareClipMaskResource final : public ClipMaskResource
{
public:
    bool isValid() const override { return true; }
    void apply(const RenderContext &, const ScissorState &, std::size_t) const override {}
};

inline std::uint8_t toByte(float value)
{
    return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

/// Blend a straight-alpha source over the destination pixel, matching the GL
/// backend's glBlendFuncSeparate settings (see RenderContext::applyBlendMode)
/// for all 14 Porter-Duff / separable modes.
inline void blendPixel(std::uint8_t *dst, float sr, float sg, float sb, float sa, DrawBlendMode mode)
{
    const float dr = dst[0] / 255.0f;
    const float dg = dst[1] / 255.0f;
    const float db = dst[2] / 255.0f;
    const float da = dst[3] / 255.0f;

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
                        const glm::mat4 &transform, DrawBlendMode blendMode, const GradientDesc &grad)
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
        const Vertex v0 = makeVertex(t);
        const Vertex v1 = makeVertex(t + 1);
        const Vertex v2 = makeVertex(t + 2);

        const float area = edge(v0.x, v0.y, v1.x, v1.y, v2.x, v2.y);
        if (std::fabs(area) < 1e-7f) {
            continue;
        }
        const float invArea = 1.0f / area;

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
                const float b0 = edge(v1.x, v1.y, v2.x, v2.y, sx, sy) * invArea;
                const float b1 = edge(v2.x, v2.y, v0.x, v0.y, sx, sy) * invArea;
                const float b2 = edge(v0.x, v0.y, v1.x, v1.y, sx, sy) * invArea;
                if (b0 < 0.0f || b1 < 0.0f || b2 < 0.0f) {
                    continue;
                }

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
                const float srcA = a * cov;
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
void rasterizeLines(std::uint8_t *framebuffer, int width, int height, const DrawLinesData &data)
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
                       GradientDesc{});
}

/// Draw each point as a device-space square of side `size` (matching gl_PointSize).
void rasterizePoints(std::uint8_t *framebuffer, int width, int height, const DrawPointsData &data)
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
                       GradientDesc{});
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

SharedClipMaskResource SoftwareRenderer::createClipMaskResource(const ClipMaskPath &) const
{
    return std::make_shared<SoftwareClipMaskResource>();
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
            dst[0] = src[0];
            dst[1] = channels > 1 ? src[1] : src[0];
            dst[2] = channels > 2 ? src[2] : src[0];
            dst[3] = channels > 3 ? src[3] : 255;
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

SharedImageResource SoftwareRenderer::renderCommandsToImageResource(const std::vector<std::unique_ptr<Command>> &,
                                                                    const OffscreenRenderRequest &) const
{
    // Offscreen layers/shadows are a later milestone.
    return {};
}

void SoftwareRenderer::resetRenderState() {}

void SoftwareRenderer::clear()
{
    commands_.clear();
}

void SoftwareRenderer::executeCommand(const Command &command)
{
    switch (command.type()) {
    case Command::Type::Path: {
        const DrawPathData &data = static_cast<const DrawPathCommand &>(command).data();
        rasterizeTriangles(framebuffer_.data(), width_, height_, data.points, data.colors, data.coverage,
                           data.color, data.transform, data.blendMode, makeGradientDesc(data));
        break;
    }
    case Command::Type::Text: {
        const DrawTextData &data = static_cast<const DrawTextCommand &>(command).data();
        rasterizeTriangles(framebuffer_.data(), width_, height_, data.vertices, {}, {},
                           data.color, data.transform, data.blendMode, GradientDesc{});
        break;
    }
    case Command::Type::Points: {
        rasterizePoints(framebuffer_.data(), width_, height_,
                        static_cast<const DrawPointsCommand &>(command).data());
        break;
    }
    case Command::Type::Lines: {
        rasterizeLines(framebuffer_.data(), width_, height_,
                       static_cast<const DrawLinesCommand &>(command).data());
        break;
    }
    default:
        // Image and Shadow are handled in later milestones.
        break;
    }
}

void SoftwareRenderer::flush()
{
    ensureFramebuffer();
    if (width_ <= 0 || height_ <= 0) {
        return;
    }
    clearFramebuffer();
    stats_.commandCount += commands_.size();
    for (const auto &command : commands_) {
        if (command) {
            executeCommand(*command);
            ++stats_.drawCallCount;
        }
    }
}

} // namespace wsc::software

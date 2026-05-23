#include <algorithm>
#include <string>
#include <iostream>
#include <fstream>
#include <cmath>
#include <cstring>
#include <limits>
#include <glm.hpp>
#include <gtc/matrix_transform.hpp>
#include <memory>
#include <vector>

#include "Canvas.h"
#include "Paint.h"
#include "Path.h"
#include "command/DrawData.h"
#include "command/DrawCommand.h"
#include "Polyline2D.h"
#include "Vec2.h"
#include "stb_easy_font.h"

namespace {
constexpr float kPointEpsilon = 0.0001f;

struct ShadowPass {
    glm::mat4 transform = glm::mat4(1.0f);
    Color color;
};

struct PathContour {
    std::vector<crushedpixel::Vec2> points;
    bool closed = false;
};

PathDrawMode toPathDrawMode(Paint::Style style)
{
    switch (style) {
    case Paint::Style::FILL:
        return PathDrawMode::Fill;
    case Paint::Style::STROKE:
        return PathDrawMode::Stroke;
    case Paint::Style::FILL_AND_STROKE:
        return PathDrawMode::FillAndStroke;
    }

    return PathDrawMode::Fill;
}

crushedpixel::Polyline2D::JointStyle toPolylineJointStyle(Paint::StrokeJoin join)
{
    switch (join) {
    case Paint::StrokeJoin::MITER:
        return crushedpixel::Polyline2D::JointStyle::MITER;
    case Paint::StrokeJoin::ROUND:
        return crushedpixel::Polyline2D::JointStyle::ROUND;
    case Paint::StrokeJoin::BEVEL:
        return crushedpixel::Polyline2D::JointStyle::BEVEL;
    }

    return crushedpixel::Polyline2D::JointStyle::MITER;
}

crushedpixel::Polyline2D::EndCapStyle toPolylineEndCapStyle(Paint::StrokeCap cap)
{
    switch (cap) {
    case Paint::StrokeCap::BUTT:
        return crushedpixel::Polyline2D::EndCapStyle::BUTT;
    case Paint::StrokeCap::ROUND:
        return crushedpixel::Polyline2D::EndCapStyle::ROUND;
    case Paint::StrokeCap::SQUARE:
        return crushedpixel::Polyline2D::EndCapStyle::SQUARE;
    }

    return crushedpixel::Polyline2D::EndCapStyle::BUTT;
}

DrawBlendMode toDrawBlendMode(Paint::BlendMode blendMode)
{
    switch (blendMode) {
    case Paint::BlendMode::SRC_OVER:
        return DrawBlendMode::SrcOver;
    case Paint::BlendMode::ADD:
        return DrawBlendMode::Add;
    case Paint::BlendMode::MULTIPLY:
        return DrawBlendMode::Multiply;
    case Paint::BlendMode::SCREEN:
        return DrawBlendMode::Screen;
    }

    return DrawBlendMode::SrcOver;
}

DrawImageSampling toDrawImageSampling(Paint::ImageSampling sampling)
{
    switch (sampling) {
    case Paint::ImageSampling::LINEAR:
        return DrawImageSampling::Linear;
    case Paint::ImageSampling::NEAREST:
        return DrawImageSampling::Nearest;
    case Paint::ImageSampling::MIPMAP_LINEAR:
        return DrawImageSampling::MipmapLinear;
    }

    return DrawImageSampling::Linear;
}

DrawImageTileMode toDrawImageTileMode(Paint::ImageTileMode tileMode)
{
    switch (tileMode) {
    case Paint::ImageTileMode::CLAMP:
        return DrawImageTileMode::Clamp;
    case Paint::ImageTileMode::REPEAT:
        return DrawImageTileMode::Repeat;
    case Paint::ImageTileMode::MIRROR:
        return DrawImageTileMode::Mirror;
    case Paint::ImageTileMode::DECAL:
        return DrawImageTileMode::Decal;
    }

    return DrawImageTileMode::Clamp;
}

float anchorXFactor(Canvas::ImageAnchor anchor)
{
    switch (anchor) {
    case Canvas::ImageAnchor::TOP_LEFT:
    case Canvas::ImageAnchor::LEFT:
    case Canvas::ImageAnchor::BOTTOM_LEFT:
        return 0.0f;
    case Canvas::ImageAnchor::TOP_RIGHT:
    case Canvas::ImageAnchor::RIGHT:
    case Canvas::ImageAnchor::BOTTOM_RIGHT:
        return 1.0f;
    case Canvas::ImageAnchor::TOP:
    case Canvas::ImageAnchor::CENTER:
    case Canvas::ImageAnchor::BOTTOM:
        return 0.5f;
    }

    return 0.5f;
}

float anchorYFactor(Canvas::ImageAnchor anchor)
{
    switch (anchor) {
    case Canvas::ImageAnchor::TOP_LEFT:
    case Canvas::ImageAnchor::TOP:
    case Canvas::ImageAnchor::TOP_RIGHT:
        return 0.0f;
    case Canvas::ImageAnchor::BOTTOM_LEFT:
    case Canvas::ImageAnchor::BOTTOM:
    case Canvas::ImageAnchor::BOTTOM_RIGHT:
        return 1.0f;
    case Canvas::ImageAnchor::LEFT:
    case Canvas::ImageAnchor::CENTER:
    case Canvas::ImageAnchor::RIGHT:
        return 0.5f;
    }

    return 0.5f;
}

void applyImageColorMatrix(const Paint &paint, DrawImageData &data)
{
    if (!paint.hasColorMatrix()) {
        return;
    }

    const auto &matrix = paint.getColorMatrix();
    data.hasColorMatrix = true;
    for (size_t row = 0; row < 4; ++row) {
        for (size_t column = 0; column < 4; ++column) {
            data.colorMatrix[column * 4 + row] = matrix[row * 5 + column];
        }
        data.colorMatrixOffset[row] = matrix[row * 5 + 4];
    }
}

DrawPathData makeDrawPathData(const std::vector<float> &points, float width, const Color &color,
                              PathDrawMode mode, const glm::mat4 &transform, const ScissorState &scissor,
                              DrawBlendMode blendMode)
{
    return DrawPathData{
        points,
        {},
        width,
        {color.r(), color.g(), color.b(), color.a()},
        mode,
        PathCapStyle::Bevel,
        transform,
        scissor,
        blendMode
    };
}

bool isFinitePoint(float x, float y)
{
    return std::isfinite(x) && std::isfinite(y);
}

bool nearlySamePoint(const crushedpixel::Vec2 &a, const crushedpixel::Vec2 &b)
{
    return std::abs(a.x - b.x) <= kPointEpsilon && std::abs(a.y - b.y) <= kPointEpsilon;
}

RectF normalizeRect(const RectF &rect)
{
    float x = rect.getX();
    float y = rect.getY();
    float width = rect.getWidth();
    float height = rect.getHeight();

    if (width < 0.0f) {
        x += width;
        width = -width;
    }

    if (height < 0.0f) {
        y += height;
        height = -height;
    }

    return RectF(x, y, width, height);
}

RectF intersectRects(const RectF &a, const RectF &b)
{
    const float left = std::max(a.getX(), b.getX());
    const float top = std::max(a.getY(), b.getY());
    const float right = std::min(a.getX() + a.getWidth(), b.getX() + b.getWidth());
    const float bottom = std::min(a.getY() + a.getHeight(), b.getY() + b.getHeight());
    if (right <= left || bottom <= top) {
        return RectF(left, top, 0.0f, 0.0f);
    }

    return RectF(left, top, right - left, bottom - top);
}

bool transformRectBounds(const RectF &rect, const glm::mat4 &matrix, RectF &bounds)
{
    const RectF normalized = normalizeRect(rect);
    if (normalized.getWidth() <= 0.0f || normalized.getHeight() <= 0.0f) {
        bounds = RectF(normalized.getX(), normalized.getY(), 0.0f, 0.0f);
        return false;
    }

    const glm::vec4 corners[4] = {
        glm::vec4(normalized.getX(), normalized.getY(), 0.0f, 1.0f),
        glm::vec4(normalized.getX() + normalized.getWidth(), normalized.getY(), 0.0f, 1.0f),
        glm::vec4(normalized.getX() + normalized.getWidth(), normalized.getY() + normalized.getHeight(), 0.0f, 1.0f),
        glm::vec4(normalized.getX(), normalized.getY() + normalized.getHeight(), 0.0f, 1.0f)
    };

    float left = std::numeric_limits<float>::max();
    float top = std::numeric_limits<float>::max();
    float right = std::numeric_limits<float>::lowest();
    float bottom = std::numeric_limits<float>::lowest();

    for (const auto &corner : corners) {
        glm::vec4 transformed = matrix * corner;
        if (std::abs(transformed.w) > kPointEpsilon) {
            transformed /= transformed.w;
        }

        if (!std::isfinite(transformed.x) || !std::isfinite(transformed.y)) {
            return false;
        }

        left = std::min(left, transformed.x);
        top = std::min(top, transformed.y);
        right = std::max(right, transformed.x);
        bottom = std::max(bottom, transformed.y);
    }

    bounds = RectF(left, top, right - left, bottom - top);
    return true;
}

bool transformPoint(const glm::mat4 &matrix, const PointF &point, PointF &mappedPoint)
{
    glm::vec4 transformed = matrix * glm::vec4(point.getX(), point.getY(), 0.0f, 1.0f);
    if (std::abs(transformed.w) <= kPointEpsilon) {
        return false;
    }

    transformed /= transformed.w;
    if (!std::isfinite(transformed.x) || !std::isfinite(transformed.y)) {
        return false;
    }

    mappedPoint = PointF(transformed.x, transformed.y);
    return true;
}

RectF clampSourceRect(const RectF &src, int imageWidth, int imageHeight)
{
    RectF normalized = normalizeRect(src);
    RectF imageBounds(0.0f, 0.0f, static_cast<float>(imageWidth), static_cast<float>(imageHeight));
    return intersectRects(normalized, imageBounds);
}

int computeEllipseSegments(float radiusX, float radiusY)
{
    if (radiusX <= 0.0f || radiusY <= 0.0f) {
        return 0;
    }

    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kSegmentsPerPixel = 0.25f;
    constexpr int kMinSegments = 16;
    constexpr int kMaxSegments = 256;

    const float a = std::max(radiusX, radiusY);
    const float b = std::min(radiusX, radiusY);
    const float circumference = kPi * (3.0f * (a + b) - std::sqrt((3.0f * a + b) * (a + 3.0f * b)));
    const int estimated = static_cast<int>(std::ceil(circumference * kSegmentsPerPixel));
    return std::clamp(estimated, kMinSegments, kMaxSegments);
}

void addEllipse(Path &path, float centerX, float centerY, float radiusX, float radiusY, int segments)
{
    if (radiusX <= 0.0f || radiusY <= 0.0f || segments < 3) {
        return;
    }

    constexpr float kPi = 3.14159265358979323846f;
    const float angleStep = (2.0f * kPi) / static_cast<float>(segments);
    path.moveTo(centerX + radiusX, centerY);
    for (int i = 1; i <= segments; ++i) {
        const float angle = angleStep * static_cast<float>(i);
        path.lineTo(centerX + radiusX * std::cos(angle), centerY + radiusY * std::sin(angle));
    }
    path.close();
}

void addQuarterArc(Path &path, float centerX, float centerY, float radius,
                   float startAngle, float endAngle, int segments)
{
    if (radius <= 0.0f || segments <= 0) {
        return;
    }

    const float angleStep = (endAngle - startAngle) / static_cast<float>(segments);
    for (int i = 1; i <= segments; ++i) {
        const float angle = startAngle + angleStep * static_cast<float>(i);
        path.lineTo(centerX + radius * std::cos(angle), centerY + radius * std::sin(angle));
    }
}

void addArc(Path &path, const RectF &bounds, float startRadians, float sweepRadians, bool useCenter, bool closeArc)
{
    const float width = bounds.getWidth();
    const float height = bounds.getHeight();
    if (width <= 0.0f || height <= 0.0f || std::abs(sweepRadians) <= kPointEpsilon) {
        return;
    }

    constexpr float kPi = 3.14159265358979323846f;
    const float maxSweep = 2.0f * kPi;
    const float clampedSweep = std::clamp(sweepRadians, -maxSweep, maxSweep);
    const float centerX = bounds.getX() + width * 0.5f;
    const float centerY = bounds.getY() + height * 0.5f;
    const float radiusX = width * 0.5f;
    const float radiusY = height * 0.5f;
    const float radius = std::max(radiusX, radiusY);
    const int segments = std::clamp(static_cast<int>(std::ceil(std::abs(clampedSweep) * radius * 0.25f)), 2, 128);

    auto pointAt = [&](float angle) {
        return PointF(centerX + std::cos(angle) * radiusX, centerY + std::sin(angle) * radiusY);
    };

    const PointF startPoint = pointAt(startRadians);
    if (useCenter) {
        path.moveTo(centerX, centerY);
        path.lineTo(startPoint.getX(), startPoint.getY());
    } else {
        path.moveTo(startPoint.getX(), startPoint.getY());
    }

    for (int segment = 1; segment <= segments; ++segment) {
        const float progress = static_cast<float>(segment) / static_cast<float>(segments);
        const PointF point = pointAt(startRadians + clampedSweep * progress);
        path.lineTo(point.getX(), point.getY());
    }

    if (closeArc) {
        path.close();
    }
}

void executeCommandList(const std::vector<std::unique_ptr<Command>> &commands, int width, int height,
                        int scissorOffsetX = 0, int scissorOffsetY = 0)
{
    RenderContext context;
    context.setSize(width, height);
    context.setScissorOffset(scissorOffsetX, scissorOffsetY);
    for (const auto &command : commands) {
        command->execute(context);
    }
}

bool createLayerTarget(int width, int height, unsigned int &framebuffer, unsigned int &texture)
{
    framebuffer = 0;
    texture = 0;
    if (width <= 0 || height <= 0) {
        return false;
    }

    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);

    const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (!complete) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &framebuffer);
        glDeleteTextures(1, &texture);
        framebuffer = 0;
        texture = 0;
        return false;
    }

    return true;
}

ScissorState makeScissorForRect(const RectF &rect, int canvasWidth, int canvasHeight)
{
    ScissorState scissor;
    const RectF bounds(0.0f, 0.0f, static_cast<float>(canvasWidth), static_cast<float>(canvasHeight));
    const RectF clipped = intersectRects(normalizeRect(rect), bounds);
    if (clipped.getWidth() <= 0.0f || clipped.getHeight() <= 0.0f) {
        return scissor;
    }

    const int left = std::clamp(static_cast<int>(std::floor(clipped.getX())), 0, canvasWidth);
    const int top = std::clamp(static_cast<int>(std::floor(clipped.getY())), 0, canvasHeight);
    const int right = std::clamp(static_cast<int>(std::ceil(clipped.getX() + clipped.getWidth())), 0, canvasWidth);
    const int bottom = std::clamp(static_cast<int>(std::ceil(clipped.getY() + clipped.getHeight())), 0, canvasHeight);

    scissor.enabled = true;
    scissor.x = left;
    scissor.y = canvasHeight - bottom;
    scissor.width = std::max(0, right - left);
    scissor.height = std::max(0, bottom - top);
    return scissor;
}

void appendSafePoint(std::vector<crushedpixel::Vec2> &points, float x, float y)
{
    if (!isFinitePoint(x, y)) {
        return;
    }

    crushedpixel::Vec2 point(x, y);
    if (!points.empty() && nearlySamePoint(points.back(), point)) {
        return;
    }

    points.push_back(point);
}

std::vector<float> flattenPoints(const std::vector<crushedpixel::Vec2> &points)
{
    std::vector<float> flattened;
    flattened.reserve(points.size() * 2);
    for (const auto &point : points) {
        flattened.push_back(point.x);
        flattened.push_back(point.y);
    }
    return flattened;
}

float lerp(float start, float end, float progress)
{
    return start + (end - start) * progress;
}

Color scaleAlpha(const Color &color, float factor)
{
    const int alpha = std::clamp(static_cast<int>(std::round(static_cast<float>(color.getA()) * factor)), 0, 255);
    return Color(color.getR(), color.getG(), color.getB(), alpha);
}

Color applyPaintAlpha(const Paint &paint, const Color &color)
{
    return scaleAlpha(color, paint.getAlphaF());
}

std::vector<float> buildLinearGradientColors(const std::vector<crushedpixel::Vec2> &vertices, const Paint &paint)
{
    std::vector<float> colors;
    if (!paint.hasLinearGradient()) {
        return colors;
    }

    colors.reserve(vertices.size() * 4);
    const float startX = paint.getGradientStartX();
    const float startY = paint.getGradientStartY();
    const float dx = paint.getGradientEndX() - startX;
    const float dy = paint.getGradientEndY() - startY;
    const float lengthSq = dx * dx + dy * dy;

    const Color startColor = paint.getGradientStartColor();
    const Color endColor = paint.getGradientEndColor();
    const float alphaScale = paint.getAlphaF();

    for (const auto &vertex : vertices) {
        float progress = 0.0f;
        if (lengthSq > kPointEpsilon) {
            progress = ((vertex.x - startX) * dx + (vertex.y - startY) * dy) / lengthSq;
            progress = std::clamp(progress, 0.0f, 1.0f);
        }

        colors.push_back(lerp(startColor.r(), endColor.r(), progress));
        colors.push_back(lerp(startColor.g(), endColor.g(), progress));
        colors.push_back(lerp(startColor.b(), endColor.b(), progress));
        colors.push_back(lerp(startColor.a(), endColor.a(), progress) * alphaScale);
    }

    return colors;
}

std::vector<float> buildRadialGradientColors(const std::vector<crushedpixel::Vec2> &vertices, const Paint &paint)
{
    std::vector<float> colors;
    if (!paint.hasRadialGradient()) {
        return colors;
    }

    colors.reserve(vertices.size() * 4);
    const float centerX = paint.getRadialCenterX();
    const float centerY = paint.getRadialCenterY();
    const float radius = paint.getRadialRadius();
    const Color startColor = paint.getRadialStartColor();
    const Color endColor = paint.getRadialEndColor();
    const float alphaScale = paint.getAlphaF();

    for (const auto &vertex : vertices) {
        float progress = 0.0f;
        if (radius > kPointEpsilon) {
            const float dx = vertex.x - centerX;
            const float dy = vertex.y - centerY;
            progress = std::sqrt(dx * dx + dy * dy) / radius;
            progress = std::clamp(progress, 0.0f, 1.0f);
        }

        colors.push_back(lerp(startColor.r(), endColor.r(), progress));
        colors.push_back(lerp(startColor.g(), endColor.g(), progress));
        colors.push_back(lerp(startColor.b(), endColor.b(), progress));
        colors.push_back(lerp(startColor.a(), endColor.a(), progress) * alphaScale);
    }

    return colors;
}

std::vector<float> buildFillVertexColors(const std::vector<crushedpixel::Vec2> &vertices, const Paint &paint)
{
    if (paint.hasLinearGradient()) {
        return buildLinearGradientColors(vertices, paint);
    }

    if (paint.hasRadialGradient()) {
        return buildRadialGradientColors(vertices, paint);
    }

    return {};
}

glm::mat4 makeOffsetTransform(const glm::mat4 &transform, float dx, float dy)
{
    return glm::translate(transform, glm::vec3(dx, dy, 0.0f));
}

std::vector<ShadowPass> buildShadowPasses(const Paint &paint, const glm::mat4 &transform)
{
    std::vector<ShadowPass> passes;
    if (!paint.hasShadowLayer()) {
        return passes;
    }

    const float dx = paint.getShadowDx();
    const float dy = paint.getShadowDy();
    const float radius = std::max(0.0f, paint.getShadowRadius());
    const Color color = applyPaintAlpha(paint, paint.getShadowColor());

    if (radius <= kPointEpsilon) {
        passes.push_back({makeOffsetTransform(transform, dx, dy), color});
        return passes;
    }

    passes.push_back({makeOffsetTransform(transform, dx, dy), scaleAlpha(color, 0.35f)});

    constexpr float kPi = 3.14159265358979323846f;
    constexpr int kSamples = 8;
    const float ringRadius = radius * 0.45f;
    for (int i = 0; i < kSamples; ++i) {
        const float angle = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(kSamples);
        const float sampleDx = dx + std::cos(angle) * ringRadius;
        const float sampleDy = dy + std::sin(angle) * ringRadius;
        passes.push_back({makeOffsetTransform(transform, sampleDx, sampleDy), scaleAlpha(color, 0.08f)});
    }

    return passes;
}

float cross(const crushedpixel::Vec2 &a, const crushedpixel::Vec2 &b, const crushedpixel::Vec2 &c)
{
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

float signedArea(const std::vector<crushedpixel::Vec2> &points)
{
    float area = 0.0f;
    for (size_t i = 0; i < points.size(); ++i) {
        const auto &current = points[i];
        const auto &next = points[(i + 1) % points.size()];
        area += current.x * next.y - next.x * current.y;
    }
    return area * 0.5f;
}

bool pointInPolygon(const crushedpixel::Vec2 &point, const std::vector<crushedpixel::Vec2> &polygon)
{
    bool inside = false;
    if (polygon.size() < 3) {
        return false;
    }

    for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        const auto &a = polygon[i];
        const auto &b = polygon[j];
        const bool crosses = ((a.y > point.y) != (b.y > point.y))
            && (point.x < (b.x - a.x) * (point.y - a.y) / ((b.y - a.y) + kPointEpsilon) + a.x);
        if (crosses) {
            inside = !inside;
        }
    }

    return inside;
}

bool pointInTriangle(const crushedpixel::Vec2 &point, const crushedpixel::Vec2 &a,
                     const crushedpixel::Vec2 &b, const crushedpixel::Vec2 &c)
{
    const float ab = cross(a, b, point);
    const float bc = cross(b, c, point);
    const float ca = cross(c, a, point);
    return ab >= -kPointEpsilon && bc >= -kPointEpsilon && ca >= -kPointEpsilon;
}

std::vector<crushedpixel::Vec2> buildTriangleFan(const std::vector<crushedpixel::Vec2> &polygon)
{
    std::vector<crushedpixel::Vec2> triangles;
    if (polygon.size() < 3) {
        return triangles;
    }

    triangles.reserve((polygon.size() - 2) * 3);
    for (size_t i = 1; i + 1 < polygon.size(); ++i) {
        triangles.push_back(polygon.front());
        triangles.push_back(polygon[i]);
        triangles.push_back(polygon[i + 1]);
    }
    return triangles;
}

std::vector<crushedpixel::Vec2> sanitizeFillPolygon(const std::vector<crushedpixel::Vec2> &points)
{
    std::vector<crushedpixel::Vec2> polygon;
    polygon.reserve(points.size());
    for (const auto &point : points) {
        appendSafePoint(polygon, point.x, point.y);
    }

    if (polygon.size() > 1 && nearlySamePoint(polygon.front(), polygon.back())) {
        polygon.pop_back();
    }

    bool removed = true;
    while (removed && polygon.size() >= 3) {
        removed = false;
        for (size_t i = 0; i < polygon.size(); ++i) {
            const auto &previous = polygon[(i + polygon.size() - 1) % polygon.size()];
            const auto &current = polygon[i];
            const auto &next = polygon[(i + 1) % polygon.size()];
            if (std::abs(cross(previous, current, next)) <= kPointEpsilon) {
                polygon.erase(polygon.begin() + static_cast<std::ptrdiff_t>(i));
                removed = true;
                break;
            }
        }
    }

    return polygon;
}

std::vector<crushedpixel::Vec2> normalizedContourForFill(const std::vector<crushedpixel::Vec2> &points, bool ccw)
{
    std::vector<crushedpixel::Vec2> polygon = sanitizeFillPolygon(points);
    if (polygon.size() < 3) {
        return polygon;
    }

    const bool isCcw = signedArea(polygon) > 0.0f;
    if (isCcw != ccw) {
        std::reverse(polygon.begin(), polygon.end());
    }

    return polygon;
}

size_t findRightmostVertex(const std::vector<crushedpixel::Vec2> &points)
{
    size_t best = 0;
    for (size_t i = 1; i < points.size(); ++i) {
        if (points[i].x > points[best].x ||
            (std::abs(points[i].x - points[best].x) <= kPointEpsilon && points[i].y < points[best].y)) {
            best = i;
        }
    }
    return best;
}

size_t findNearestVertex(const std::vector<crushedpixel::Vec2> &points, const crushedpixel::Vec2 &target)
{
    size_t best = 0;
    float bestDistance = std::numeric_limits<float>::max();
    for (size_t i = 0; i < points.size(); ++i) {
        const float dx = points[i].x - target.x;
        const float dy = points[i].y - target.y;
        const float distance = dx * dx + dy * dy;
        if (distance < bestDistance) {
            bestDistance = distance;
            best = i;
        }
    }
    return best;
}

std::vector<crushedpixel::Vec2> bridgeHoleIntoOuter(const std::vector<crushedpixel::Vec2> &outer,
                                                    const std::vector<crushedpixel::Vec2> &hole)
{
    if (outer.size() < 3 || hole.size() < 3) {
        return outer;
    }

    const size_t holeIndex = findRightmostVertex(hole);
    const size_t outerIndex = findNearestVertex(outer, hole[holeIndex]);

    std::vector<crushedpixel::Vec2> combined;
    combined.reserve(outer.size() + hole.size() + 2);
    for (size_t i = 0; i <= outerIndex; ++i) {
        combined.push_back(outer[i]);
    }

    for (size_t step = 0; step < hole.size(); ++step) {
        combined.push_back(hole[(holeIndex + step) % hole.size()]);
    }
    combined.push_back(hole[holeIndex]);
    combined.push_back(outer[outerIndex]);

    for (size_t i = outerIndex + 1; i < outer.size(); ++i) {
        combined.push_back(outer[i]);
    }

    return combined;
}

std::vector<crushedpixel::Vec2> triangulateSimplePolygon(const std::vector<crushedpixel::Vec2> &points)
{
    std::vector<crushedpixel::Vec2> polygon = sanitizeFillPolygon(points);
    if (polygon.size() < 3) {
        return {};
    }

    const float area = signedArea(polygon);
    if (std::abs(area) <= kPointEpsilon) {
        return {};
    }

    if (area < 0.0f) {
        std::reverse(polygon.begin(), polygon.end());
    }

    std::vector<size_t> indices;
    indices.reserve(polygon.size());
    for (size_t i = 0; i < polygon.size(); ++i) {
        indices.push_back(i);
    }

    std::vector<crushedpixel::Vec2> triangles;
    triangles.reserve((polygon.size() - 2) * 3);

    size_t guard = 0;
    const size_t maxIterations = polygon.size() * polygon.size();
    while (indices.size() > 3 && guard++ < maxIterations) {
        bool earFound = false;
        for (size_t i = 0; i < indices.size(); ++i) {
            const size_t previousIndex = indices[(i + indices.size() - 1) % indices.size()];
            const size_t currentIndex = indices[i];
            const size_t nextIndex = indices[(i + 1) % indices.size()];

            const auto &previous = polygon[previousIndex];
            const auto &current = polygon[currentIndex];
            const auto &next = polygon[nextIndex];
            if (cross(previous, current, next) <= kPointEpsilon) {
                continue;
            }

            bool containsPoint = false;
            for (size_t candidateIndex : indices) {
                if (candidateIndex == previousIndex || candidateIndex == currentIndex || candidateIndex == nextIndex) {
                    continue;
                }

                if (pointInTriangle(polygon[candidateIndex], previous, current, next)) {
                    containsPoint = true;
                    break;
                }
            }

            if (containsPoint) {
                continue;
            }

            triangles.push_back(previous);
            triangles.push_back(current);
            triangles.push_back(next);
            indices.erase(indices.begin() + static_cast<std::ptrdiff_t>(i));
            earFound = true;
            break;
        }

        if (!earFound) {
            return buildTriangleFan(polygon);
        }
    }

    if (indices.size() == 3) {
        triangles.push_back(polygon[indices[0]]);
        triangles.push_back(polygon[indices[1]]);
        triangles.push_back(polygon[indices[2]]);
    }

    return triangles;
}

std::vector<PathContour> extractContours(const Path &path)
{
    std::vector<PathContour> contours;
    PathContour current;

    auto flushCurrent = [&]() {
        if (!current.points.empty()) {
            contours.push_back(current);
            current = PathContour{};
        }
    };

    for (const auto &pathPoint : path.getPoints()) {
        if (pathPoint.op == Path::Op::MOVE_TO) {
            flushCurrent();
            appendSafePoint(current.points, pathPoint.point.getX(), pathPoint.point.getY());
        } else if (pathPoint.op == Path::Op::LINE_TO) {
            appendSafePoint(current.points, pathPoint.point.getX(), pathPoint.point.getY());
        } else if (pathPoint.op == Path::Op::CLOSE) {
            current.closed = true;
            flushCurrent();
        }
    }

    flushCurrent();
    return contours;
}

std::vector<crushedpixel::Vec2> triangulateContours(const std::vector<PathContour> &contours, Path::FillType fillType)
{
    std::vector<std::vector<crushedpixel::Vec2>> closedContours;
    closedContours.reserve(contours.size());
    for (const auto &contour : contours) {
        if (!contour.closed || contour.points.size() < 3) {
            continue;
        }

        auto polygon = sanitizeFillPolygon(contour.points);
        if (polygon.size() >= 3 && std::abs(signedArea(polygon)) > kPointEpsilon) {
            closedContours.push_back(std::move(polygon));
        }
    }

    std::vector<crushedpixel::Vec2> triangles;
    if (closedContours.empty()) {
        return triangles;
    }

    if (fillType == Path::FillType::WINDING || closedContours.size() == 1) {
        for (const auto &contour : closedContours) {
            auto contourTriangles = triangulateSimplePolygon(contour);
            triangles.insert(triangles.end(), contourTriangles.begin(), contourTriangles.end());
        }
        return triangles;
    }

    std::vector<bool> consumed(closedContours.size(), false);
    for (size_t i = 0; i < closedContours.size(); ++i) {
        if (consumed[i]) {
            continue;
        }

        int depth = 0;
        for (size_t j = 0; j < closedContours.size(); ++j) {
            if (i == j) {
                continue;
            }
            if (pointInPolygon(closedContours[i].front(), closedContours[j])) {
                ++depth;
            }
        }

        if (depth % 2 != 0) {
            continue;
        }

        auto bridged = normalizedContourForFill(closedContours[i], true);
        for (size_t j = 0; j < closedContours.size(); ++j) {
            if (i == j || consumed[j]) {
                continue;
            }

            int holeDepth = 0;
            for (size_t k = 0; k < closedContours.size(); ++k) {
                if (j == k) {
                    continue;
                }
                if (pointInPolygon(closedContours[j].front(), closedContours[k])) {
                    ++holeDepth;
                }
            }

            if (holeDepth == depth + 1 && pointInPolygon(closedContours[j].front(), closedContours[i])) {
                auto hole = normalizedContourForFill(closedContours[j], false);
                bridged = bridgeHoleIntoOuter(bridged, hole);
                consumed[j] = true;
            }
        }

        auto contourTriangles = triangulateSimplePolygon(bridged);
        triangles.insert(triangles.end(), contourTriangles.begin(), contourTriangles.end());
        consumed[i] = true;
    }

    return triangles;
}

std::vector<crushedpixel::Vec2> buildStrokeMesh(const std::vector<crushedpixel::Vec2> &points,
                                                bool closed, const Paint &paint)
{
    if (paint.getStrokeWidth() <= 0.0f || points.size() < 2) {
        return {};
    }

    std::vector<crushedpixel::Vec2> safePoints;
    safePoints.reserve(points.size());
    for (const auto &point : points) {
        appendSafePoint(safePoints, point.x, point.y);
    }

    if (closed && safePoints.size() > 1 && nearlySamePoint(safePoints.front(), safePoints.back())) {
        safePoints.pop_back();
    }

    if (safePoints.size() < (closed ? 3u : 2u)) {
        return {};
    }

    auto capStyle = closed
        ? crushedpixel::Polyline2D::EndCapStyle::JOINT
        : toPolylineEndCapStyle(paint.getStrokeCap());

    return crushedpixel::Polyline2D::create<crushedpixel::Vec2>(
        safePoints,
        paint.getStrokeWidth(),
        toPolylineJointStyle(paint.getStrokeJoin()),
        capStyle,
        false);
}

std::vector<crushedpixel::Vec2> sanitizePolylinePoints(const std::vector<Point> &points)
{
    std::vector<crushedpixel::Vec2> safePoints;
    safePoints.reserve(points.size());
    for (const auto &point : points) {
        appendSafePoint(safePoints, static_cast<float>(point.getX()), static_cast<float>(point.getY()));
    }
    return safePoints;
}

std::vector<crushedpixel::Vec2> sanitizePolylinePoints(const std::vector<PointF> &points)
{
    std::vector<crushedpixel::Vec2> safePoints;
    safePoints.reserve(points.size());
    for (const auto &point : points) {
        appendSafePoint(safePoints, point.getX(), point.getY());
    }
    return safePoints;
}

Path makePathFromPolyline(const std::vector<crushedpixel::Vec2> &points, bool closed)
{
    Path path;
    if (points.empty()) {
        return path;
    }

    path.moveTo(points.front().x, points.front().y);
    for (size_t i = 1; i < points.size(); ++i) {
        path.lineTo(points[i].x, points[i].y);
    }

    if (closed) {
        path.close();
    }

    return path;
}

std::vector<std::vector<crushedpixel::Vec2>> buildDashedPolylines(const std::vector<crushedpixel::Vec2> &points,
                                                                  bool closed,
                                                                  const std::vector<float> &intervals,
                                                                  float phase)
{
    std::vector<std::vector<crushedpixel::Vec2>> dashes;
    if (points.size() < 2 || intervals.empty()) {
        return dashes;
    }

    float patternLength = 0.0f;
    for (float interval : intervals) {
        patternLength += interval;
    }
    if (patternLength <= kPointEpsilon) {
        return dashes;
    }

    phase = std::fmod(phase, patternLength);
    if (phase < 0.0f) {
        phase += patternLength;
    }
    size_t intervalIndex = 0;
    while (phase >= intervals[intervalIndex]) {
        phase -= intervals[intervalIndex];
        intervalIndex = (intervalIndex + 1) % intervals.size();
    }

    bool drawing = (intervalIndex % 2) == 0;
    float remaining = intervals[intervalIndex] - phase;
    std::vector<crushedpixel::Vec2> currentDash;

    auto finishDash = [&]() {
        if (currentDash.size() >= 2) {
            dashes.push_back(currentDash);
        }
        currentDash.clear();
    };

    const size_t segmentCount = closed ? points.size() : points.size() - 1;
    for (size_t segment = 0; segment < segmentCount; ++segment) {
        const auto start = points[segment];
        const auto end = points[(segment + 1) % points.size()];
        const float dx = end.x - start.x;
        const float dy = end.y - start.y;
        const float length = std::hypot(dx, dy);
        if (length <= kPointEpsilon) {
            continue;
        }

        float consumed = 0.0f;
        crushedpixel::Vec2 current = start;
        while (consumed < length - kPointEpsilon) {
            const float step = std::min(remaining, length - consumed);
            const float nextDistance = consumed + step;
            const float t = nextDistance / length;
            crushedpixel::Vec2 next(start.x + dx * t, start.y + dy * t);

            if (drawing) {
                if (currentDash.empty()) {
                    currentDash.push_back(current);
                }
                currentDash.push_back(next);
            }

            consumed = nextDistance;
            current = next;
            remaining -= step;

            if (remaining <= kPointEpsilon) {
                if (drawing) {
                    finishDash();
                }
                intervalIndex = (intervalIndex + 1) % intervals.size();
                drawing = (intervalIndex % 2) == 0;
                remaining = intervals[intervalIndex];
            }
        }
    }

    finishDash();
    if (closed && dashes.size() >= 2
        && !dashes.front().empty() && !dashes.back().empty()
        && nearlySamePoint(dashes.front().front(), dashes.back().back())) {
        std::vector<crushedpixel::Vec2> merged = dashes.back();
        merged.insert(merged.end(), dashes.front().begin() + 1, dashes.front().end());
        dashes.front() = std::move(merged);
        dashes.pop_back();
    }
    return dashes;
}

void submitStrokeMesh(Renderer &renderer, const std::vector<crushedpixel::Vec2> &points,
                      bool closed, const Paint &paint, const glm::mat4 &transform,
                      const ScissorState &scissor)
{
    if (paint.hasDashPathEffect()) {
        Paint dashPaint = paint;
        dashPaint.clearDashPathEffect();
        const auto dashes = buildDashedPolylines(points, closed, paint.getDashIntervals(), paint.getDashPhase());
        for (const auto &dash : dashes) {
            submitStrokeMesh(renderer, dash, false, dashPaint, transform, scissor);
        }
        return;
    }

    auto strokeMesh = buildStrokeMesh(points, closed, paint);
    if (strokeMesh.empty()) {
        return;
    }

    DrawPathData strokeData = makeDrawPathData(flattenPoints(strokeMesh), paint.getStrokeWidth(),
                                               applyPaintAlpha(paint, paint.getStrokeColor()), PathDrawMode::Stroke,
                                               transform, scissor, toDrawBlendMode(paint.getBlendMode()));
    renderer.submit(std::make_unique<DrawPathCommand>(strokeData));
}

Color resolveTextColor(const Paint &paint)
{
    if (paint.getStyle() == Paint::Style::STROKE) {
        return applyPaintAlpha(paint, paint.getStrokeColor());
    }
    return applyPaintAlpha(paint, paint.getColor());
}

std::string sanitizeTextToAscii(const std::string &text)
{
    std::string sanitized;
    sanitized.reserve(text.size());

    size_t i = 0;
    while (i < text.size()) {
        const unsigned char ch = static_cast<unsigned char>(text[i]);
        if (ch < 0x80) {
            if (ch == '\n') {
                sanitized.push_back('\n');
            } else if (ch == '\t') {
                sanitized.append("    ");
            } else if (ch >= 32 && ch <= 126) {
                sanitized.push_back(static_cast<char>(ch));
            } else {
                sanitized.push_back(' ');
            }
            ++i;
            continue;
        }

        size_t advance = 1;
        if ((ch & 0xE0) == 0xC0) {
            advance = 2;
        } else if ((ch & 0xF0) == 0xE0) {
            advance = 3;
        } else if ((ch & 0xF8) == 0xF0) {
            advance = 4;
        }

        if (i + advance > text.size()) {
            advance = 1;
        }

        sanitized.push_back('?');
        i += advance;
    }

    return sanitized;
}

float measureAsciiTextWidth(const std::string &asciiText, float scale, float letterSpacing)
{
    if (asciiText.empty()) {
        return 0.0f;
    }

    const float baseWidth = static_cast<float>(stb_easy_font_width(const_cast<char *>(asciiText.c_str()))) * scale;
    const float spacing = std::isfinite(letterSpacing) ? letterSpacing : 0.0f;
    return std::max(0.0f, baseWidth + spacing * static_cast<float>(asciiText.size() - 1));
}

float measureAsciiTextHeight(const std::string &asciiText, float scale)
{
    if (asciiText.empty()) {
        return 0.0f;
    }

    return static_cast<float>(stb_easy_font_height(const_cast<char *>(asciiText.c_str()))) * scale;
}

float textBaselineOffset(Paint::TextBaseline baseline, float textHeight)
{
    switch (baseline) {
    case Paint::TextBaseline::TOP:
        return 0.0f;
    case Paint::TextBaseline::MIDDLE:
        return -textHeight * 0.5f;
    case Paint::TextBaseline::BOTTOM:
        return -textHeight;
    }

    return 0.0f;
}

std::vector<float> buildTextVertices(const std::string &asciiText, float x, float y, float scale, float letterSpacing = 0.0f)
{
    if (asciiText.empty()) {
        return {};
    }

    if (std::abs(letterSpacing) > kPointEpsilon) {
        std::vector<float> vertices;
        float cursorX = x;
        for (char character : asciiText) {
            const std::string glyph(1, character);
            auto glyphVertices = buildTextVertices(glyph, cursorX, y, scale);
            vertices.insert(vertices.end(), glyphVertices.begin(), glyphVertices.end());
            cursorX += measureAsciiTextWidth(glyph, scale, 0.0f) + letterSpacing;
        }
        return vertices;
    }

    constexpr size_t kApproxBytesPerChar = 300;
    std::vector<char> buffer(std::max<size_t>(64, asciiText.size() * kApproxBytesPerChar));
    const int quadCount = stb_easy_font_print(0.0f, 0.0f, const_cast<char *>(asciiText.c_str()), nullptr,
                                              buffer.data(), static_cast<int>(buffer.size()));
    if (quadCount <= 0) {
        return {};
    }

    std::vector<float> vertices;
    vertices.reserve(static_cast<size_t>(quadCount) * 12);

    auto readFloat = [](const char *ptr) {
        float value = 0.0f;
        std::memcpy(&value, ptr, sizeof(float));
        return value;
    };

    for (int quadIndex = 0; quadIndex < quadCount; ++quadIndex) {
        float quadX[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        float quadY[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        const char *quadBase = buffer.data() + static_cast<size_t>(quadIndex) * 64;

        for (int v = 0; v < 4; ++v) {
            const char *vertexBase = quadBase + v * 16;
            quadX[v] = x + readFloat(vertexBase) * scale;
            quadY[v] = y + readFloat(vertexBase + 4) * scale;
        }

        const int triangleOrder[6] = {0, 1, 2, 0, 2, 3};
        for (int idx : triangleOrder) {
            vertices.push_back(quadX[idx]);
            vertices.push_back(quadY[idx]);
        }
    }

    return vertices;
}
}

void Canvas::initialize()
{
    Renderer::initialize();
}

void Canvas::finalize()
{
    Renderer::finalize();
}

void Canvas::setSize(int width, int height)
{
    width_ = width;
    height_ = height;
    renderer_.setViewport(width, height);
}

void Canvas::drawColor(const Color &color)
{
    Paint paint;
    paint.setStyle(Paint::Style::FILL);
    paint.setFillColor(color);
    drawPaint(paint);
}

void Canvas::drawPaint(const Paint &paint)
{
    if (width_ <= 0 || height_ <= 0) {
        return;
    }

    Paint fillPaint = paint;
    fillPaint.setStyle(Paint::Style::FILL);

    CanvasState savedState = currentState_;
    currentState_.matrix = glm::mat4(1.0f);
    drawRect(RectF(0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_)), fillPaint);
    currentState_ = savedState;
}

void Canvas::drawPoint(int x, int y, const Paint &paint)
{
    drawPoint(static_cast<float>(x), static_cast<float>(y), paint);
}

void Canvas::drawPoint(float x, float y, const Paint &paint)
{
    if (!isFinitePoint(x, y)) {
        return;
    }

    Color color = applyPaintAlpha(paint, paint.getStrokeColor());
    std::vector<float> pts = {x, y};
    DrawPointsData data = {
        pts,
        paint.getStrokeWidth(),
        {color.r(), color.g(), color.b(), color.a()},
        currentState_.matrix,
        makeCurrentScissorState()
    };
    data.blendMode = toDrawBlendMode(paint.getBlendMode());
    renderer_.submit(std::make_unique<DrawPointsCommand>(data));
}

void Canvas::drawPoint(const Point &point, const Paint &paint)
{
    drawPoint(point.getX(), point.getY(), paint);
}

void Canvas::drawPoint(const PointF &point, const Paint &paint)
{
    drawPoint(point.getX(), point.getY(), paint);
}

void Canvas::drawPoints(const std::vector<Point> &points, const Paint &paint) {
    Color color = applyPaintAlpha(paint, paint.getStrokeColor());
    std::vector<float> pts;
    pts.reserve(points.size() * 2);
    for (const auto &point : points) {
        pts.push_back(static_cast<float>(point.getX()));
        pts.push_back(static_cast<float>(point.getY()));
    }
    DrawPointsData data = {
        pts,
        paint.getStrokeWidth(),
        {color.r(), color.g(), color.b(), color.a()},
        currentState_.matrix,
        makeCurrentScissorState()
    };
    data.blendMode = toDrawBlendMode(paint.getBlendMode());
    renderer_.submit(std::make_unique<DrawPointsCommand>(data));
}

void Canvas::drawPoints(const std::vector<PointF> &points, const Paint &paint) {
    Color color = applyPaintAlpha(paint, paint.getStrokeColor());
    std::vector<float> pts;
    pts.reserve(points.size() * 2);
    for (const auto &point : points) {
        if (!isFinitePoint(point.getX(), point.getY())) {
            continue;
        }
        pts.push_back(point.getX());
        pts.push_back(point.getY());
    }

    if (pts.empty()) {
        return;
    }

    DrawPointsData data = {
        pts,
        paint.getStrokeWidth(),
        {color.r(), color.g(), color.b(), color.a()},
        currentState_.matrix,
        makeCurrentScissorState()
    };
    data.blendMode = toDrawBlendMode(paint.getBlendMode());
    renderer_.submit(std::make_unique<DrawPointsCommand>(data));
}

void Canvas::drawLine(int x1, int y1, int x2, int y2, const Paint &paint)
{
    drawLine(static_cast<float>(x1), static_cast<float>(y1), static_cast<float>(x2), static_cast<float>(y2), paint);
}

void Canvas::drawLine(float x1, float y1, float x2, float y2, const Paint &paint)
{
    std::vector<crushedpixel::Vec2> points;
    points.reserve(2);
    appendSafePoint(points, x1, y1);
    appendSafePoint(points, x2, y2);
    submitStrokeMesh(renderer_, points, false, paint, currentState_.matrix, makeCurrentScissorState());
}

void Canvas::drawLine(const Point &start, const Point &end, const Paint &paint)
{
    drawLine(start.getX(), start.getY(), end.getX(), end.getY(), paint);
}

void Canvas::drawLine(const PointF &start, const PointF &end, const Paint &paint)
{
    drawLine(start.getX(), start.getY(), end.getX(), end.getY(), paint);
}

void Canvas::drawLines(const std::vector<Point> &points, const Paint &paint)
{
    for (size_t i = 0; i + 1 < points.size(); i += 2) {
        std::vector<crushedpixel::Vec2> linePoints;
        linePoints.reserve(2);
        appendSafePoint(linePoints, static_cast<float>(points[i].getX()), static_cast<float>(points[i].getY()));
        appendSafePoint(linePoints, static_cast<float>(points[i + 1].getX()), static_cast<float>(points[i + 1].getY()));
        submitStrokeMesh(renderer_, linePoints, false, paint, currentState_.matrix, makeCurrentScissorState());
    }
}

void Canvas::drawLines(const std::vector<PointF> &points, const Paint &paint)
{
    for (size_t i = 0; i + 1 < points.size(); i += 2) {
        std::vector<crushedpixel::Vec2> linePoints;
        linePoints.reserve(2);
        appendSafePoint(linePoints, points[i].getX(), points[i].getY());
        appendSafePoint(linePoints, points[i + 1].getX(), points[i + 1].getY());
        submitStrokeMesh(renderer_, linePoints, false, paint, currentState_.matrix, makeCurrentScissorState());
    }
}

void Canvas::drawPolyline(const std::vector<Point> &points, const Paint &paint)
{
    auto safePoints = sanitizePolylinePoints(points);
    if (safePoints.size() < 2) {
        return;
    }

    Path path = makePathFromPolyline(safePoints, false);
    Paint strokePaint = paint;
    strokePaint.setStyle(Paint::Style::STROKE);
    drawPath(path, strokePaint);
}

void Canvas::drawPolyline(const std::vector<PointF> &points, const Paint &paint)
{
    auto safePoints = sanitizePolylinePoints(points);
    if (safePoints.size() < 2) {
        return;
    }

    Path path = makePathFromPolyline(safePoints, false);
    Paint strokePaint = paint;
    strokePaint.setStyle(Paint::Style::STROKE);
    drawPath(path, strokePaint);
}

void Canvas::drawPolygon(const std::vector<Point> &points, const Paint &paint)
{
    auto safePoints = sanitizePolylinePoints(points);
    if (safePoints.size() > 1 && nearlySamePoint(safePoints.front(), safePoints.back())) {
        safePoints.pop_back();
    }
    if (safePoints.size() < 3) {
        return;
    }

    Path path = makePathFromPolyline(safePoints, true);
    drawPath(path, paint);
}

void Canvas::drawPolygon(const std::vector<PointF> &points, const Paint &paint)
{
    auto safePoints = sanitizePolylinePoints(points);
    if (safePoints.size() > 1 && nearlySamePoint(safePoints.front(), safePoints.back())) {
        safePoints.pop_back();
    }
    if (safePoints.size() < 3) {
        return;
    }

    Path path = makePathFromPolyline(safePoints, true);
    drawPath(path, paint);
}

void Canvas::drawRect(const RectF &rect, const Paint &paint)
{
    RectF normalized = normalizeRect(rect);
    if (normalized.getWidth() <= 0.0f || normalized.getHeight() <= 0.0f) {
        return;
    }

    Path path;
    path.addRect(normalized);
    drawPath(path, paint);
}

void Canvas::drawRect(const Rect &rect, const Paint &paint)
{
    drawRect(RectF(static_cast<float>(rect.getX()), static_cast<float>(rect.getY()),
                   static_cast<float>(rect.getWidth()), static_cast<float>(rect.getHeight())),
             paint);
}

void Canvas::drawRoundRect(const RectF &rect, float radius, const Paint &paint)
{
    drawRoundRect(rect, radius, radius, radius, radius, paint);
}

void Canvas::drawRoundRect(const Rect &rect, float radius, const Paint &paint)
{
    drawRoundRect(RectF(static_cast<float>(rect.getX()), static_cast<float>(rect.getY()),
                        static_cast<float>(rect.getWidth()), static_cast<float>(rect.getHeight())),
                  radius, paint);
}

void Canvas::drawRoundRect(const RectF &rect, float topLeftRadius, float topRightRadius,
                           float bottomRightRadius, float bottomLeftRadius, const Paint &paint)
{
    RectF normalized = normalizeRect(rect);
    const float width = normalized.getWidth();
    const float height = normalized.getHeight();
    if (width <= 0.0f || height <= 0.0f) {
        return;
    }

    float topLeft = std::max(0.0f, topLeftRadius);
    float topRight = std::max(0.0f, topRightRadius);
    float bottomRight = std::max(0.0f, bottomRightRadius);
    float bottomLeft = std::max(0.0f, bottomLeftRadius);

    float scale = 1.0f;
    auto constrainPair = [&](float available, float a, float b) {
        const float sum = a + b;
        if (sum > available && sum > 0.0f) {
            scale = std::min(scale, available / sum);
        }
    };

    constrainPair(width, topLeft, topRight);
    constrainPair(width, bottomLeft, bottomRight);
    constrainPair(height, topRight, bottomRight);
    constrainPair(height, topLeft, bottomLeft);

    topLeft *= scale;
    topRight *= scale;
    bottomRight *= scale;
    bottomLeft *= scale;

    if (topLeft <= 0.0f && topRight <= 0.0f && bottomRight <= 0.0f && bottomLeft <= 0.0f) {
        drawRect(normalized, paint);
        return;
    }

    constexpr float kPi = 3.14159265358979323846f;
    auto cornerSegments = [](float radius) {
        return std::clamp(static_cast<int>(std::ceil(radius * 0.35f)), 4, 24);
    };

    const float left = normalized.getX();
    const float top = normalized.getY();
    const float right = left + width;
    const float bottom = top + height;

    Path path;
    path.moveTo(left + topLeft, top);
    path.lineTo(right - topRight, top);
    if (topRight > 0.0f) {
        addQuarterArc(path, right - topRight, top + topRight, topRight,
                      -0.5f * kPi, 0.0f, cornerSegments(topRight));
    } else {
        path.lineTo(right, top);
    }

    path.lineTo(right, bottom - bottomRight);
    if (bottomRight > 0.0f) {
        addQuarterArc(path, right - bottomRight, bottom - bottomRight, bottomRight,
                      0.0f, 0.5f * kPi, cornerSegments(bottomRight));
    } else {
        path.lineTo(right, bottom);
    }

    path.lineTo(left + bottomLeft, bottom);
    if (bottomLeft > 0.0f) {
        addQuarterArc(path, left + bottomLeft, bottom - bottomLeft, bottomLeft,
                      0.5f * kPi, kPi, cornerSegments(bottomLeft));
    } else {
        path.lineTo(left, bottom);
    }

    path.lineTo(left, top + topLeft);
    if (topLeft > 0.0f) {
        addQuarterArc(path, left + topLeft, top + topLeft, topLeft,
                      kPi, 1.5f * kPi, cornerSegments(topLeft));
    } else {
        path.lineTo(left, top);
    }
    path.close();

    drawPath(path, paint);
}

void Canvas::drawRoundRect(const Rect &rect, float topLeftRadius, float topRightRadius,
                           float bottomRightRadius, float bottomLeftRadius, const Paint &paint)
{
    drawRoundRect(RectF(static_cast<float>(rect.getX()), static_cast<float>(rect.getY()),
                        static_cast<float>(rect.getWidth()), static_cast<float>(rect.getHeight())),
                  topLeftRadius, topRightRadius, bottomRightRadius, bottomLeftRadius, paint);
}

void Canvas::drawCircle(float centerX, float centerY, float radius, const Paint &paint)
{
    if (radius <= 0.0f || !isFinitePoint(centerX, centerY)) {
        return;
    }

    const int segments = computeEllipseSegments(radius, radius);
    if (segments <= 0) {
        return;
    }

    Path path;
    addEllipse(path, centerX, centerY, radius, radius, segments);
    drawPath(path, paint);
}

void Canvas::drawCircle(const PointF &center, float radius, const Paint &paint)
{
    drawCircle(center.getX(), center.getY(), radius, paint);
}

void Canvas::drawCircle(const Point &center, float radius, const Paint &paint)
{
    drawCircle(static_cast<float>(center.getX()), static_cast<float>(center.getY()), radius, paint);
}

void Canvas::drawOval(const RectF &bounds, const Paint &paint)
{
    RectF normalized = normalizeRect(bounds);
    const float width = normalized.getWidth();
    const float height = normalized.getHeight();
    if (width <= 0.0f || height <= 0.0f) {
        return;
    }

    const float centerX = normalized.getX() + width * 0.5f;
    const float centerY = normalized.getY() + height * 0.5f;
    const float radiusX = width * 0.5f;
    const float radiusY = height * 0.5f;
    const int segments = computeEllipseSegments(radiusX, radiusY);
    if (segments <= 0) {
        return;
    }

    Path path;
    addEllipse(path, centerX, centerY, radiusX, radiusY, segments);
    drawPath(path, paint);
}

void Canvas::drawOval(const Rect &bounds, const Paint &paint)
{
    drawOval(RectF(static_cast<float>(bounds.getX()), static_cast<float>(bounds.getY()),
                   static_cast<float>(bounds.getWidth()), static_cast<float>(bounds.getHeight())),
             paint);
}

void Canvas::drawArc(const RectF &bounds, float startRadians, float sweepRadians, bool useCenter, const Paint &paint)
{
    RectF normalized = normalizeRect(bounds);
    if (normalized.getWidth() <= 0.0f || normalized.getHeight() <= 0.0f || std::abs(sweepRadians) <= kPointEpsilon) {
        return;
    }

    const bool closeArc = useCenter || paint.getStyle() != Paint::Style::STROKE;
    Path path;
    addArc(path, normalized, startRadians, sweepRadians, useCenter, closeArc);
    drawPath(path, paint);
}

void Canvas::drawArc(const Rect &bounds, float startRadians, float sweepRadians, bool useCenter, const Paint &paint)
{
    drawArc(RectF(static_cast<float>(bounds.getX()), static_cast<float>(bounds.getY()),
                  static_cast<float>(bounds.getWidth()), static_cast<float>(bounds.getHeight())),
            startRadians, sweepRadians, useCenter, paint);
}

void Canvas::drawPath(const Path &path, const Paint &paint)
{
    if (path.isEmpty()) {
        return;
    }

    Path effectedPath;
    const Path *sourcePath = &path;
    if (paint.hasCornerPathEffect()) {
        effectedPath = path.roundedCorners(paint.getCornerPathEffectRadius());
        sourcePath = &effectedPath;
    }

    const auto contours = extractContours(*sourcePath);
    if (contours.empty()) {
        return;
    }

    const bool drawFill = paint.getStyle() == Paint::Style::FILL || paint.getStyle() == Paint::Style::FILL_AND_STROKE;
    const bool drawStroke = paint.getStyle() == Paint::Style::STROKE || paint.getStyle() == Paint::Style::FILL_AND_STROKE;

    std::vector<crushedpixel::Vec2> fillTriangles;
    if (drawFill) {
        fillTriangles = triangulateContours(contours, sourcePath->getFillType());
    }

    if (paint.hasShadowLayer()) {
        const auto shadowPasses = buildShadowPasses(paint, currentState_.matrix);
        const ScissorState scissor = makeCurrentScissorState();
        for (const auto &shadowPass : shadowPasses) {
            if (drawFill && !fillTriangles.empty()) {
                DrawPathData shadowFillData = makeDrawPathData(flattenPoints(fillTriangles), paint.getStrokeWidth(),
                                                               shadowPass.color, PathDrawMode::Fill,
                                                               shadowPass.transform, scissor,
                                                               toDrawBlendMode(paint.getBlendMode()));
                renderer_.submit(std::make_unique<DrawPathCommand>(shadowFillData));
            }

            if (drawStroke) {
                Paint shadowPaint = paint;
                shadowPaint.clearShader();
                shadowPaint.clearShadowLayer();
                shadowPaint.setColor(shadowPass.color);
                shadowPaint.setAlpha(255);
                for (const auto &contour : contours) {
                    submitStrokeMesh(renderer_, contour.points, contour.closed, shadowPaint,
                                     shadowPass.transform, scissor);
                }
            }
        }
    }

    if (drawFill && !fillTriangles.empty()) {
        DrawPathData fillData = makeDrawPathData(flattenPoints(fillTriangles), paint.getStrokeWidth(),
                                                 applyPaintAlpha(paint, paint.getFillColor()), PathDrawMode::Fill,
                                                 currentState_.matrix, makeCurrentScissorState(),
                                                 toDrawBlendMode(paint.getBlendMode()));
        fillData.colors = buildFillVertexColors(fillTriangles, paint);
        renderer_.submit(std::make_unique<DrawPathCommand>(fillData));
    }

    if (drawStroke) {
        const ScissorState scissor = makeCurrentScissorState();
        for (const auto &contour : contours) {
            submitStrokeMesh(renderer_, contour.points, contour.closed, paint,
                             currentState_.matrix, scissor);
        }
    }
}

RectF Canvas::measureStrokeBounds(const Path &path, const Paint &paint) const
{
    if (path.isEmpty() || !std::isfinite(paint.getStrokeWidth()) || paint.getStrokeWidth() <= 0.0f) {
        return RectF();
    }

    Path effectedPath;
    const Path *sourcePath = &path;
    if (paint.hasCornerPathEffect()) {
        effectedPath = path.roundedCorners(paint.getCornerPathEffectRadius());
        sourcePath = &effectedPath;
    }

    const auto contours = extractContours(*sourcePath);
    bool hasPoint = false;
    float minX = 0.0f;
    float minY = 0.0f;
    float maxX = 0.0f;
    float maxY = 0.0f;

    auto addPoint = [&](const crushedpixel::Vec2 &point) {
        if (!hasPoint) {
            minX = maxX = point.x;
            minY = maxY = point.y;
            hasPoint = true;
            return;
        }
        minX = std::min(minX, point.x);
        minY = std::min(minY, point.y);
        maxX = std::max(maxX, point.x);
        maxY = std::max(maxY, point.y);
    };

    auto addMesh = [&](const std::vector<crushedpixel::Vec2> &mesh) {
        for (const auto &point : mesh) {
            addPoint(point);
        }
    };

    if (paint.hasDashPathEffect()) {
        Paint dashPaint = paint;
        dashPaint.clearDashPathEffect();
        for (const auto &contour : contours) {
            const auto dashes = buildDashedPolylines(contour.points, contour.closed,
                                                     paint.getDashIntervals(), paint.getDashPhase());
            for (const auto &dash : dashes) {
                addMesh(buildStrokeMesh(dash, false, dashPaint));
            }
        }
    } else {
        for (const auto &contour : contours) {
            addMesh(buildStrokeMesh(contour.points, contour.closed, paint));
        }
    }

    if (!hasPoint) {
        return RectF();
    }

    return RectF(minX, minY, maxX - minX, maxY - minY);
}

void Canvas::drawImage(const Image &image, float x, float y, const Paint &paint)
{
    const RectF dst(x, y, static_cast<float>(image.getWidth()), static_cast<float>(image.getHeight()));
    const RectF src(0.0f, 0.0f, static_cast<float>(image.getWidth()), static_cast<float>(image.getHeight()));
    drawImage(image, src, dst, paint);
}

void Canvas::drawImage(const Image &image, const RectF &dst, const Paint &paint)
{
    const RectF src(0.0f, 0.0f, static_cast<float>(image.getWidth()), static_cast<float>(image.getHeight()));
    drawImage(image, src, dst, paint);
}

void Canvas::drawImage(const Image &image, const RectF &src, const RectF &dst, const Paint &paint)
{
    if (image.getTextureID() == 0 || image.getWidth() <= 0 || image.getHeight() <= 0) {
        return;
    }

    RectF normalizedDst = normalizeRect(dst);
    if (normalizedDst.getWidth() <= 0.0f || normalizedDst.getHeight() <= 0.0f) {
        return;
    }

    RectF clampedSrc = clampSourceRect(src, image.getWidth(), image.getHeight());
    if (clampedSrc.getWidth() <= 0.0f || clampedSrc.getHeight() <= 0.0f) {
        return;
    }

    const float invWidth = 1.0f / static_cast<float>(image.getWidth());
    const float invHeight = 1.0f / static_cast<float>(image.getHeight());

    DrawImageData data;
    data.textureID = image.getTextureID();
    data.x = normalizedDst.getX();
    data.y = normalizedDst.getY();
    data.width = normalizedDst.getWidth();
    data.height = normalizedDst.getHeight();
    data.u0 = clampedSrc.getX() * invWidth;
    data.v0 = clampedSrc.getY() * invHeight;
    data.u1 = (clampedSrc.getX() + clampedSrc.getWidth()) * invWidth;
    data.v1 = (clampedSrc.getY() + clampedSrc.getHeight()) * invHeight;
    const Color tintColor = paint.getColor();
    data.tintColor[0] = tintColor.r();
    data.tintColor[1] = tintColor.g();
    data.tintColor[2] = tintColor.b();
    data.tintColor[3] = 1.0f;
    data.alpha = std::clamp(paint.getColor().a() * paint.getAlphaF(), 0.0f, 1.0f);
    data.sampling = toDrawImageSampling(paint.getImageSampling());
    data.tileMode = toDrawImageTileMode(paint.getImageTileMode());
    data.mipmapsReady = image.hasMipmaps();
    data.transform = currentState_.matrix;
    data.scissor = makeCurrentScissorState();
    data.blendMode = toDrawBlendMode(paint.getBlendMode());
    applyImageColorMatrix(paint, data);

    renderer_.submit(std::make_unique<DrawImageCommand>(data));
}

void Canvas::drawImageFit(const Image &image, const RectF &dst, ImageFit fit, const Paint &paint)
{
    drawImageFit(image, dst, fit, ImageAnchor::CENTER, paint);
}

void Canvas::drawImageFit(const Image &image, const RectF &dst, ImageFit fit, ImageAnchor anchor, const Paint &paint)
{
    drawImageFit(image, dst, fit, anchorXFactor(anchor), anchorYFactor(anchor), paint);
}

void Canvas::drawImageFit(const Image &image, const RectF &dst, ImageFit fit, float alignX, float alignY, const Paint &paint)
{
    if (image.getTextureID() == 0 || image.getWidth() <= 0 || image.getHeight() <= 0) {
        return;
    }

    RectF normalizedDst = normalizeRect(dst);
    if (normalizedDst.getWidth() <= 0.0f || normalizedDst.getHeight() <= 0.0f
        || !std::isfinite(normalizedDst.getX()) || !std::isfinite(normalizedDst.getY())
        || !std::isfinite(normalizedDst.getWidth()) || !std::isfinite(normalizedDst.getHeight())) {
        return;
    }

    const float imageWidth = static_cast<float>(image.getWidth());
    const float imageHeight = static_cast<float>(image.getHeight());
    const float imageAspect = imageWidth / imageHeight;
    const float dstAspect = normalizedDst.getWidth() / normalizedDst.getHeight();
    const float xFactor = std::clamp(std::isfinite(alignX) ? alignX : 0.5f, 0.0f, 1.0f);
    const float yFactor = std::clamp(std::isfinite(alignY) ? alignY : 0.5f, 0.0f, 1.0f);

    if (fit == ImageFit::FILL) {
        drawImage(image, normalizedDst, paint);
        return;
    }

    if (fit == ImageFit::CONTAIN) {
        float fittedWidth = normalizedDst.getWidth();
        float fittedHeight = normalizedDst.getHeight();
        if (dstAspect > imageAspect) {
            fittedWidth = fittedHeight * imageAspect;
        } else {
            fittedHeight = fittedWidth / imageAspect;
        }

        const float fittedX = normalizedDst.getX() + (normalizedDst.getWidth() - fittedWidth) * xFactor;
        const float fittedY = normalizedDst.getY() + (normalizedDst.getHeight() - fittedHeight) * yFactor;
        drawImage(image, RectF(fittedX, fittedY, fittedWidth, fittedHeight), paint);
        return;
    }

    RectF src(0.0f, 0.0f, imageWidth, imageHeight);
    if (imageAspect > dstAspect) {
        const float croppedWidth = imageHeight * dstAspect;
        src = RectF((imageWidth - croppedWidth) * xFactor, 0.0f, croppedWidth, imageHeight);
    } else if (imageAspect < dstAspect) {
        const float croppedHeight = imageWidth / dstAspect;
        src = RectF(0.0f, (imageHeight - croppedHeight) * yFactor, imageWidth, croppedHeight);
    }

    drawImage(image, src, normalizedDst, paint);
}

void Canvas::drawImageNinePatch(const Image &image, const RectF &centerSrc, const RectF &dst, const Paint &paint)
{
    if (image.getTextureID() == 0 || image.getWidth() <= 0 || image.getHeight() <= 0) {
        return;
    }

    RectF normalizedDst = normalizeRect(dst);
    if (normalizedDst.getWidth() <= 0.0f || normalizedDst.getHeight() <= 0.0f
        || !std::isfinite(normalizedDst.getX()) || !std::isfinite(normalizedDst.getY())
        || !std::isfinite(normalizedDst.getWidth()) || !std::isfinite(normalizedDst.getHeight())) {
        return;
    }

    const float imageWidth = static_cast<float>(image.getWidth());
    const float imageHeight = static_cast<float>(image.getHeight());
    const RectF center = clampSourceRect(centerSrc, image.getWidth(), image.getHeight());
    const float left = std::clamp(center.getX(), 0.0f, imageWidth);
    const float top = std::clamp(center.getY(), 0.0f, imageHeight);
    const float right = std::clamp(center.getX() + center.getWidth(), left, imageWidth);
    const float bottom = std::clamp(center.getY() + center.getHeight(), top, imageHeight);

    const float srcX[3] = {0.0f, left, right};
    const float srcY[3] = {0.0f, top, bottom};
    const float srcW[3] = {left, right - left, imageWidth - right};
    const float srcH[3] = {top, bottom - top, imageHeight - bottom};

    float dstLeft = srcW[0];
    float dstRight = srcW[2];
    const float fixedWidth = dstLeft + dstRight;
    if (fixedWidth > normalizedDst.getWidth() && fixedWidth > 0.0f) {
        const float scale = normalizedDst.getWidth() / fixedWidth;
        dstLeft *= scale;
        dstRight *= scale;
    }

    float dstTop = srcH[0];
    float dstBottom = srcH[2];
    const float fixedHeight = dstTop + dstBottom;
    if (fixedHeight > normalizedDst.getHeight() && fixedHeight > 0.0f) {
        const float scale = normalizedDst.getHeight() / fixedHeight;
        dstTop *= scale;
        dstBottom *= scale;
    }

    const float dstW[3] = {dstLeft, normalizedDst.getWidth() - dstLeft - dstRight, dstRight};
    const float dstH[3] = {dstTop, normalizedDst.getHeight() - dstTop - dstBottom, dstBottom};
    const float dstX[3] = {normalizedDst.getX(), normalizedDst.getX() + dstW[0], normalizedDst.getX() + dstW[0] + dstW[1]};
    const float dstY[3] = {normalizedDst.getY(), normalizedDst.getY() + dstH[0], normalizedDst.getY() + dstH[0] + dstH[1]};

    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            if (srcW[col] <= 0.0f || srcH[row] <= 0.0f || dstW[col] <= 0.0f || dstH[row] <= 0.0f) {
                continue;
            }

            drawImage(image,
                      RectF(srcX[col], srcY[row], srcW[col], srcH[row]),
                      RectF(dstX[col], dstY[row], dstW[col], dstH[row]),
                      paint);
        }
    }
}

void Canvas::drawImageTiled(const Image &image, const RectF &dst, const Paint &paint)
{
    drawImageTiled(image, dst, static_cast<float>(image.getWidth()), static_cast<float>(image.getHeight()), paint);
}

void Canvas::drawImageTiled(const Image &image, const RectF &dst, float tileWidth, float tileHeight, const Paint &paint)
{
    if (image.getTextureID() == 0 || image.getWidth() <= 0 || image.getHeight() <= 0) {
        return;
    }

    RectF normalizedDst = normalizeRect(dst);
    if (normalizedDst.getWidth() <= 0.0f || normalizedDst.getHeight() <= 0.0f
        || tileWidth <= 0.0f || tileHeight <= 0.0f || !std::isfinite(tileWidth) || !std::isfinite(tileHeight)) {
        return;
    }

    DrawImageData data;
    data.textureID = image.getTextureID();
    data.x = normalizedDst.getX();
    data.y = normalizedDst.getY();
    data.width = normalizedDst.getWidth();
    data.height = normalizedDst.getHeight();
    data.u0 = 0.0f;
    data.v0 = 0.0f;
    data.u1 = normalizedDst.getWidth() / tileWidth;
    data.v1 = normalizedDst.getHeight() / tileHeight;
    const Color tintColor = paint.getColor();
    data.tintColor[0] = tintColor.r();
    data.tintColor[1] = tintColor.g();
    data.tintColor[2] = tintColor.b();
    data.tintColor[3] = 1.0f;
    data.alpha = std::clamp(paint.getColor().a() * paint.getAlphaF(), 0.0f, 1.0f);
    data.sampling = toDrawImageSampling(paint.getImageSampling());
    data.tileMode = toDrawImageTileMode(paint.getImageTileMode());
    data.mipmapsReady = image.hasMipmaps();
    data.transform = currentState_.matrix;
    data.scissor = makeCurrentScissorState();
    data.blendMode = toDrawBlendMode(paint.getBlendMode());
    applyImageColorMatrix(paint, data);

    renderer_.submit(std::make_unique<DrawImageCommand>(data));
}

void Canvas::drawText(const std::string &text, float x, float y, const Paint &paint)
{
    const std::string asciiText = sanitizeTextToAscii(text);
    if (asciiText.empty()) {
        return;
    }

    if (paint.getTextSize() <= 0.0f) {
        return;
    }

    constexpr float kTextBaseSize = 8.0f;
    const float textScale = std::max(0.01f, paint.getTextSize() / kTextBaseSize);
    const float textHeight = measureAsciiTextHeight(asciiText, textScale);
    float alignedX = x;
    const float textWidth = measureText(asciiText, paint);
    if (paint.getTextAlign() == Paint::TextAlign::CENTER) {
        alignedX -= textWidth * 0.5f;
    } else if (paint.getTextAlign() == Paint::TextAlign::RIGHT) {
        alignedX -= textWidth;
    }

    const float alignedY = y + textBaselineOffset(paint.getTextBaseline(), textHeight);
    std::vector<float> vertices = buildTextVertices(asciiText, alignedX, alignedY, textScale, paint.getLetterSpacing());
    if (vertices.empty()) {
        return;
    }

    const Color color = resolveTextColor(paint);
    DrawTextData data;
    data.vertices = std::move(vertices);
    data.color[0] = color.r();
    data.color[1] = color.g();
    data.color[2] = color.b();
    data.color[3] = color.a();
    data.transform = currentState_.matrix;
    data.scissor = makeCurrentScissorState();
    data.blendMode = toDrawBlendMode(paint.getBlendMode());

    renderer_.submit(std::make_unique<DrawTextCommand>(data));
}

void Canvas::drawTextBox(const std::string &text, const RectF &bounds, const Paint &paint)
{
    drawTextBox(text, bounds, paint.getTextSize() * 1.25f, paint);
}

void Canvas::drawTextBox(const std::string &text, const RectF &bounds, float lineHeight, const Paint &paint)
{
    drawTextBox(text, bounds, lineHeight, 0, false, paint);
}

void Canvas::drawTextBox(const std::string &text, const RectF &bounds, float lineHeight, int maxLines, bool ellipsize, const Paint &paint)
{
    RectF normalizedBounds = normalizeRect(bounds);
    if (text.empty() || normalizedBounds.getWidth() <= 0.0f || normalizedBounds.getHeight() <= 0.0f
        || paint.getTextSize() <= 0.0f) {
        return;
    }

    const float effectiveLineHeight = std::isfinite(lineHeight) && lineHeight > 0.0f
        ? lineHeight
        : paint.getTextSize() * 1.25f;
    if (effectiveLineHeight <= 0.0f) {
        return;
    }

    std::vector<std::string> lines;
    auto appendParagraph = [&](const std::string &paragraph) {
        if (paragraph.empty()) {
            lines.emplace_back();
            return;
        }

        std::string currentLine;
        size_t position = 0;
        while (position < paragraph.size()) {
            while (position < paragraph.size() && paragraph[position] == ' ') {
                ++position;
            }
            if (position >= paragraph.size()) {
                break;
            }

            const size_t wordStart = position;
            while (position < paragraph.size() && paragraph[position] != ' ') {
                ++position;
            }

            const std::string word = paragraph.substr(wordStart, position - wordStart);
            const std::string candidate = currentLine.empty() ? word : currentLine + " " + word;
            if (currentLine.empty() || measureText(candidate, paint) <= normalizedBounds.getWidth()) {
                currentLine = candidate;
            } else {
                lines.push_back(currentLine);
                currentLine = word;
            }
        }

        if (!currentLine.empty()) {
            lines.push_back(currentLine);
        }
    };

    size_t paragraphStart = 0;
    for (size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == '\n') {
            appendParagraph(text.substr(paragraphStart, i - paragraphStart));
            paragraphStart = i + 1;
        }
    }

    if (lines.empty()) {
        return;
    }

    auto ellipsizeLine = [&](std::string line) {
        const std::string marker = "...";
        const float maxWidth = normalizedBounds.getWidth();
        if (measureText(line, paint) <= maxWidth) {
            std::string candidate = line;
            while (!candidate.empty() && candidate.back() == ' ') {
                candidate.pop_back();
            }
            if (measureText(candidate + marker, paint) <= maxWidth) {
                return candidate + marker;
            }
        }

        while (!line.empty() && line.back() == ' ') {
            line.pop_back();
        }

        if (measureText(marker, paint) > maxWidth) {
            std::string dots;
            for (char character : marker) {
                const std::string candidate = dots + character;
                if (measureText(candidate, paint) > maxWidth) {
                    break;
                }
                dots = candidate;
            }
            return dots;
        }

        while (!line.empty() && measureText(line + marker, paint) > maxWidth) {
            line.pop_back();
            while (!line.empty() && line.back() == ' ') {
                line.pop_back();
            }
        }

        return line.empty() ? marker : line + marker;
    };

    float x = normalizedBounds.getX();
    if (paint.getTextAlign() == Paint::TextAlign::CENTER) {
        x += normalizedBounds.getWidth() * 0.5f;
    } else if (paint.getTextAlign() == Paint::TextAlign::RIGHT) {
        x += normalizedBounds.getWidth();
    }

    const int saveCount = save();
    clipRect(normalizedBounds);
    float y = normalizedBounds.getY();
    const float bottom = normalizedBounds.getY() + normalizedBounds.getHeight();
    int drawnLines = 0;
    for (size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
        if (y >= bottom) {
            break;
        }
        if (maxLines > 0 && drawnLines >= maxLines) {
            break;
        }

        std::string line = lines[lineIndex];
        const bool hasMoreLines = lineIndex + 1 < lines.size();
        const bool lastByMaxLines = maxLines > 0 && drawnLines + 1 >= maxLines;
        const bool lastByBounds = y + effectiveLineHeight >= bottom;
        if (ellipsize && (hasMoreLines || measureText(line, paint) > normalizedBounds.getWidth())
            && (lastByMaxLines || lastByBounds)) {
            line = ellipsizeLine(line);
        }

        if (!line.empty()) {
            drawText(line, x, y, paint);
        }
        y += effectiveLineHeight;
        ++drawnLines;
    }
    restoreToCount(saveCount);
}

void Canvas::drawTextOnPath(const std::string &text, const Path &path, const Paint &paint)
{
    drawTextOnPath(text, path, 0.0f, 0.0f, paint);
}

void Canvas::drawTextOnPath(const std::string &text, const Path &path, float hOffset, float vOffset, const Paint &paint)
{
    const std::string asciiText = sanitizeTextToAscii(text);
    const float pathLength = path.length();
    if (asciiText.empty() || pathLength <= 0.0f || paint.getTextSize() <= 0.0f || !std::isfinite(hOffset) || !std::isfinite(vOffset)) {
        return;
    }

    Paint glyphPaint = paint;
    glyphPaint.setTextAlign(Paint::TextAlign::LEFT);

    const float letterSpacing = paint.getLetterSpacing();
    float cursor = hOffset;
    for (char character : asciiText) {
        const std::string glyph(1, character);
        const float glyphWidth = std::max(1.0f, measureText(glyph, glyphPaint));
        const float centerDistance = cursor + glyphWidth * 0.5f;
        cursor += glyphWidth + letterSpacing;

        if (centerDistance < 0.0f) {
            continue;
        }
        if (centerDistance > pathLength) {
            break;
        }

        PointF pathPoint;
        PointF tangent;
        if (!path.pointAndTangentAtLength(centerDistance, pathPoint, tangent)) {
            continue;
        }

        const float angle = std::atan2(tangent.getY(), tangent.getX());
        const float normalX = -tangent.getY();
        const float normalY = tangent.getX();
        const int saveCount = save();
        translate(pathPoint.getX() + normalX * vOffset, pathPoint.getY() + normalY * vOffset);
        rotate(angle);
        drawText(glyph, -glyphWidth * 0.5f, -paint.getTextSize() * 0.5f, glyphPaint);
        restoreToCount(saveCount);
    }
}

float Canvas::measureText(const std::string &text, const Paint &paint) const
{
    const std::string asciiText = sanitizeTextToAscii(text);
    if (asciiText.empty() || paint.getTextSize() <= 0.0f) {
        return 0.0f;
    }

    constexpr float kTextBaseSize = 8.0f;
    const float textScale = std::max(0.01f, paint.getTextSize() / kTextBaseSize);
    return measureAsciiTextWidth(asciiText, textScale, paint.getLetterSpacing());
}

RectF Canvas::measureTextBounds(const std::string &text, const Paint &paint) const
{
    const std::string asciiText = sanitizeTextToAscii(text);
    if (asciiText.empty() || paint.getTextSize() <= 0.0f) {
        return RectF();
    }

    constexpr float kTextBaseSize = 8.0f;
    const float textScale = std::max(0.01f, paint.getTextSize() / kTextBaseSize);
    const float width = measureText(asciiText, paint);
    const float height = measureAsciiTextHeight(asciiText, textScale);

    float left = 0.0f;
    if (paint.getTextAlign() == Paint::TextAlign::CENTER) {
        left = -width * 0.5f;
    } else if (paint.getTextAlign() == Paint::TextAlign::RIGHT) {
        left = -width;
    }

    return RectF(left, textBaselineOffset(paint.getTextBaseline(), height), width, height);
}

Canvas::TextMetrics Canvas::measureTextMetrics(const std::string &text, const Paint &paint) const
{
    TextMetrics metrics;
    metrics.bounds = measureTextBounds(text, paint);
    metrics.width = metrics.bounds.getWidth();
    metrics.height = metrics.bounds.getHeight();
    metrics.top = metrics.bounds.getY();
    metrics.bottom = metrics.bounds.getY() + metrics.bounds.getHeight();
    metrics.ascent = std::min(0.0f, metrics.top);
    metrics.descent = std::max(0.0f, metrics.bottom);
    return metrics;
}

int Canvas::save()
{
    const int savedCount = getSaveCount();
    stateStack_.push_back(currentState_);
    return savedCount;
}

int Canvas::saveLayer(const RectF &bounds, const Paint &paint)
{
    const RectF normalized = normalizeRect(bounds);
    const int savedCount = save();
    LayerState layer;
    layer.saveCount = getSaveCount();
    layer.commandStart = renderer_.commandCount();
    layer.bounds = normalized;
    layer.paint = paint;
    layerStack_.push_back(layer);
    clipRect(normalized);
    return savedCount;
}

int Canvas::saveLayer(const Rect &bounds, const Paint &paint)
{
    return saveLayer(RectF(static_cast<float>(bounds.getX()), static_cast<float>(bounds.getY()),
                           static_cast<float>(bounds.getWidth()), static_cast<float>(bounds.getHeight())),
                     paint);
}

void Canvas::restore()
{
    if (stateStack_.empty()) {
        return;
    }

    if (!layerStack_.empty() && layerStack_.back().saveCount == getSaveCount()) {
        restoreLayer(layerStack_.back());
        layerStack_.pop_back();
    }

    currentState_ = stateStack_.back();
    stateStack_.pop_back();
}

int Canvas::getSaveCount() const
{
    return static_cast<int>(stateStack_.size()) + 1;
}

const glm::mat4& Canvas::getMatrix() const
{
    return currentState_.matrix;
}

PointF Canvas::mapPoint(const PointF &point) const
{
    PointF mapped;
    if (!transformPoint(currentState_.matrix, point, mapped)) {
        return PointF();
    }
    return mapped;
}

RectF Canvas::mapRect(const RectF &rect) const
{
    RectF mapped;
    if (!transformRectBounds(rect, currentState_.matrix, mapped)) {
        return RectF();
    }
    return mapped;
}

RectF Canvas::mapRect(const Rect &rect) const
{
    return mapRect(RectF(static_cast<float>(rect.getX()), static_cast<float>(rect.getY()),
                         static_cast<float>(rect.getWidth()), static_cast<float>(rect.getHeight())));
}

bool Canvas::inverseMapPoint(const PointF &devicePoint, PointF &localPoint) const
{
    const float determinant = glm::determinant(currentState_.matrix);
    if (!std::isfinite(determinant) || std::abs(determinant) <= kPointEpsilon) {
        return false;
    }

    return transformPoint(glm::inverse(currentState_.matrix), devicePoint, localPoint);
}

bool Canvas::inverseMapRect(const RectF &deviceRect, RectF &localRect) const
{
    const float determinant = glm::determinant(currentState_.matrix);
    if (!std::isfinite(determinant) || std::abs(determinant) <= kPointEpsilon) {
        return false;
    }

    return transformRectBounds(deviceRect, glm::inverse(currentState_.matrix), localRect);
}

bool Canvas::isPointInClip(const PointF &devicePoint) const
{
    RectF clipBounds;
    if (!getClipBounds(clipBounds)) {
        return false;
    }

    const float x = devicePoint.getX();
    const float y = devicePoint.getY();
    return x >= clipBounds.getX()
        && y >= clipBounds.getY()
        && x <= clipBounds.getX() + clipBounds.getWidth()
        && y <= clipBounds.getY() + clipBounds.getHeight();
}

bool Canvas::hitTestPathFill(const Path &path, const PointF &devicePoint) const
{
    if (!isPointInClip(devicePoint)) {
        return false;
    }

    PointF localPoint;
    return inverseMapPoint(devicePoint, localPoint) && path.contains(localPoint);
}

bool Canvas::hitTestPathStroke(const Path &path, const PointF &devicePoint, float strokeWidth) const
{
    if (!isPointInClip(devicePoint)) {
        return false;
    }

    PointF localPoint;
    return inverseMapPoint(devicePoint, localPoint) && path.strokeContains(localPoint, strokeWidth);
}

bool Canvas::hasClip() const
{
    return currentState_.clip.enabled;
}

bool Canvas::getClipBounds(RectF &bounds) const
{
    if (width_ <= 0 || height_ <= 0) {
        bounds = RectF();
        return false;
    }

    const RectF canvasBounds(0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
    bounds = currentState_.clip.enabled
        ? intersectRects(normalizeRect(currentState_.clip.rect), canvasBounds)
        : canvasBounds;

    return bounds.getWidth() > 0.0f && bounds.getHeight() > 0.0f;
}

bool Canvas::quickReject(const RectF &rect) const
{
    RectF clipBounds;
    if (!getClipBounds(clipBounds)) {
        return true;
    }

    RectF transformedBounds;
    if (!transformRectBounds(rect, currentState_.matrix, transformedBounds)) {
        return false;
    }

    const RectF overlap = intersectRects(transformedBounds, clipBounds);
    return overlap.getWidth() <= 0.0f || overlap.getHeight() <= 0.0f;
}

bool Canvas::quickReject(const Rect &rect) const
{
    return quickReject(RectF(static_cast<float>(rect.getX()), static_cast<float>(rect.getY()),
                             static_cast<float>(rect.getWidth()), static_cast<float>(rect.getHeight())));
}

bool Canvas::quickReject(const Path &path, const Paint &paint) const
{
    if (path.isEmpty()) {
        return true;
    }

    Path effectedPath;
    const Path *sourcePath = &path;
    if (paint.hasCornerPathEffect()) {
        effectedPath = path.roundedCorners(paint.getCornerPathEffectRadius());
        sourcePath = &effectedPath;
    }

    bool hasBounds = false;
    RectF bounds;
    auto addBounds = [&](const RectF &candidate) {
        if (!hasBounds) {
            bounds = candidate;
            hasBounds = true;
            return;
        }

        const float left = std::min(bounds.getX(), candidate.getX());
        const float top = std::min(bounds.getY(), candidate.getY());
        const float right = std::max(bounds.getX() + bounds.getWidth(), candidate.getX() + candidate.getWidth());
        const float bottom = std::max(bounds.getY() + bounds.getHeight(), candidate.getY() + candidate.getHeight());
        bounds = RectF(left, top, right - left, bottom - top);
    };

    const bool drawFill = paint.getStyle() == Paint::Style::FILL || paint.getStyle() == Paint::Style::FILL_AND_STROKE;
    const bool drawStroke = paint.getStyle() == Paint::Style::STROKE || paint.getStyle() == Paint::Style::FILL_AND_STROKE;
    if (drawFill) {
        addBounds(sourcePath->getBounds());
    }
    if (drawStroke) {
        addBounds(measureStrokeBounds(path, paint));
    }

    if (!hasBounds) {
        return true;
    }

    if (paint.hasShadowLayer()) {
        const float radius = std::max(0.0f, paint.getShadowRadius());
        addBounds(RectF(bounds.getX() + paint.getShadowDx() - radius,
                        bounds.getY() + paint.getShadowDy() - radius,
                        bounds.getWidth() + radius * 2.0f,
                        bounds.getHeight() + radius * 2.0f));
    }

    return quickReject(bounds);
}

void Canvas::restoreToCount(int saveCount)
{
    const int targetCount = std::max(1, saveCount);
    while (getSaveCount() > targetCount && !stateStack_.empty()) {
        restore();
    }
}

void Canvas::restoreLayer(const LayerState &layer)
{
    auto commands = renderer_.takeCommandsFrom(layer.commandStart);
    if (commands.empty() || width_ <= 0 || height_ <= 0) {
        return;
    }

    const RectF canvasBounds(0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
    const RectF clipped = intersectRects(layer.bounds, canvasBounds);
    if (clipped.getWidth() <= 0.0f || clipped.getHeight() <= 0.0f) {
        return;
    }

    const int layerLeft = std::clamp(static_cast<int>(std::floor(clipped.getX())), 0, width_);
    const int layerTop = std::clamp(static_cast<int>(std::floor(clipped.getY())), 0, height_);
    const int layerRight = std::clamp(static_cast<int>(std::ceil(clipped.getX() + clipped.getWidth())), 0, width_);
    const int layerBottom = std::clamp(static_cast<int>(std::ceil(clipped.getY() + clipped.getHeight())), 0, height_);
    const int layerWidth = layerRight - layerLeft;
    const int layerHeight = layerBottom - layerTop;
    if (layerWidth <= 0 || layerHeight <= 0) {
        return;
    }

    const RectF layerRect(static_cast<float>(layerLeft), static_cast<float>(layerTop),
                          static_cast<float>(layerWidth), static_cast<float>(layerHeight));

    GLint previousFramebuffer = 0;
    GLint previousTexture = 0;
    GLint previousViewport[4] = {0, 0, 0, 0};
    GLfloat previousClearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
    glGetIntegerv(GL_VIEWPORT, previousViewport);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, previousClearColor);

    unsigned int framebuffer = 0;
    unsigned int texture = 0;
    if (!createLayerTarget(layerWidth, layerHeight, framebuffer, texture)) {
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<unsigned int>(previousFramebuffer));
        glBindTexture(GL_TEXTURE_2D, static_cast<unsigned int>(previousTexture));
        renderer_.appendCommands(std::move(commands));
        return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glViewport(-layerLeft, -(height_ - layerBottom), width_, height_);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    executeCommandList(commands, width_, height_, -layerLeft, -(height_ - layerBottom));

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<unsigned int>(previousFramebuffer));
    glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
    glClearColor(previousClearColor[0], previousClearColor[1], previousClearColor[2], previousClearColor[3]);
    glBindTexture(GL_TEXTURE_2D, static_cast<unsigned int>(previousTexture));
    glDeleteFramebuffers(1, &framebuffer);
    glDisable(GL_SCISSOR_TEST);

    DrawImageData data;
    data.textureID = texture;
    data.ownsTexture = true;
    data.x = layerRect.getX();
    data.y = layerRect.getY();
    data.width = layerRect.getWidth();
    data.height = layerRect.getHeight();
    data.u0 = 0.0f;
    data.u1 = 1.0f;
    data.v0 = 1.0f;
    data.v1 = 0.0f;
    data.alpha = std::clamp(layer.paint.getColor().a() * layer.paint.getAlphaF(), 0.0f, 1.0f);
    data.sampling = toDrawImageSampling(layer.paint.getImageSampling());
    data.tileMode = DrawImageTileMode::Clamp;
    data.transform = glm::mat4(1.0f);
    data.scissor = makeScissorForRect(layerRect, width_, height_);
    data.blendMode = toDrawBlendMode(layer.paint.getBlendMode());
    applyImageColorMatrix(layer.paint, data);
    renderer_.submit(std::make_unique<DrawImageCommand>(data));
}

void Canvas::clipRect(const RectF &rect)
{
    RectF normalized = normalizeRect(rect);
    RectF canvasBounds(0.0f, 0.0f, static_cast<float>(width_), static_cast<float>(height_));
    RectF clipped = intersectRects(normalized, canvasBounds);

    if (!currentState_.clip.enabled) {
        currentState_.clip.enabled = true;
        currentState_.clip.rect = clipped;
        return;
    }

    currentState_.clip.rect = intersectRects(currentState_.clip.rect, clipped);
}

void Canvas::clipRect(const Rect &rect)
{
    clipRect(RectF(static_cast<float>(rect.getX()), static_cast<float>(rect.getY()),
                   static_cast<float>(rect.getWidth()), static_cast<float>(rect.getHeight())));
}

void Canvas::setMatrix(const glm::mat4 &matrix)
{
    currentState_.matrix = matrix;
}

void Canvas::resetMatrix()
{
    currentState_.matrix = glm::mat4(1.0f);
}

void Canvas::concat(const glm::mat4 &matrix)
{
    currentState_.matrix *= matrix;
}

void Canvas::translate(float dx, float dy)
{
    currentState_.matrix = glm::translate(currentState_.matrix, glm::vec3(dx, dy, 0.0f));
}

void Canvas::scale(float sx, float sy)
{
    currentState_.matrix = glm::scale(currentState_.matrix, glm::vec3(sx, sy, 1.0f));
}

void Canvas::rotate(float radians)
{
    currentState_.matrix = glm::rotate(currentState_.matrix, radians, glm::vec3(0.0f, 0.0f, 1.0f));
}

ScissorState Canvas::makeCurrentScissorState() const
{
    ScissorState scissor;
    if (!currentState_.clip.enabled) {
        return scissor;
    }

    const RectF normalized = normalizeRect(currentState_.clip.rect);
    const int left = static_cast<int>(std::floor(normalized.getX()));
    const int top = static_cast<int>(std::floor(normalized.getY()));
    const int right = static_cast<int>(std::ceil(normalized.getX() + normalized.getWidth()));
    const int bottom = static_cast<int>(std::ceil(normalized.getY() + normalized.getHeight()));

    const int clampedLeft = std::clamp(left, 0, width_);
    const int clampedTop = std::clamp(top, 0, height_);
    const int clampedRight = std::clamp(right, 0, width_);
    const int clampedBottom = std::clamp(bottom, 0, height_);

    scissor.enabled = true;
    scissor.x = clampedLeft;
    scissor.y = height_ - clampedBottom;
    scissor.width = std::max(0, clampedRight - clampedLeft);
    scissor.height = std::max(0, clampedBottom - clampedTop);
    return scissor;
}


void Canvas::beginFrame()
{
    glDisable(GL_SCISSOR_TEST);
    layerStack_.clear();
    renderer_.clear();
}

void Canvas::flush()
{
    while (!layerStack_.empty() && !stateStack_.empty()) {
        restore();
    }
    renderer_.flush();
    renderer_.clear();
    glDisable(GL_SCISSOR_TEST);
}

void Canvas::endFrame()
{
    flush();
}

bool Canvas::readPixelsRGBA(std::vector<unsigned char> &pixels) const
{
    if (width_ <= 0 || height_ <= 0) {
        pixels.clear();
        return false;
    }

    const size_t rowSize = static_cast<size_t>(width_) * 4;
    const size_t bufferSize = rowSize * static_cast<size_t>(height_);
    std::vector<unsigned char> bottomUp(bufferSize);
    pixels.resize(bufferSize);

    GLint previousPackAlignment = 4;
    glGetIntegerv(GL_PACK_ALIGNMENT, &previousPackAlignment);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE, bottomUp.data());
    glPixelStorei(GL_PACK_ALIGNMENT, previousPackAlignment);

    for (int y = 0; y < height_; ++y) {
        const size_t srcOffset = static_cast<size_t>(height_ - 1 - y) * rowSize;
        const size_t dstOffset = static_cast<size_t>(y) * rowSize;
        std::copy(bottomUp.begin() + srcOffset, bottomUp.begin() + srcOffset + rowSize,
                  pixels.begin() + dstOffset);
    }

    return true;
}

std::vector<unsigned char> Canvas::readPixelsRGBA() const
{
    std::vector<unsigned char> pixels;
    readPixelsRGBA(pixels);
    return pixels;
}

bool Canvas::savePixelsPPM(const std::string &path) const
{
    std::vector<unsigned char> pixels;
    if (!readPixelsRGBA(pixels) || pixels.empty()) {
        return false;
    }

    std::ofstream output(path, std::ios::binary);
    if (!output) {
        return false;
    }

    output << "P6\n" << width_ << ' ' << height_ << "\n255\n";
    for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
        const char rgb[3] = {
            static_cast<char>(pixels[i]),
            static_cast<char>(pixels[i + 1]),
            static_cast<char>(pixels[i + 2])
        };
        output.write(rgb, sizeof(rgb));
    }

    return output.good();
}

std::uint64_t Canvas::hashPixelsRGBA(const std::vector<unsigned char> &pixels)
{
    constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ull;
    constexpr std::uint64_t kFnvPrime = 1099511628211ull;

    std::uint64_t hash = kFnvOffsetBasis;
    for (unsigned char value : pixels) {
        hash ^= static_cast<std::uint64_t>(value);
        hash *= kFnvPrime;
    }
    return hash;
}

std::uint64_t Canvas::computePixelsHashRGBA() const
{
    std::vector<unsigned char> pixels;
    if (!readPixelsRGBA(pixels)) {
        return 0;
    }
    return hashPixelsRGBA(pixels);
}

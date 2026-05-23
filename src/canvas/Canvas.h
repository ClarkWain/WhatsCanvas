#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <vector>
#include <memory>
#include <string>
#include <cstdint>
#include <glm.hpp>
#include "Image.h"
#include "Paint.h"
#include "Path.h"
#include "render/Renderer.h"
#include "base.h"

class Canvas
{
public:
    struct TextMetrics
    {
        float width = 0.0f;
        float height = 0.0f;
        float top = 0.0f;
        float bottom = 0.0f;
        float ascent = 0.0f;
        float descent = 0.0f;
        RectF bounds;
    };

    enum class ImageFit
    {
        FILL,
        CONTAIN,
        COVER
    };

    enum class ImageAnchor
    {
        TOP_LEFT,
        TOP,
        TOP_RIGHT,
        LEFT,
        CENTER,
        RIGHT,
        BOTTOM_LEFT,
        BOTTOM,
        BOTTOM_RIGHT
    };

    static void initialize();
    static void finalize();

public:
    void setSize(int width, int height);
    int getWidth() const { return width_; }
    int getHeight() const { return height_; }
    void setColor(Color color) { color_ = color; }
    void drawColor(const Color &color);
    void drawPaint(const Paint &paint);

    void drawPoint(int x, int y, const Paint &paint);
    void drawPoint(float x, float y, const Paint &paint);
    void drawPoint(const Point &point, const Paint &paint);
    void drawPoint(const PointF &point, const Paint &paint);
    void drawPoints(const std::vector<Point> &points, const Paint &paint);
    void drawPoints(const std::vector<PointF> &points, const Paint &paint);

    void drawLine(int x1, int y1, int x2, int y2, const Paint &paint);
    void drawLine(float x1, float y1, float x2, float y2, const Paint &paint);
    void drawLine(const Point &start, const Point &end, const Paint &paint);
    void drawLine(const PointF &start, const PointF &end, const Paint &paint);
    void drawLines(const std::vector<Point> &points, const Paint &paint);
    void drawLines(const std::vector<PointF> &points, const Paint &paint);
    void drawPolyline(const std::vector<Point> &points, const Paint &paint);
    void drawPolyline(const std::vector<PointF> &points, const Paint &paint);
    void drawPolygon(const std::vector<Point> &points, const Paint &paint);
    void drawPolygon(const std::vector<PointF> &points, const Paint &paint);
    void drawRect(const RectF &rect, const Paint &paint);
    void drawRect(const Rect &rect, const Paint &paint);
    void drawRoundRect(const RectF &rect, float radius, const Paint &paint);
    void drawRoundRect(const Rect &rect, float radius, const Paint &paint);
    void drawRoundRect(const RectF &rect, float topLeftRadius, float topRightRadius,
                       float bottomRightRadius, float bottomLeftRadius, const Paint &paint);
    void drawRoundRect(const Rect &rect, float topLeftRadius, float topRightRadius,
                       float bottomRightRadius, float bottomLeftRadius, const Paint &paint);
    void drawCircle(float centerX, float centerY, float radius, const Paint &paint);
    void drawCircle(const PointF &center, float radius, const Paint &paint);
    void drawCircle(const Point &center, float radius, const Paint &paint);
    void drawOval(const RectF &bounds, const Paint &paint);
    void drawOval(const Rect &bounds, const Paint &paint);
    void drawArc(const RectF &bounds, float startRadians, float sweepRadians, bool useCenter, const Paint &paint);
    void drawArc(const Rect &bounds, float startRadians, float sweepRadians, bool useCenter, const Paint &paint);
    void drawPath(const Path &path, const Paint &paint);
    RectF measureStrokeBounds(const Path &path, const Paint &paint) const;
    void drawImage(const Image &image, float x, float y, const Paint &paint);
    void drawImage(const Image &image, const RectF &dst, const Paint &paint);
    void drawImage(const Image &image, const RectF &src, const RectF &dst, const Paint &paint);
    void drawImageFit(const Image &image, const RectF &dst, ImageFit fit, const Paint &paint);
    void drawImageFit(const Image &image, const RectF &dst, ImageFit fit, ImageAnchor anchor, const Paint &paint);
    void drawImageFit(const Image &image, const RectF &dst, ImageFit fit, float alignX, float alignY, const Paint &paint);
    void drawImageNinePatch(const Image &image, const RectF &centerSrc, const RectF &dst, const Paint &paint);
    void drawImageTiled(const Image &image, const RectF &dst, const Paint &paint);
    void drawImageTiled(const Image &image, const RectF &dst, float tileWidth, float tileHeight, const Paint &paint);
    void drawText(const std::string &text, float x, float y, const Paint &paint);
    void drawTextBox(const std::string &text, const RectF &bounds, const Paint &paint);
    void drawTextBox(const std::string &text, const RectF &bounds, float lineHeight, const Paint &paint);
    void drawTextBox(const std::string &text, const RectF &bounds, float lineHeight, int maxLines, bool ellipsize, const Paint &paint);
    void drawTextOnPath(const std::string &text, const Path &path, const Paint &paint);
    void drawTextOnPath(const std::string &text, const Path &path, float hOffset, float vOffset, const Paint &paint);
    float measureText(const std::string &text, const Paint &paint) const;
    RectF measureTextBounds(const std::string &text, const Paint &paint) const;
    TextMetrics measureTextMetrics(const std::string &text, const Paint &paint) const;

    int save();
    int saveLayer(const RectF &bounds, const Paint &paint);
    int saveLayer(const Rect &bounds, const Paint &paint);
    void restore();
    int getSaveCount() const;
    void restoreToCount(int saveCount);
    const glm::mat4& getMatrix() const;
    PointF mapPoint(const PointF &point) const;
    RectF mapRect(const RectF &rect) const;
    RectF mapRect(const Rect &rect) const;
    bool inverseMapPoint(const PointF &devicePoint, PointF &localPoint) const;
    bool inverseMapRect(const RectF &deviceRect, RectF &localRect) const;
    bool isPointInClip(const PointF &devicePoint) const;
    bool hitTestPathFill(const Path &path, const PointF &devicePoint) const;
    bool hitTestPathStroke(const Path &path, const PointF &devicePoint, float strokeWidth) const;
    bool hasClip() const;
    bool getClipBounds(RectF &bounds) const;
    bool quickReject(const RectF &rect) const;
    bool quickReject(const Rect &rect) const;
    bool quickReject(const Path &path, const Paint &paint) const;
    void clipRect(const RectF &rect);
    void clipRect(const Rect &rect);
    void setMatrix(const glm::mat4 &matrix);
    void resetMatrix();
    void concat(const glm::mat4 &matrix);
    void translate(float dx, float dy);
    void scale(float sx, float sy);
    void rotate(float radians);

    void beginFrame();
    void flush();
    void endFrame();
    bool readPixelsRGBA(std::vector<unsigned char> &pixels) const;
    std::vector<unsigned char> readPixelsRGBA() const;
    bool savePixelsPPM(const std::string &path) const;
    static std::uint64_t hashPixelsRGBA(const std::vector<unsigned char> &pixels);
    std::uint64_t computePixelsHashRGBA() const;

private:
    struct ClipState {
        bool enabled = false;
        RectF rect;
    };

    struct CanvasState {
        glm::mat4 matrix = glm::mat4(1.0f);
        ClipState clip;
    };

    struct LayerState {
        int saveCount = 1;
        std::size_t commandStart = 0;
        RectF bounds;
        Paint paint;
    };

    ScissorState makeCurrentScissorState() const;
    void restoreLayer(const LayerState &layer);

    int width_ = 0;
    int height_ = 0;
    Color color_;
    Renderer renderer_;
    CanvasState currentState_;
    std::vector<CanvasState> stateStack_;
    std::vector<LayerState> layerStack_;
};

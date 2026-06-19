#pragma once

#include <cstddef>
#include <vector>

#include "Export.h"
#include "base.h"

namespace wsc {

/// Mutable 2D path with measurement, trimming and hit-test helpers.
class WSC_API Path {
public:
    enum class FillType {
        WINDING,
        EVEN_ODD
    };

    enum class Op {
        MOVE_TO,
        LINE_TO,
        CLOSE
    };

    struct PathPoint {
        Op op;
        PointF point;

        PathPoint(Op op, const PointF &point);
    };

    Path();

    void setFillType(FillType fillType);
    FillType getFillType() const;

    void moveTo(float x, float y);
    void lineTo(float x, float y);
    void quadTo(float controlX, float controlY, float endX, float endY);
    void quadTo(const PointF &control, const PointF &end);
    void cubicTo(float control1X, float control1Y, float control2X, float control2Y, float endX, float endY);
    void cubicTo(const PointF &control1, const PointF &control2, const PointF &end);
    void close();
    void reset();

    const std::vector<PathPoint> &getPoints() const;
    bool isEmpty() const;
    std::size_t getContourCount() const;
    std::size_t getClosedContourCount() const;
    bool isClosed() const;
    RectF getBounds() const;
    RectF getStrokeBounds(float strokeWidth) const;
    std::vector<RectF> getContourBounds() const;

    bool contains(float x, float y) const;
    bool contains(const PointF &point) const;
    bool strokeContains(float x, float y, float strokeWidth) const;
    bool strokeContains(const PointF &point, float strokeWidth) const;

    float length() const;
    bool pointAtLength(float targetLength, PointF &point) const;
    bool pointAndTangentAtLength(float targetLength, PointF &point, PointF &tangent) const;
    Path reversed() const;
    Path trim(float startLength, float endLength) const;
    Path trim(float startLength, float endLength, bool wrap) const;
    Path trim(float startLength, float endLength, bool wrap, bool reverse) const;
    Path roundedCorners(float radius) const;

    void offset(float dx, float dy);
    void addRect(const RectF &rect);
    void addOval(const RectF &rect);
    void addRoundRect(const RectF &rect, float radius);
    void addRoundRect(const RectF &rect, float topLeftRadius, float topRightRadius,
                      float bottomRightRadius, float bottomLeftRadius);
    void addCircle(float x, float y, float radius);

private:
    static RectF normalizeRect(const RectF &rect);
    static int computeEllipseSegments(float radiusX, float radiusY);
    static void scaleRadiusPair(float &scale, float available, float a, float b);
    static int computeCornerSegments(float radius);
    void appendQuarterArc(float centerX, float centerY, float radius, float startAngle, float endAngle);
    static float distance(const PointF &start, const PointF &end);
    static int computeCurveSegments(float controlPolygonLength);

    std::vector<PathPoint> points_;
    FillType fillType_ = FillType::WINDING;
    PointF currentPoint_;
    PointF contourStart_;
    bool hasCurrentPoint_ = false;
    bool hasContourStart_ = false;
};

} // namespace wsc

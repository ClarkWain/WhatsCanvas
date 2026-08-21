#pragma once

#include <cstddef>
#include <vector>

#include "Export.h"
#include "base.h"

namespace wsc {

/// Mutable, backend-neutral 2D path in local Canvas coordinates.
///
/// A path may contain multiple contours. Curve calls are flattened into line
/// segments when recorded, so measurement, hit testing and rendering consume
/// the same stable geometry. Path values are ordinary copyable CPU objects and
/// may be reused across canvases.
class WSC_API Path {
public:
    /// Fill rule used to decide interior regions of self-intersecting paths.
    enum class FillType {
        WINDING,
        EVEN_ODD
    };

    /// Type of a recorded path verb.
    enum class Op {
        MOVE_TO,
        LINE_TO,
        CLOSE
    };

    /// One recorded flattened path verb. getPoints() exposes these for
    /// inspection; callers must not retain references after mutating the Path.
    struct PathPoint {
        Op op;
        PointF point;

        PathPoint(Op op, const PointF &point);
    };

    Path();

    /// Select the winding rule used by fill drawing and contains().
    void setFillType(FillType fillType);

    FillType getFillType() const;

    /// Start a new contour at (x, y).
    void moveTo(float x, float y);

    /// Add a straight segment to (x, y).
    void lineTo(float x, float y);

    /// Add a flattened quadratic Bezier segment. With no current contour this
    /// behaves like moveTo(end).
    void quadTo(float controlX, float controlY, float endX, float endY);

    void quadTo(const PointF &control, const PointF &end);

    /// Add a flattened cubic Bezier segment. With no current contour this
    /// behaves like moveTo(end).
    void cubicTo(float control1X, float control1Y, float control2X, float control2Y, float endX, float endY);

    void cubicTo(const PointF &control1, const PointF &control2, const PointF &end);

    /// Close the current contour back to its start point.
    void close();

    /// Clear all contours.
    void reset();

    /// Borrow the internal flattened verb list until the next mutation.
    const std::vector<PathPoint> &getPoints() const;

    bool isEmpty() const;

    std::size_t getContourCount() const;

    std::size_t getClosedContourCount() const;

    bool isClosed() const;

    /// Tight bounding box of the path geometry (fill).
    RectF getBounds() const;

    /// Conservative local bounds for a simple centered stroke. Use
    /// Canvas::measureStrokeBounds for Paint cap/join/dash/effect accuracy.
    RectF getStrokeBounds(float strokeWidth) const;

    std::vector<RectF> getContourBounds() const;

    /// Point-in-fill / point-near-stroke hit tests.
    bool contains(float x, float y) const;

    bool contains(const PointF &point) const;

    /// Centerline-distance stroke test using the supplied width. It does not
    /// model Paint cap/join/dash effects; use Canvas hit testing for transforms/clip.
    bool strokeContains(float x, float y, float strokeWidth) const;

    bool strokeContains(const PointF &point, float strokeWidth) const;

    /// Total outline length of the path.
    float length() const;

    /// Sample a distance along all contours in order. Negative distances clamp
    /// to zero; distances beyond the end return the final point/tangent. False
    /// means the input is non-finite or the path has no non-zero segment.
    bool pointAtLength(float targetLength, PointF &point) const;

    /// Tangent output is normalized when successful.
    bool pointAndTangentAtLength(float targetLength, PointF &point, PointF &tangent) const;

    /// Return a copy with contour direction reversed.
    Path reversed() const;

    /// Return the geometry between absolute path lengths. Non-wrapping input is
    /// ordered/clamped; `wrap` allows a span to cross the path end; `reverse`
    /// measures the requested span from reversed contour direction.
    Path trim(float startLength, float endLength) const;

    Path trim(float startLength, float endLength, bool wrap) const;

    Path trim(float startLength, float endLength, bool wrap, bool reverse) const;

    /// Return a copy with corners rounded by the given radius.
    Path roundedCorners(float radius) const;

    /// Translate all points by (dx, dy).
    void offset(float dx, float dy);

    /// Append common shapes as new contours. Non-positive radii/sizes produce
    /// no drawable area.
    void addRect(const RectF &rect);

    /// Shape rectangles use `(x,y,width,height)` and append a new closed contour.
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

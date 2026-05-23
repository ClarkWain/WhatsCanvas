#pragma once

#include <algorithm>
#include <cmath>
#include <vector>
#include "base.h"

class Path {
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
        
        PathPoint(Op op, const PointF& point) : op(op), point(point) {}
    };

    Path() = default;

    void setFillType(FillType fillType) {
        fillType_ = fillType;
    }

    FillType getFillType() const {
        return fillType_;
    }

    // 移动到指定点
    void moveTo(float x, float y) {
        points_.emplace_back(Op::MOVE_TO, PointF(x, y));
        currentPoint_ = PointF(x, y);
        contourStart_ = currentPoint_;
        hasCurrentPoint_ = true;
        hasContourStart_ = true;
    }

    // 从当前点连线到指定点
    void lineTo(float x, float y) {
        if (!hasCurrentPoint_) {
            moveTo(x, y);
            return;
        }

        points_.emplace_back(Op::LINE_TO, PointF(x, y));
        currentPoint_ = PointF(x, y);
        hasCurrentPoint_ = true;
    }

    void quadTo(float controlX, float controlY, float endX, float endY) {
        if (!hasCurrentPoint_) {
            moveTo(endX, endY);
            return;
        }

        const PointF start = currentPoint_;
        const PointF control(controlX, controlY);
        const PointF end(endX, endY);
        const int segments = computeCurveSegments(distance(start, control) + distance(control, end));

        for (int segment = 1; segment <= segments; ++segment) {
            const float progress = static_cast<float>(segment) / static_cast<float>(segments);
            const float inverse = 1.0f - progress;
            const float curveX = inverse * inverse * start.getX()
                + 2.0f * inverse * progress * control.getX()
                + progress * progress * end.getX();
            const float curveY = inverse * inverse * start.getY()
                + 2.0f * inverse * progress * control.getY()
                + progress * progress * end.getY();
            lineTo(curveX, curveY);
        }
    }

    void quadTo(const PointF& control, const PointF& end) {
        quadTo(control.getX(), control.getY(), end.getX(), end.getY());
    }

    void cubicTo(float control1X, float control1Y, float control2X, float control2Y, float endX, float endY) {
        if (!hasCurrentPoint_) {
            moveTo(endX, endY);
            return;
        }

        const PointF start = currentPoint_;
        const PointF control1(control1X, control1Y);
        const PointF control2(control2X, control2Y);
        const PointF end(endX, endY);
        const int segments = computeCurveSegments(
            distance(start, control1) + distance(control1, control2) + distance(control2, end));

        for (int segment = 1; segment <= segments; ++segment) {
            const float progress = static_cast<float>(segment) / static_cast<float>(segments);
            const float inverse = 1.0f - progress;
            const float curveX = inverse * inverse * inverse * start.getX()
                + 3.0f * inverse * inverse * progress * control1.getX()
                + 3.0f * inverse * progress * progress * control2.getX()
                + progress * progress * progress * end.getX();
            const float curveY = inverse * inverse * inverse * start.getY()
                + 3.0f * inverse * inverse * progress * control1.getY()
                + 3.0f * inverse * progress * progress * control2.getY()
                + progress * progress * progress * end.getY();
            lineTo(curveX, curveY);
        }
    }

    void cubicTo(const PointF& control1, const PointF& control2, const PointF& end) {
        cubicTo(control1.getX(), control1.getY(), control2.getX(), control2.getY(), end.getX(), end.getY());
    }

    // 闭合路径（连接到起始点）
    void close() {
        points_.emplace_back(Op::CLOSE, PointF());
        if (hasContourStart_) {
            currentPoint_ = contourStart_;
            hasCurrentPoint_ = true;
        }
    }

    // 重置路径
    void reset() {
        points_.clear();
        hasCurrentPoint_ = false;
        hasContourStart_ = false;
        fillType_ = FillType::WINDING;
    }

    // 获取路径点集合
    const std::vector<PathPoint>& getPoints() const {
        return points_;
    }

    // 检查路径是否为空
    bool isEmpty() const {
        return points_.empty();
    }

    size_t getContourCount() const {
        size_t count = 0;
        for (const auto& pathPoint : points_) {
            if (pathPoint.op == Op::MOVE_TO) {
                ++count;
            }
        }
        return count;
    }

    size_t getClosedContourCount() const {
        size_t count = 0;
        bool hasContour = false;
        for (const auto& pathPoint : points_) {
            if (pathPoint.op == Op::MOVE_TO) {
                hasContour = true;
            } else if (pathPoint.op == Op::CLOSE && hasContour) {
                ++count;
                hasContour = false;
            }
        }
        return count;
    }

    bool isClosed() const {
        const size_t contourCount = getContourCount();
        return contourCount > 0 && contourCount == getClosedContourCount();
    }

    RectF getBounds() const {
        bool hasPoint = false;
        float minX = 0.0f;
        float minY = 0.0f;
        float maxX = 0.0f;
        float maxY = 0.0f;

        for (const auto& pathPoint : points_) {
            if (pathPoint.op == Op::CLOSE) {
                continue;
            }

            const float x = pathPoint.point.getX();
            const float y = pathPoint.point.getY();
            if (!hasPoint) {
                minX = maxX = x;
                minY = maxY = y;
                hasPoint = true;
                continue;
            }

            minX = std::min(minX, x);
            minY = std::min(minY, y);
            maxX = std::max(maxX, x);
            maxY = std::max(maxY, y);
        }

        if (!hasPoint) {
            return RectF();
        }

        return RectF(minX, minY, maxX - minX, maxY - minY);
    }

    RectF getStrokeBounds(float strokeWidth) const {
        RectF bounds = getBounds();
        if (isEmpty() || !std::isfinite(strokeWidth) || strokeWidth <= 0.0f) {
            return bounds;
        }

        const float outset = strokeWidth * 0.5f;
        return RectF(bounds.getX() - outset, bounds.getY() - outset,
                     bounds.getWidth() + strokeWidth, bounds.getHeight() + strokeWidth);
    }

    std::vector<RectF> getContourBounds() const {
        std::vector<RectF> bounds;
        bool hasPoint = false;
        float minX = 0.0f;
        float minY = 0.0f;
        float maxX = 0.0f;
        float maxY = 0.0f;

        auto addPoint = [&](const PointF& point) {
            const float x = point.getX();
            const float y = point.getY();
            if (!hasPoint) {
                minX = maxX = x;
                minY = maxY = y;
                hasPoint = true;
                return;
            }

            minX = std::min(minX, x);
            minY = std::min(minY, y);
            maxX = std::max(maxX, x);
            maxY = std::max(maxY, y);
        };

        auto finishContour = [&]() {
            if (hasPoint) {
                bounds.emplace_back(minX, minY, maxX - minX, maxY - minY);
            }
            hasPoint = false;
        };

        for (const auto& pathPoint : points_) {
            if (pathPoint.op == Op::MOVE_TO) {
                finishContour();
                addPoint(pathPoint.point);
            } else if (pathPoint.op == Op::LINE_TO) {
                addPoint(pathPoint.point);
            } else if (pathPoint.op == Op::CLOSE) {
                finishContour();
            }
        }
        finishContour();

        return bounds;
    }

    bool contains(float x, float y) const {
        std::vector<PointF> contour;
        bool contourClosed = false;
        int evenOddHits = 0;
        int winding = 0;

        auto pointInContour = [](const std::vector<PointF>& points, float px, float py) {
            bool inside = false;
            if (points.size() < 3) {
                return false;
            }

            for (size_t i = 0, j = points.size() - 1; i < points.size(); j = i++) {
                const float ax = points[i].getX();
                const float ay = points[i].getY();
                const float bx = points[j].getX();
                const float by = points[j].getY();
                const bool crosses = ((ay > py) != (by > py))
                    && (px < (bx - ax) * (py - ay) / ((by - ay) + 0.0001f) + ax);
                if (crosses) {
                    inside = !inside;
                }
            }

            return inside;
        };

        auto contourWinding = [](const std::vector<PointF>& points, float px, float py) {
            int windingNumber = 0;
            if (points.size() < 3) {
                return windingNumber;
            }

            auto isLeft = [](const PointF& a, const PointF& b, float px, float py) {
                return (b.getX() - a.getX()) * (py - a.getY()) - (px - a.getX()) * (b.getY() - a.getY());
            };

            for (size_t i = 0; i < points.size(); ++i) {
                const PointF& a = points[i];
                const PointF& b = points[(i + 1) % points.size()];
                if (a.getY() <= py) {
                    if (b.getY() > py && isLeft(a, b, px, py) > 0.0f) {
                        ++windingNumber;
                    }
                } else if (b.getY() <= py && isLeft(a, b, px, py) < 0.0f) {
                    --windingNumber;
                }
            }

            return windingNumber;
        };

        auto processContour = [&]() {
            if (!contourClosed || contour.size() < 3) {
                contour.clear();
                contourClosed = false;
                return;
            }

            if (fillType_ == FillType::EVEN_ODD) {
                if (pointInContour(contour, x, y)) {
                    ++evenOddHits;
                }
            } else {
                winding += contourWinding(contour, x, y);
            }

            contour.clear();
            contourClosed = false;
        };

        for (const auto& pathPoint : points_) {
            if (pathPoint.op == Op::MOVE_TO) {
                processContour();
                contour.push_back(pathPoint.point);
            } else if (pathPoint.op == Op::LINE_TO) {
                contour.push_back(pathPoint.point);
            } else if (pathPoint.op == Op::CLOSE) {
                contourClosed = true;
                processContour();
            }
        }
        processContour();

        if (fillType_ == FillType::EVEN_ODD) {
            return (evenOddHits % 2) != 0;
        }
        return winding != 0;
    }

    bool contains(const PointF& point) const {
        return contains(point.getX(), point.getY());
    }

    bool strokeContains(float x, float y, float strokeWidth) const {
        if (strokeWidth <= 0.0f || !std::isfinite(strokeWidth) || !std::isfinite(x) || !std::isfinite(y)) {
            return false;
        }

        const float halfWidth = strokeWidth * 0.5f;
        const float thresholdSq = halfWidth * halfWidth;
        PointF contourStart;
        PointF previous;
        bool hasContourStart = false;
        bool hasPrevious = false;

        auto distanceToSegmentSq = [](float px, float py, const PointF& a, const PointF& b) {
            const float ax = a.getX();
            const float ay = a.getY();
            const float bx = b.getX();
            const float by = b.getY();
            const float dx = bx - ax;
            const float dy = by - ay;
            const float lengthSq = dx * dx + dy * dy;
            if (lengthSq <= 0.000001f) {
                const float pointDx = px - ax;
                const float pointDy = py - ay;
                return pointDx * pointDx + pointDy * pointDy;
            }

            const float t = std::clamp(((px - ax) * dx + (py - ay) * dy) / lengthSq, 0.0f, 1.0f);
            const float nearestX = ax + dx * t;
            const float nearestY = ay + dy * t;
            const float pointDx = px - nearestX;
            const float pointDy = py - nearestY;
            return pointDx * pointDx + pointDy * pointDy;
        };

        for (const auto& pathPoint : points_) {
            if (pathPoint.op == Op::MOVE_TO) {
                contourStart = pathPoint.point;
                previous = pathPoint.point;
                hasContourStart = true;
                hasPrevious = true;
            } else if (pathPoint.op == Op::LINE_TO) {
                if (hasPrevious && distanceToSegmentSq(x, y, previous, pathPoint.point) <= thresholdSq) {
                    return true;
                }
                previous = pathPoint.point;
                hasPrevious = true;
            } else if (pathPoint.op == Op::CLOSE) {
                if (hasPrevious && hasContourStart && distanceToSegmentSq(x, y, previous, contourStart) <= thresholdSq) {
                    return true;
                }
                hasPrevious = false;
                hasContourStart = false;
            }
        }

        return false;
    }

    bool strokeContains(const PointF& point, float strokeWidth) const {
        return strokeContains(point.getX(), point.getY(), strokeWidth);
    }

    float length() const {
        float totalLength = 0.0f;
        PointF contourStart;
        PointF previous;
        bool hasContourStart = false;
        bool hasPrevious = false;

        auto addSegment = [&](const PointF& start, const PointF& end) {
            totalLength += distance(start, end);
        };

        for (const auto& pathPoint : points_) {
            if (pathPoint.op == Op::MOVE_TO) {
                contourStart = pathPoint.point;
                previous = pathPoint.point;
                hasContourStart = true;
                hasPrevious = true;
            } else if (pathPoint.op == Op::LINE_TO) {
                if (hasPrevious) {
                    addSegment(previous, pathPoint.point);
                }
                previous = pathPoint.point;
                hasPrevious = true;
            } else if (pathPoint.op == Op::CLOSE) {
                if (hasPrevious && hasContourStart) {
                    addSegment(previous, contourStart);
                }
                hasPrevious = false;
                hasContourStart = false;
            }
        }

        return totalLength;
    }

    bool pointAtLength(float targetLength, PointF& point) const {
        PointF tangent;
        return pointAndTangentAtLength(targetLength, point, tangent);
    }

    bool pointAndTangentAtLength(float targetLength, PointF& point, PointF& tangent) const {
        if (!std::isfinite(targetLength)) {
            return false;
        }

        const float clampedTarget = std::max(0.0f, targetLength);
        float accumulated = 0.0f;
        PointF contourStart;
        PointF previous;
        PointF lastPoint;
        PointF lastTangent;
        bool hasContourStart = false;
        bool hasPrevious = false;
        bool hasSegment = false;

        auto visitSegment = [&](const PointF& start, const PointF& end) {
            const float segmentLength = distance(start, end);
            if (segmentLength <= 0.0001f) {
                return false;
            }

            const float dx = end.getX() - start.getX();
            const float dy = end.getY() - start.getY();
            lastPoint = end;
            lastTangent = PointF(dx / segmentLength, dy / segmentLength);
            hasSegment = true;

            if (clampedTarget <= accumulated + segmentLength) {
                const float t = std::clamp((clampedTarget - accumulated) / segmentLength, 0.0f, 1.0f);
                point = PointF(start.getX() + dx * t, start.getY() + dy * t);
                tangent = lastTangent;
                return true;
            }

            accumulated += segmentLength;
            return false;
        };

        for (const auto& pathPoint : points_) {
            if (pathPoint.op == Op::MOVE_TO) {
                contourStart = pathPoint.point;
                previous = pathPoint.point;
                hasContourStart = true;
                hasPrevious = true;
            } else if (pathPoint.op == Op::LINE_TO) {
                if (hasPrevious && visitSegment(previous, pathPoint.point)) {
                    return true;
                }
                previous = pathPoint.point;
                hasPrevious = true;
            } else if (pathPoint.op == Op::CLOSE) {
                if (hasPrevious && hasContourStart && visitSegment(previous, contourStart)) {
                    return true;
                }
                hasPrevious = false;
                hasContourStart = false;
            }
        }

        if (!hasSegment) {
            return false;
        }

        point = lastPoint;
        tangent = lastTangent;
        return true;
    }

    Path reversed() const {
        struct Contour {
            std::vector<PointF> points;
            bool closed = false;
        };

        std::vector<Contour> contours;
        Contour current;

        auto finishContour = [&]() {
            if (current.points.empty()) {
                current = Contour();
                return;
            }

            if (current.closed && current.points.size() > 1) {
                const PointF& first = current.points.front();
                const PointF& last = current.points.back();
                if (distance(first, last) <= 0.0001f) {
                    current.points.pop_back();
                }
            }

            contours.push_back(current);
            current = Contour();
        };

        for (const auto& pathPoint : points_) {
            if (pathPoint.op == Op::MOVE_TO) {
                finishContour();
                current.points.push_back(pathPoint.point);
            } else if (pathPoint.op == Op::LINE_TO) {
                current.points.push_back(pathPoint.point);
            } else if (pathPoint.op == Op::CLOSE) {
                current.closed = true;
                finishContour();
            }
        }
        finishContour();

        Path result;
        result.setFillType(fillType_);
        for (size_t contourIndex = contours.size(); contourIndex > 0; --contourIndex) {
            const Contour& contour = contours[contourIndex - 1];
            if (contour.points.empty()) {
                continue;
            }

            result.moveTo(contour.points.back().getX(), contour.points.back().getY());
            for (size_t pointIndex = contour.points.size() - 1; pointIndex > 0; --pointIndex) {
                const PointF& point = contour.points[pointIndex - 1];
                result.lineTo(point.getX(), point.getY());
            }
            if (contour.closed) {
                result.close();
            }
        }

        return result;
    }

    Path trim(float startLength, float endLength) const {
        Path result;
        result.setFillType(fillType_);

        if (!std::isfinite(startLength) || !std::isfinite(endLength)) {
            return result;
        }

        const float startTarget = std::max(0.0f, std::min(startLength, endLength));
        const float endTarget = std::max(startTarget, std::max(startLength, endLength));
        if (endTarget <= startTarget) {
            return result;
        }

        float accumulated = 0.0f;
        PointF contourStart;
        PointF previous;
        PointF currentOutputPoint;
        bool hasContourStart = false;
        bool hasPrevious = false;
        bool hasOutputPoint = false;

        auto pointOnSegment = [](const PointF& start, const PointF& end, float t) {
            return PointF(start.getX() + (end.getX() - start.getX()) * t,
                          start.getY() + (end.getY() - start.getY()) * t);
        };

        auto appendTrimmedSegment = [&](const PointF& start, const PointF& end) {
            const float segmentLength = distance(start, end);
            if (segmentLength <= 0.0001f) {
                return false;
            }

            const float segmentStart = accumulated;
            const float segmentEnd = accumulated + segmentLength;
            if (segmentEnd <= startTarget) {
                accumulated = segmentEnd;
                return false;
            }
            if (segmentStart >= endTarget) {
                return true;
            }

            const float clippedStart = std::max(startTarget, segmentStart);
            const float clippedEnd = std::min(endTarget, segmentEnd);
            if (clippedEnd > clippedStart) {
                const float t0 = (clippedStart - segmentStart) / segmentLength;
                const float t1 = (clippedEnd - segmentStart) / segmentLength;
                const PointF p0 = pointOnSegment(start, end, t0);
                const PointF p1 = pointOnSegment(start, end, t1);
                if (!hasOutputPoint || distance(currentOutputPoint, p0) > 0.0001f) {
                    result.moveTo(p0.getX(), p0.getY());
                }
                result.lineTo(p1.getX(), p1.getY());
                currentOutputPoint = p1;
                hasOutputPoint = true;
            }

            accumulated = segmentEnd;
            return accumulated >= endTarget;
        };

        for (const auto& pathPoint : points_) {
            if (pathPoint.op == Op::MOVE_TO) {
                contourStart = pathPoint.point;
                previous = pathPoint.point;
                hasContourStart = true;
                hasPrevious = true;
            } else if (pathPoint.op == Op::LINE_TO) {
                if (hasPrevious && appendTrimmedSegment(previous, pathPoint.point)) {
                    break;
                }
                previous = pathPoint.point;
                hasPrevious = true;
            } else if (pathPoint.op == Op::CLOSE) {
                if (hasPrevious && hasContourStart && appendTrimmedSegment(previous, contourStart)) {
                    break;
                }
                hasPrevious = false;
                hasContourStart = false;
            }
        }

        return result;
    }

    Path trim(float startLength, float endLength, bool wrap) const {
        if (!wrap) {
            return trim(startLength, endLength);
        }

        Path result;
        result.setFillType(fillType_);
        if (!std::isfinite(startLength) || !std::isfinite(endLength)) {
            return result;
        }

        const float totalLength = length();
        const float span = endLength - startLength;
        if (totalLength <= 0.0001f || span <= 0.0001f) {
            return result;
        }
        if (span >= totalLength) {
            return trim(0.0f, totalLength);
        }

        float normalizedStart = std::fmod(startLength, totalLength);
        if (normalizedStart < 0.0f) {
            normalizedStart += totalLength;
        }

        auto appendPath = [&result](const Path& path) {
            for (const auto& pathPoint : path.points_) {
                if (pathPoint.op == Op::MOVE_TO) {
                    result.moveTo(pathPoint.point.getX(), pathPoint.point.getY());
                } else if (pathPoint.op == Op::LINE_TO) {
                    result.lineTo(pathPoint.point.getX(), pathPoint.point.getY());
                } else if (pathPoint.op == Op::CLOSE) {
                    result.close();
                }
            }
        };

        const float normalizedEnd = normalizedStart + span;
        if (normalizedEnd <= totalLength) {
            appendPath(trim(normalizedStart, normalizedEnd));
        } else {
            appendPath(trim(normalizedStart, totalLength));
            appendPath(trim(0.0f, normalizedEnd - totalLength));
        }

        return result;
    }

    Path trim(float startLength, float endLength, bool wrap, bool reverse) const {
        if (!reverse) {
            return trim(startLength, endLength, wrap);
        }

        Path result;
        result.setFillType(fillType_);
        if (!std::isfinite(startLength) || !std::isfinite(endLength)) {
            return result;
        }

        const float totalLength = length();
        if (totalLength <= 0.0001f) {
            return result;
        }

        const Path reversedPath = reversed();
        if (!wrap) {
            const float lower = std::clamp(std::min(startLength, endLength), 0.0f, totalLength);
            const float upper = std::clamp(std::max(startLength, endLength), 0.0f, totalLength);
            if (upper <= lower) {
                return result;
            }
            return reversedPath.trim(totalLength - upper, totalLength - lower);
        }

        const float span = std::abs(endLength - startLength);
        if (span <= 0.0001f) {
            return result;
        }
        if (span >= totalLength) {
            return reversedPath.trim(0.0f, totalLength);
        }

        float normalizedStart = std::fmod(startLength, totalLength);
        if (normalizedStart < 0.0f) {
            normalizedStart += totalLength;
        }

        auto appendPath = [&result](const Path& path) {
            for (const auto& pathPoint : path.points_) {
                if (pathPoint.op == Op::MOVE_TO) {
                    result.moveTo(pathPoint.point.getX(), pathPoint.point.getY());
                } else if (pathPoint.op == Op::LINE_TO) {
                    result.lineTo(pathPoint.point.getX(), pathPoint.point.getY());
                } else if (pathPoint.op == Op::CLOSE) {
                    result.close();
                }
            }
        };

        const float backwardEnd = normalizedStart - span;
        if (backwardEnd >= 0.0f) {
            appendPath(reversedPath.trim(totalLength - normalizedStart, totalLength - backwardEnd));
        } else {
            appendPath(reversedPath.trim(totalLength - normalizedStart, totalLength));
            appendPath(reversedPath.trim(0.0f, -backwardEnd));
        }

        return result;
    }

    Path roundedCorners(float radius) const {
        if (radius <= 0.0f || !std::isfinite(radius)) {
            return *this;
        }

        struct Contour {
            std::vector<PointF> points;
            bool closed = false;
        };

        std::vector<Contour> contours;
        Contour current;

        auto finishContour = [&]() {
            if (current.points.empty()) {
                current = Contour();
                return;
            }

            if (current.closed && current.points.size() > 1) {
                const PointF& first = current.points.front();
                const PointF& last = current.points.back();
                if (distance(first, last) <= 0.0001f) {
                    current.points.pop_back();
                }
            }

            contours.push_back(current);
            current = Contour();
        };

        for (const auto& pathPoint : points_) {
            if (pathPoint.op == Op::MOVE_TO) {
                finishContour();
                current.points.push_back(pathPoint.point);
            } else if (pathPoint.op == Op::LINE_TO) {
                current.points.push_back(pathPoint.point);
            } else if (pathPoint.op == Op::CLOSE) {
                current.closed = true;
                finishContour();
            }
        }
        finishContour();

        Path result;
        result.setFillType(fillType_);

        auto roundedCorner = [radius](const PointF& previous, const PointF& currentPoint,
                                      const PointF& next, PointF& start, PointF& end) {
            const float previousDx = previous.getX() - currentPoint.getX();
            const float previousDy = previous.getY() - currentPoint.getY();
            const float nextDx = next.getX() - currentPoint.getX();
            const float nextDy = next.getY() - currentPoint.getY();
            const float previousLength = std::hypot(previousDx, previousDy);
            const float nextLength = std::hypot(nextDx, nextDy);
            if (previousLength <= 0.0001f || nextLength <= 0.0001f) {
                return false;
            }

            const float cut = std::min(radius, std::min(previousLength, nextLength) * 0.5f);
            if (cut <= 0.0001f) {
                return false;
            }

            start = PointF(currentPoint.getX() + previousDx / previousLength * cut,
                           currentPoint.getY() + previousDy / previousLength * cut);
            end = PointF(currentPoint.getX() + nextDx / nextLength * cut,
                         currentPoint.getY() + nextDy / nextLength * cut);
            return true;
        };

        auto emitSimpleContour = [&result](const Contour& contour) {
            if (contour.points.empty()) {
                return;
            }

            result.moveTo(contour.points.front().getX(), contour.points.front().getY());
            for (size_t i = 1; i < contour.points.size(); ++i) {
                result.lineTo(contour.points[i].getX(), contour.points[i].getY());
            }
            if (contour.closed) {
                result.close();
            }
        };

        for (const Contour& contour : contours) {
            const size_t count = contour.points.size();
            if (count < 3) {
                emitSimpleContour(contour);
                continue;
            }

            if (!contour.closed) {
                result.moveTo(contour.points.front().getX(), contour.points.front().getY());
                for (size_t i = 1; i + 1 < count; ++i) {
                    PointF start;
                    PointF end;
                    if (roundedCorner(contour.points[i - 1], contour.points[i], contour.points[i + 1], start, end)) {
                        result.lineTo(start.getX(), start.getY());
                        result.quadTo(contour.points[i], end);
                    } else {
                        result.lineTo(contour.points[i].getX(), contour.points[i].getY());
                    }
                }
                result.lineTo(contour.points.back().getX(), contour.points.back().getY());
                continue;
            }

            bool hasStarted = false;
            for (size_t i = 0; i < count; ++i) {
                const PointF& previous = contour.points[(i + count - 1) % count];
                const PointF& currentPoint = contour.points[i];
                const PointF& next = contour.points[(i + 1) % count];
                PointF start;
                PointF end;
                if (roundedCorner(previous, currentPoint, next, start, end)) {
                    if (!hasStarted) {
                        result.moveTo(start.getX(), start.getY());
                        hasStarted = true;
                    } else {
                        result.lineTo(start.getX(), start.getY());
                    }
                    result.quadTo(currentPoint, end);
                } else {
                    if (!hasStarted) {
                        result.moveTo(currentPoint.getX(), currentPoint.getY());
                        hasStarted = true;
                    } else {
                        result.lineTo(currentPoint.getX(), currentPoint.getY());
                    }
                }
            }
            result.close();
        }

        return result;
    }

    // 移动整个路径
    void offset(float dx, float dy) {
        for (auto& point : points_) {
            if (point.op != Op::CLOSE) {
                point.point.setX(point.point.getX() + dx);
                point.point.setY(point.point.getY() + dy);
            }
        }

        if (hasCurrentPoint_) {
            currentPoint_.setX(currentPoint_.getX() + dx);
            currentPoint_.setY(currentPoint_.getY() + dy);
        }

        if (hasContourStart_) {
            contourStart_.setX(contourStart_.getX() + dx);
            contourStart_.setY(contourStart_.getY() + dy);
        }
    }

    // 添加矩形
    void addRect(const RectF& rect) {
        moveTo(rect.getX(), rect.getY());
        lineTo(rect.getX() + rect.getWidth(), rect.getY());
        lineTo(rect.getX() + rect.getWidth(), rect.getY() + rect.getHeight());
        lineTo(rect.getX(), rect.getY() + rect.getHeight());
        close();
    }

    void addOval(const RectF& rect) {
        RectF normalized = normalizeRect(rect);
        const float width = normalized.getWidth();
        const float height = normalized.getHeight();
        if (width <= 0.0f || height <= 0.0f) {
            return;
        }

        constexpr float pi = 3.14159265358979323846f;
        const float centerX = normalized.getX() + width * 0.5f;
        const float centerY = normalized.getY() + height * 0.5f;
        const float radiusX = width * 0.5f;
        const float radiusY = height * 0.5f;
        const int segments = computeEllipseSegments(radiusX, radiusY);
        if (segments <= 0) {
            return;
        }

        moveTo(centerX + radiusX, centerY);
        for (int i = 1; i <= segments; ++i) {
            const float angle = 2.0f * pi * static_cast<float>(i) / static_cast<float>(segments);
            lineTo(centerX + radiusX * std::cos(angle), centerY + radiusY * std::sin(angle));
        }
        close();
    }

    void addRoundRect(const RectF& rect, float radius) {
        addRoundRect(rect, radius, radius, radius, radius);
    }

    void addRoundRect(const RectF& rect, float topLeftRadius, float topRightRadius,
                      float bottomRightRadius, float bottomLeftRadius) {
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

        scaleRadiusPair(scale, width, topLeft, topRight);
        scaleRadiusPair(scale, width, bottomLeft, bottomRight);
        scaleRadiusPair(scale, height, topRight, bottomRight);
        scaleRadiusPair(scale, height, topLeft, bottomLeft);

        topLeft *= scale;
        topRight *= scale;
        bottomRight *= scale;
        bottomLeft *= scale;

        if (topLeft <= 0.0f && topRight <= 0.0f && bottomRight <= 0.0f && bottomLeft <= 0.0f) {
            addRect(normalized);
            return;
        }

        constexpr float pi = 3.14159265358979323846f;
        const float left = normalized.getX();
        const float top = normalized.getY();
        const float right = left + width;
        const float bottom = top + height;

        moveTo(left + topLeft, top);
        lineTo(right - topRight, top);
        appendQuarterArc(right - topRight, top + topRight, topRight, -0.5f * pi, 0.0f);
        lineTo(right, bottom - bottomRight);
        appendQuarterArc(right - bottomRight, bottom - bottomRight, bottomRight, 0.0f, 0.5f * pi);
        lineTo(left + bottomLeft, bottom);
        appendQuarterArc(left + bottomLeft, bottom - bottomLeft, bottomLeft, 0.5f * pi, pi);
        lineTo(left, top + topLeft);
        appendQuarterArc(left + topLeft, top + topLeft, topLeft, pi, 1.5f * pi);
        close();
    }

    // 添加圆形
    void addCircle(float x, float y, float radius) {
        constexpr float pi = 3.14159265358979323846f;
        // 根据半径计算合适的细分段数
        // 每4个像素对应一个细分段，最少12段，最多180段
        const float segmentsPerPixel = 0.25f;  // 每4个像素一段
        const int minSegments = 12;            // 最少段数
        const int maxSegments = 180;           // 最多段数
        
        int segments = static_cast<int>(2.0f * pi * radius * segmentsPerPixel);
        segments = std::max(minSegments, std::min(maxSegments, segments));
        
        const float angleStep = 2.0f * pi / segments;
        
        moveTo(x + radius, y);
        for (int i = 1; i <= segments; i++) {
            float angle = i * angleStep;
            lineTo(x + radius * std::cos(angle), y + radius * std::sin(angle));
        }
        close();
    }

private:
    static RectF normalizeRect(const RectF& rect) {
        const float left = std::min(rect.getX(), rect.getX() + rect.getWidth());
        const float top = std::min(rect.getY(), rect.getY() + rect.getHeight());
        const float right = std::max(rect.getX(), rect.getX() + rect.getWidth());
        const float bottom = std::max(rect.getY(), rect.getY() + rect.getHeight());
        return RectF(left, top, right - left, bottom - top);
    }

    static int computeEllipseSegments(float radiusX, float radiusY) {
        constexpr float pi = 3.14159265358979323846f;
        const float radius = std::max(radiusX, radiusY);
        const int estimated = static_cast<int>(2.0f * pi * radius * 0.25f);
        return std::clamp(estimated, 12, 180);
    }

    static void scaleRadiusPair(float& scale, float available, float a, float b) {
        const float sum = a + b;
        if (sum > available && sum > 0.0f) {
            scale = std::min(scale, available / sum);
        }
    }

    static int computeCornerSegments(float radius) {
        return std::clamp(static_cast<int>(std::ceil(radius * 0.35f)), 4, 24);
    }

    void appendQuarterArc(float centerX, float centerY, float radius, float startAngle, float endAngle) {
        if (radius <= 0.0f) {
            lineTo(centerX, centerY);
            return;
        }

        const int segments = computeCornerSegments(radius);
        for (int segment = 1; segment <= segments; ++segment) {
            const float progress = static_cast<float>(segment) / static_cast<float>(segments);
            const float angle = startAngle + (endAngle - startAngle) * progress;
            lineTo(centerX + std::cos(angle) * radius, centerY + std::sin(angle) * radius);
        }
    }

    static float distance(const PointF& start, const PointF& end) {
        return std::hypot(end.getX() - start.getX(), end.getY() - start.getY());
    }

    static int computeCurveSegments(float controlPolygonLength) {
        constexpr float pixelsPerSegment = 10.0f;
        constexpr int minSegments = 8;
        constexpr int maxSegments = 96;
        if (controlPolygonLength <= 0.0f || !std::isfinite(controlPolygonLength)) {
            return minSegments;
        }

        const int estimated = static_cast<int>(std::ceil(controlPolygonLength / pixelsPerSegment));
        return std::clamp(estimated, minSegments, maxSegments);
    }

    std::vector<PathPoint> points_;
    FillType fillType_ = FillType::WINDING;
    PointF currentPoint_;
    PointF contourStart_;
    bool hasCurrentPoint_ = false;
    bool hasContourStart_ = false;
};


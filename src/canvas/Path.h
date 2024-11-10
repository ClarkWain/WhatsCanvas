#pragma once

#include <vector>
#include "base.h"

class Path {
public:
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

    // 移动到指定点
    void moveTo(float x, float y) {
        points_.emplace_back(Op::MOVE_TO, PointF(x, y));
    }

    // 从当前点连线到指定点
    void lineTo(float x, float y) {
        points_.emplace_back(Op::LINE_TO, PointF(x, y));
    }

    // 闭合路径（连接到起始点）
    void close() {
        points_.emplace_back(Op::CLOSE, PointF());
    }

    // 重置路径
    void reset() {
        points_.clear();
    }

    // 获取路径点集合
    const std::vector<PathPoint>& getPoints() const {
        return points_;
    }

    // 检查路径是否为空
    bool isEmpty() const {
        return points_.empty();
    }

    // 移动整个路径
    void offset(float dx, float dy) {
        for (auto& point : points_) {
            if (point.op != Op::CLOSE) {
                point.point.setX(point.point.getX() + dx);
                point.point.setY(point.point.getY() + dy);
            }
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

    // 添加圆形
    void addCircle(float x, float y, float radius) {
        // 根据半径计算合适的细分段数
        // 每4个像素对应一个细分段，最少12段，最多180段
        const float segmentsPerPixel = 0.25f;  // 每4个像素一段
        const int minSegments = 12;            // 最少段数
        const int maxSegments = 180;           // 最多段数
        
        int segments = static_cast<int>(2 * M_PI * radius * segmentsPerPixel);
        segments = std::max(minSegments, std::min(maxSegments, segments));
        
        const float angleStep = 2.0f * M_PI / segments;
        
        moveTo(x + radius, y);
        for (int i = 1; i <= segments; i++) {
            float angle = i * angleStep;
            lineTo(x + radius * cos(angle), y + radius * sin(angle));
        }
        close();
    }

private:
    std::vector<PathPoint> points_;
};


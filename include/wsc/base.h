#pragma once

#include "Export.h"

namespace wsc {

class Size;
class SizeF;
class Rect;
class RectF;

/// Integer point in 2D space.
class WSC_API Point
{
public:
    Point(int x = 0, int y = 0);

    int getX() const;
    void setX(int x);
    int getY() const;
    void setY(int y);
    void set(int x, int y);

    bool operator==(const Point &other) const;
    bool operator!=(const Point &other) const;
    Point operator+(const Point &other) const;
    Point operator+(int scalar) const;
    Point operator-(const Point &other) const;
    Point operator-(int scalar) const;
    Point &operator+=(const Point &other);
    Point &operator+=(int scalar);
    Point &operator-=(const Point &other);
    Point &operator-=(int scalar);
    Point operator*(int scalar) const;

private:
    int x;
    int y;
};

/// Floating-point point in 2D space.
class WSC_API PointF
{
public:
    PointF(float x = 0.0f, float y = 0.0f);

    float getX() const;
    void setX(float x);
    float getY() const;
    void setY(float y);
    void set(float x, float y);

    bool operator==(const PointF &other) const;
    bool operator!=(const PointF &other) const;
    PointF operator+(const PointF &other) const;
    PointF operator+(float scalar) const;
    PointF operator-(const PointF &other) const;
    PointF operator-(float scalar) const;
    PointF &operator+=(const PointF &other);
    PointF &operator+=(float scalar);
    PointF &operator-=(const PointF &other);
    PointF &operator-=(float scalar);
    PointF operator*(float scalar) const;
    PointF operator/(float scalar) const;

private:
    float x;
    float y;
};

/// Integer width and height pair.
class WSC_API Size
{
public:
    Size(int width = 0, int height = 0);

    int getWidth() const;
    void setWidth(int width);
    int getHeight() const;
    void setHeight(int height);
    void set(int width, int height);

    bool operator==(const Size &other) const;
    bool operator!=(const Size &other) const;

private:
    int width;
    int height;
};

/// Floating-point width and height pair.
class WSC_API SizeF
{
public:
    SizeF(float width = 0.0f, float height = 0.0f);

    float getWidth() const;
    void setWidth(float width);
    float getHeight() const;
    void setHeight(float height);
    void set(float width, float height);

    bool operator==(const SizeF &other) const;
    bool operator!=(const SizeF &other) const;

private:
    float width;
    float height;
};

/// Floating-point rectangle in 2D space.
class WSC_API RectF
{
public:
    RectF(float x = 0.0f, float y = 0.0f, float width = 0.0f, float height = 0.0f);
    RectF(const PointF &leftTop, const PointF &bottomRight);

    float getX() const;
    void setX(float x);
    float getY() const;
    void setY(float y);
    PointF getLeftTop() const;
    PointF getBottomRight() const;
    float getWidth() const;
    void setWidth(float width);
    float getHeight() const;
    void setHeight(float height);
    SizeF getSize() const;
    PointF getCenter() const;
    void setCenter(const PointF &center);
    void setCenter(float cx, float cy);
    bool contains(float px, float py) const;
    bool contains(const PointF &point) const;
    void transform(float dx, float dy);
    void scale(float sx, float sy);
    float getArea() const;
    float getPerimeter() const;
    bool intersects(const RectF &other) const;
    bool isSquare() const;
    RectF getIntersection(const RectF &other) const;

private:
    float x;
    float y;
    float width;
    float height;
};

/// Integer rectangle in 2D space.
class WSC_API Rect
{
public:
    Rect(int x = 0, int y = 0, int width = 0, int height = 0);
    Rect(const Point &leftTop, const Point &bottomRight);

    int getX() const;
    void setX(int x);
    int getY() const;
    void setY(int y);
    Point getLeftTop() const;
    Point getBottomRight() const;
    int getWidth() const;
    void setWidth(int width);
    int getHeight() const;
    void setHeight(int height);
    Size getSize() const;
    PointF getCenter() const;
    void setCenter(const Point &center);
    void setCenter(int cx, int cy);
    bool contains(int px, int py) const;
    bool contains(const Point &point) const;
    void transform(int dx, int dy);
    void scale(float sx, float sy);
    int getArea() const;
    int getPerimeter() const;
    bool intersects(const Rect &other) const;
    bool isSquare() const;
    Rect getIntersection(const Rect &other) const;

private:
    int x;
    int y;
    int width;
    int height;
};

} // namespace wsc

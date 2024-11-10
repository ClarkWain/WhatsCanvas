#pragma once

#include <algorithm>

class Size;
class SizeF;
class Rect;
class RectF;

class Point
{
private:
    int x;
    int y;

public:
    Point(int x = 0, int y = 0) : x(x), y(y) {}

    int getX() const
    {
        return x;
    }

    void setX(int x)
    {
        this->x = x;
    }

    int getY() const
    {
        return y;
    }

    void setY(int y)
    {
        this->y = y;
    }

    void set(int x, int y)
    {
        this->x = x;
        this->y = y;
    }

    bool operator==(const Point &other) const
    {
        return x == other.x && y == other.y;
    }

    bool operator!=(const Point &other) const
    {
        return !(*this == other);
    }

    Point operator+(const Point &other) const
    {
        return Point(x + other.x, y + other.y);
    }

    Point operator+(int scalar) const
    {
        return Point(x + scalar, y + scalar);
    }

    Point operator-(const Point &other) const
    {
        return Point(x - other.x, y - other.y);
    }

    Point operator-(int scalar) const
    {
        return Point(x - scalar, y - scalar);
    }

    Point &operator+=(const Point &other)
    {
        x += other.x;
        y += other.y;
        return *this;
    }

    Point &operator+=(int scalar)
    {
        x += scalar;
        y += scalar;
        return *this;
    }

    Point &operator-=(const Point &other)
    {
        x -= other.x;
        y -= other.y;
        return *this;
    }

    Point &operator-=(int scalar)
    {
        x -= scalar;
        y -= scalar;
        return *this;
    }

    Point operator*(int scalar) const
    {
        return Point(x * scalar, y * scalar);
    }
};

class PointF
{
private:
    float x;
    float y;

public:
    PointF(float x = 0.0f, float y = 0.0f) : x(x), y(y) {}

    float getX() const
    {
        return x;
    }

    void setX(float x)
    {
        this->x = x;
    }

    float getY() const
    {
        return y;
    }

    void setY(float y)
    {
        this->y = y;
    }

    void set(float x, float y)
    {
        this->x = x;
        this->y = y;
    }

    bool operator==(const PointF &other) const
    {
        return x == other.x && y == other.y;
    }

    bool operator!=(const PointF &other) const
    {
        return !(*this == other);
    }

    PointF operator+(const PointF &other) const
    {
        return PointF(x + other.x, y + other.y);
    }

    PointF operator+(float scalar) const
    {
        return PointF(x + scalar, y + scalar);
    }

    PointF operator-(const PointF &other) const
    {
        return PointF(x - other.x, y - other.y);
    }

    PointF operator-(float scalar) const
    {
        return PointF(x - scalar, y - scalar);
    }

    PointF &operator+=(const PointF &other)
    {
        x += other.x;
        y += other.y;
        return *this;
    }

    PointF &operator+=(float scalar)
    {
        x += scalar;
        y += scalar;
        return *this;
    }

    PointF &operator-=(const PointF &other)
    {
        x -= other.x;
        y -= other.y;
        return *this;
    }

    PointF &operator-=(float scalar)
    {
        x -= scalar;
        y -= scalar;
        return *this;
    }

    PointF operator*(float scalar) const
    {
        return PointF(x * scalar, y * scalar);
    }

    PointF operator/(float scalar) const
    {
        return PointF(x / scalar, y / scalar);
    }
};

class Size
{
private:
    int width;
    int height;

public:
    Size(int width = 0, int height = 0) : width(width), height(height) {}

    int getWidth() const
    {
        return width;
    }

    void setWidth(int width)
    {
        this->width = width;
    }

    int getHeight() const
    {
        return height;
    }

    void setHeight(int height)
    {
        this->height = height;
    }

    void set(int width, int height)
    {
        this->width = width;
        this->height = height;
    }

    bool operator==(const Size &other) const
    {
        return width == other.width && height == other.height;
    }

    bool operator!=(const Size &other) const
    {
        return !(*this == other);
    }
};

class SizeF
{
private:
    float width;
    float height;

public:
    SizeF(float width = 0.0f, float height = 0.0f) : width(width), height(height) {}

    float getWidth() const
    {
        return width;
    }

    void setWidth(float width)
    {
        this->width = width;
    }

    float getHeight() const
    {
        return height;
    }

    void setHeight(float height)
    {
        this->height = height;
    }

    void set(float width, float height)
    {
        this->width = width;
        this->height = height;
    }

    bool operator==(const SizeF &other) const
    {
        return width == other.width && height == other.height;
    }

    bool operator!=(const SizeF &other) const
    {
        return !(*this == other);
    }
};


class RectF
{
private:
    float x;
    float y;
    float width;
    float height;

public:
    RectF(float x = 0.0f, float y = 0.0f, float width = 0.0f, float height = 0.0f)
        : x(x), y(y), width(width), height(height) {}

    RectF(const PointF &leftTop, const PointF &bottomRight)
        : x(leftTop.getX()), y(leftTop.getY()), width(bottomRight.getX() - leftTop.getX()), height(bottomRight.getY() - leftTop.getY()) {}

    float getX() const
    {
        return x;
    }

    void setX(float x)
    {
        this->x = x;
    }

    float getY() const
    {
        return y;
    }

    void setY(float y)
    {
        this->y = y;
    }

    PointF getLeftTop() const
    {
        return PointF(x, y);
    }

    PointF getBottomRight() const
    {
        return PointF(x + width, y + height);
    }

    float getWidth() const
    {
        return width;
    }

    void setWidth(float width)
    {
        this->width = width;
    }

    float getHeight() const
    {
        return height;
    }

    void setHeight(float height)
    {
        this->height = height;
    }

    SizeF getSize() const
    {
        return SizeF(width, height);
    }

    PointF getCenter() const
    {
        return PointF(x + width / 2.0f, y + height / 2.0f);
    }

    void setCenter(const PointF &center)
    {
        x = center.getX() - width / 2;
        y = center.getY() - height / 2;
    }

    void setCenter(float cx, float cy)
    {
        x = cx - width / 2;
        y = cy - height / 2;
    }

    bool contains(float px, float py) const
    {
        return px >= x && px <= x + width && py >= y && py <= y + height;
    }

    bool contains(const PointF &point) const
    {
        return contains(point.getX(), point.getY());
    }

    void transform(float dx, float dy)
    {
        x += dx;
        y += dy;
    }

    void scale(float sx, float sy)
    {
        width *= sx;
        height *= sy;
    }

    float getArea() const
    {
        return width * height;
    }

    float getPerimeter() const
    {
        return 2 * (width + height);
    }

    bool intersects(const RectF &other) const
    {
        return !(x > other.x + other.width ||
                 x + width < other.x ||
                 y > other.y + other.height ||
                 y + height < other.y);
    }

    bool isSquare() const
    {
        return width == height;
    }

    RectF getIntersection(const RectF &other) const
    {
        float newX = std::max(x, other.x);
        float newY = std::max(y, other.y);
        float newWidth = std::min(x + width, other.x + other.width) - newX;
        float newHeight = std::min(y + height, other.y + other.height) - newY;

        if (newWidth > 0 && newHeight > 0)
        {
            return RectF(newX, newY, newWidth, newHeight);
        }
        else
        {
            return RectF(); // Return an empty rectangle
        }
    }
};


class Rect
{
private:
    int x;
    int y;
    int width;
    int height;

public:
    Rect(int x = 0, int y = 0, int width = 0, int height = 0)
        : x(x), y(y), width(width), height(height) {}

    Rect(const Point &leftTop, const Point &bottomRight)
        : x(leftTop.getX()), y(leftTop.getY()), width(bottomRight.getX() - leftTop.getX()), height(bottomRight.getY() - leftTop.getY()) {}

    int getX() const
    {
        return x;
    }

    void setX(int x)
    {
        this->x = x;
    }

    int getY() const
    {
        return y;
    }

    void setY(int y)
    {
        this->y = y;
    }

    Point getLeftTop() const
    {
        return Point(x, y);
    }

    Point getBottomRight() const
    {
        return Point(x + width, y + height);
    }

    int getWidth() const
    {
        return width;
    }

    void setWidth(int width)
    {
        this->width = width;
    }

    int getHeight() const
    {
        return height;
    }

    void setHeight(int height)
    {
        this->height = height;
    }

    Size getSize() const
    {
        return Size(width, height);
    }

    PointF getCenter() const
    {
        return PointF(x + width / 2.0f, y + height / 2.0f);
    }

    void setCenter(const Point &center)
    {
        x = center.getX() - width / 2;
        y = center.getY() - height / 2;
    }

    void setCenter(int cx, int cy)
    {
        x = cx - width / 2;
        y = cy - height / 2;
    }

    bool contains(int px, int py) const
    {
        return px >= x && px <= x + width && py >= y && py <= y + height;
    }

    bool contains(const Point &point) const
    {
        return contains(point.getX(), point.getY());
    }

    void transform(int dx, int dy)
    {
        x += dx;
        y += dy;
    }

    void scale(float sx, float sy)
    {
        width = static_cast<int>(width * sx);
        height = static_cast<int>(height * sy);
    }

    int getArea() const
    {
        return width * height;
    }

    int getPerimeter() const
    {
        return 2 * (width + height);
    }

    bool intersects(const Rect &other) const
    {
        return !(x > other.x + other.width ||
                 x + width < other.x ||
                 y > other.y + other.height ||
                 y + height < other.y);
    }

    bool isSquare() const
    {
        return width == height;
    }

    Rect getIntersection(const Rect &other) const
    {
        int newX = std::max(x, other.x);
        int newY = std::max(y, other.y);
        int newWidth = std::min(x + width, other.x + other.width) - newX;
        int newHeight = std::min(y + height, other.y + other.height) - newY;

        if (newWidth > 0 && newHeight > 0)
        {
            return Rect(newX, newY, newWidth, newHeight);
        }
        else
        {
            return Rect(); // Return an empty rectangle
        }
    }
};
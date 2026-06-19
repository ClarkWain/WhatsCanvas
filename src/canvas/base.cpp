#include "base.h"

#include <algorithm>

namespace wsc {

Point::Point(int x, int y) : x(x), y(y) {}
int Point::getX() const { return x; }
void Point::setX(int value) { x = value; }
int Point::getY() const { return y; }
void Point::setY(int value) { y = value; }
void Point::set(int xValue, int yValue) { x = xValue; y = yValue; }
bool Point::operator==(const Point &other) const { return x == other.x && y == other.y; }
bool Point::operator!=(const Point &other) const { return !(*this == other); }
Point Point::operator+(const Point &other) const { return Point(x + other.x, y + other.y); }
Point Point::operator+(int scalar) const { return Point(x + scalar, y + scalar); }
Point Point::operator-(const Point &other) const { return Point(x - other.x, y - other.y); }
Point Point::operator-(int scalar) const { return Point(x - scalar, y - scalar); }
Point &Point::operator+=(const Point &other) { x += other.x; y += other.y; return *this; }
Point &Point::operator+=(int scalar) { x += scalar; y += scalar; return *this; }
Point &Point::operator-=(const Point &other) { x -= other.x; y -= other.y; return *this; }
Point &Point::operator-=(int scalar) { x -= scalar; y -= scalar; return *this; }
Point Point::operator*(int scalar) const { return Point(x * scalar, y * scalar); }

PointF::PointF(float x, float y) : x(x), y(y) {}
float PointF::getX() const { return x; }
void PointF::setX(float value) { x = value; }
float PointF::getY() const { return y; }
void PointF::setY(float value) { y = value; }
void PointF::set(float xValue, float yValue) { x = xValue; y = yValue; }
bool PointF::operator==(const PointF &other) const { return x == other.x && y == other.y; }
bool PointF::operator!=(const PointF &other) const { return !(*this == other); }
PointF PointF::operator+(const PointF &other) const { return PointF(x + other.x, y + other.y); }
PointF PointF::operator+(float scalar) const { return PointF(x + scalar, y + scalar); }
PointF PointF::operator-(const PointF &other) const { return PointF(x - other.x, y - other.y); }
PointF PointF::operator-(float scalar) const { return PointF(x - scalar, y - scalar); }
PointF &PointF::operator+=(const PointF &other) { x += other.x; y += other.y; return *this; }
PointF &PointF::operator+=(float scalar) { x += scalar; y += scalar; return *this; }
PointF &PointF::operator-=(const PointF &other) { x -= other.x; y -= other.y; return *this; }
PointF &PointF::operator-=(float scalar) { x -= scalar; y -= scalar; return *this; }
PointF PointF::operator*(float scalar) const { return PointF(x * scalar, y * scalar); }
PointF PointF::operator/(float scalar) const { return PointF(x / scalar, y / scalar); }

Size::Size(int width, int height) : width(width), height(height) {}
int Size::getWidth() const { return width; }
void Size::setWidth(int value) { width = value; }
int Size::getHeight() const { return height; }
void Size::setHeight(int value) { height = value; }
void Size::set(int widthValue, int heightValue) { width = widthValue; height = heightValue; }
bool Size::operator==(const Size &other) const { return width == other.width && height == other.height; }
bool Size::operator!=(const Size &other) const { return !(*this == other); }

SizeF::SizeF(float width, float height) : width(width), height(height) {}
float SizeF::getWidth() const { return width; }
void SizeF::setWidth(float value) { width = value; }
float SizeF::getHeight() const { return height; }
void SizeF::setHeight(float value) { height = value; }
void SizeF::set(float widthValue, float heightValue) { width = widthValue; height = heightValue; }
bool SizeF::operator==(const SizeF &other) const { return width == other.width && height == other.height; }
bool SizeF::operator!=(const SizeF &other) const { return !(*this == other); }

RectF::RectF(float x, float y, float width, float height)
    : x(x), y(y), width(width), height(height) {}

RectF::RectF(const PointF &leftTop, const PointF &bottomRight)
    : x(leftTop.getX()), y(leftTop.getY()),
      width(bottomRight.getX() - leftTop.getX()),
      height(bottomRight.getY() - leftTop.getY()) {}

float RectF::getX() const { return x; }
void RectF::setX(float value) { x = value; }
float RectF::getY() const { return y; }
void RectF::setY(float value) { y = value; }
PointF RectF::getLeftTop() const { return PointF(x, y); }
PointF RectF::getBottomRight() const { return PointF(x + width, y + height); }
float RectF::getWidth() const { return width; }
void RectF::setWidth(float value) { width = value; }
float RectF::getHeight() const { return height; }
void RectF::setHeight(float value) { height = value; }
SizeF RectF::getSize() const { return SizeF(width, height); }
PointF RectF::getCenter() const { return PointF(x + width / 2.0f, y + height / 2.0f); }
void RectF::setCenter(const PointF &center) { x = center.getX() - width / 2.0f; y = center.getY() - height / 2.0f; }
void RectF::setCenter(float cx, float cy) { x = cx - width / 2.0f; y = cy - height / 2.0f; }
bool RectF::contains(float px, float py) const { return px >= x && px <= x + width && py >= y && py <= y + height; }
bool RectF::contains(const PointF &point) const { return contains(point.getX(), point.getY()); }
void RectF::transform(float dx, float dy) { x += dx; y += dy; }
void RectF::scale(float sx, float sy) { width *= sx; height *= sy; }
float RectF::getArea() const { return width * height; }
float RectF::getPerimeter() const { return 2.0f * (width + height); }
bool RectF::intersects(const RectF &other) const
{
    return !(x > other.x + other.width || x + width < other.x ||
             y > other.y + other.height || y + height < other.y);
}
bool RectF::isSquare() const { return width == height; }
RectF RectF::getIntersection(const RectF &other) const
{
    const float newX = std::max(x, other.x);
    const float newY = std::max(y, other.y);
    const float newWidth = std::min(x + width, other.x + other.width) - newX;
    const float newHeight = std::min(y + height, other.y + other.height) - newY;
    return newWidth > 0.0f && newHeight > 0.0f ? RectF(newX, newY, newWidth, newHeight) : RectF();
}

Rect::Rect(int x, int y, int width, int height)
    : x(x), y(y), width(width), height(height) {}

Rect::Rect(const Point &leftTop, const Point &bottomRight)
    : x(leftTop.getX()), y(leftTop.getY()),
      width(bottomRight.getX() - leftTop.getX()),
      height(bottomRight.getY() - leftTop.getY()) {}

int Rect::getX() const { return x; }
void Rect::setX(int value) { x = value; }
int Rect::getY() const { return y; }
void Rect::setY(int value) { y = value; }
Point Rect::getLeftTop() const { return Point(x, y); }
Point Rect::getBottomRight() const { return Point(x + width, y + height); }
int Rect::getWidth() const { return width; }
void Rect::setWidth(int value) { width = value; }
int Rect::getHeight() const { return height; }
void Rect::setHeight(int value) { height = value; }
Size Rect::getSize() const { return Size(width, height); }
PointF Rect::getCenter() const { return PointF(x + width / 2.0f, y + height / 2.0f); }
void Rect::setCenter(const Point &center) { x = center.getX() - width / 2; y = center.getY() - height / 2; }
void Rect::setCenter(int cx, int cy) { x = cx - width / 2; y = cy - height / 2; }
bool Rect::contains(int px, int py) const { return px >= x && px <= x + width && py >= y && py <= y + height; }
bool Rect::contains(const Point &point) const { return contains(point.getX(), point.getY()); }
void Rect::transform(int dx, int dy) { x += dx; y += dy; }
void Rect::scale(float sx, float sy) { width = static_cast<int>(width * sx); height = static_cast<int>(height * sy); }
int Rect::getArea() const { return width * height; }
int Rect::getPerimeter() const { return 2 * (width + height); }
bool Rect::intersects(const Rect &other) const
{
    return !(x > other.x + other.width || x + width < other.x ||
             y > other.y + other.height || y + height < other.y);
}
bool Rect::isSquare() const { return width == height; }
Rect Rect::getIntersection(const Rect &other) const
{
    const int newX = std::max(x, other.x);
    const int newY = std::max(y, other.y);
    const int newWidth = std::min(x + width, other.x + other.width) - newX;
    const int newHeight = std::min(y + height, other.y + other.height) - newY;
    return newWidth > 0 && newHeight > 0 ? Rect(newX, newY, newWidth, newHeight) : Rect();
}

} // namespace wsc

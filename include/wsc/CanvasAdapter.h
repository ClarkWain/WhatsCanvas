#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include "Canvas.h"
#include "Color.h"
#include "Image.h"
#include "Paint.h"
#include "Path.h"
#include "base.h"

namespace wsc {

class CanvasAdapter
{
public:
    explicit CanvasAdapter(Canvas &canvas)
        : canvas_(&canvas)
    {
        fillPaint_.setStyle(Paint::Style::FILL);
        strokePaint_.setStyle(Paint::Style::STROKE);
    }

    Canvas &canvas() { return *canvas_; }
    const Canvas &canvas() const { return *canvas_; }

    Paint &fillPaint() { return fillPaint_; }
    const Paint &fillPaint() const { return fillPaint_; }
    Paint &strokePaint() { return strokePaint_; }
    const Paint &strokePaint() const { return strokePaint_; }
    Path &currentPath() { return path_; }
    const Path &currentPath() const { return path_; }

    void setFillColor(const Color &color) { fillPaint_.setColor(color); }
    void setStrokeColor(const Color &color) { strokePaint_.setStrokeColor(color); }
    void setStrokeWidth(float width) { strokePaint_.setStrokeWidth(width); }
    void setAlpha(float alpha)
    {
        fillPaint_.setAlpha(alpha);
        strokePaint_.setAlpha(alpha);
    }
    void setBlendMode(Paint::BlendMode blendMode)
    {
        fillPaint_.setBlendMode(blendMode);
        strokePaint_.setBlendMode(blendMode);
    }
    void setFont(const std::string &family)
    {
        fillPaint_.setFont(family);
        strokePaint_.setFont(family);
    }
    void setTextSize(float size)
    {
        fillPaint_.setTextSize(size);
        strokePaint_.setTextSize(size);
    }

    void beginPath() { path_.reset(); }
    void moveTo(float x, float y) { path_.moveTo(x, y); }
    void lineTo(float x, float y) { path_.lineTo(x, y); }
    void quadTo(float controlX, float controlY, float endX, float endY)
    {
        path_.quadTo(controlX, controlY, endX, endY);
    }
    void cubicTo(float control1X, float control1Y, float control2X, float control2Y, float endX, float endY)
    {
        path_.cubicTo(control1X, control1Y, control2X, control2Y, endX, endY);
    }
    void closePath() { path_.close(); }
    void fill() { canvas_->drawPath(path_, fillPaint_); }
    void stroke() { canvas_->drawPath(path_, strokePaint_); }

    void fillRect(const RectF &rect) { canvas_->drawRect(rect, fillPaint_); }
    void strokeRect(const RectF &rect) { canvas_->drawRect(rect, strokePaint_); }
    void fillRect(float x, float y, float width, float height) { fillRect(RectF(x, y, width, height)); }
    void strokeRect(float x, float y, float width, float height) { strokeRect(RectF(x, y, width, height)); }
    void fillCircle(const PointF &center, float radius) { canvas_->drawCircle(center, radius, fillPaint_); }
    void strokeCircle(const PointF &center, float radius) { canvas_->drawCircle(center, radius, strokePaint_); }
    void drawText(const std::string &text, float x, float y) { canvas_->drawText(text, x, y, fillPaint_); }

    std::uint32_t registerImage(Image &image)
    {
        const std::uint32_t handle = nextImageHandle_++;
        images_[handle] = &image;
        return handle;
    }

    bool unregisterImage(std::uint32_t handle)
    {
        return images_.erase(handle) > 0;
    }

    void clearImages()
    {
        images_.clear();
    }

    Image *image(std::uint32_t handle) const
    {
        const auto it = images_.find(handle);
        return it == images_.end() ? nullptr : it->second;
    }

    bool drawImage(std::uint32_t handle, const RectF &dst, const Paint *paint = nullptr)
    {
        Image *resolved = image(handle);
        if (resolved == nullptr) {
            return false;
        }
        canvas_->drawImage(*resolved, dst, paint == nullptr ? fillPaint_ : *paint);
        return true;
    }

private:
    Canvas *canvas_ = nullptr;
    Paint fillPaint_;
    Paint strokePaint_;
    Path path_;
    std::unordered_map<std::uint32_t, Image *> images_;
    std::uint32_t nextImageHandle_ = 1;
};

} // namespace wsc

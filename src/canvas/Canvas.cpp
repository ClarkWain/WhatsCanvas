#include <string>
#include <iostream>
#include <glm.hpp>
#include <memory>

#include "Canvas.h"
#include "Paint.h"
#include "command/DrawData.h"
#include "command/DrawCommand.h"

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

void Canvas::drawPoint(int x, int y, const Paint &paint)
{
    Color color = paint.getColor();
    std::vector<float> pts = {static_cast<float>(x), static_cast<float>(y)};
    DrawPointsData data = {pts, paint.getStrokeWidth(), {color.r(), color.g(), color.b(), color.a()}};
    renderer_.submit(std::make_unique<DrawPointsCommand>(data));
}

void Canvas::drawPoint(const Point &point, const Paint &paint)
{
    drawPoint(point.getX(), point.getY(), paint);
}

void Canvas::drawPoints(const std::vector<Point> &points, const Paint &paint) {
    Color color = paint.getColor();
    std::vector<float> pts;
    pts.reserve(points.size() * 2);
    for (const auto &point : points) {
        pts.push_back(static_cast<float>(point.getX()));
        pts.push_back(static_cast<float>(point.getY()));
    }
    DrawPointsData data = {pts, paint.getStrokeWidth(), {color.r(), color.g(), color.b(), color.a()}};
    renderer_.submit(std::make_unique<DrawPointsCommand>(data));
}


void Canvas::beginFrame()
{
    renderer_.clear();
}

void Canvas::endFrame()
{
    renderer_.flush();
}

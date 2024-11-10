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

void Canvas::drawLine(int x1, int y1, int x2, int y2, const Paint &paint)
{
    Color color = paint.getColor();
    std::vector<float> pts = {static_cast<float>(x1), static_cast<float>(y1), static_cast<float>(x2), static_cast<float>(y2)};
    DrawLinesData data = {pts, paint.getStrokeWidth(), {color.r(), color.g(), color.b(), color.a()}};
    renderer_.submit(std::make_unique<DrawLinesCommand>(data));
}

void Canvas::drawLine(const Point &start, const Point &end, const Paint &paint)
{
    drawLine(start.getX(), start.getY(), end.getX(), end.getY(), paint);
}

void Canvas::drawLines(const std::vector<Point> &points, const Paint &paint)
{
    Color color = paint.getColor();
    std::vector<float> pts;
    pts.reserve(points.size() * 4);
    for (const auto &point : points) {
        pts.push_back(static_cast<float>(point.getX()));
        pts.push_back(static_cast<float>(point.getY()));
    }
    DrawLinesData data = {pts, paint.getStrokeWidth(), {color.r(), color.g(), color.b(), color.a()}};
    renderer_.submit(std::make_unique<DrawLinesCommand>(data));
}


void Canvas::beginFrame()
{
    renderer_.clear();
}

void Canvas::endFrame()
{
    renderer_.flush();
}

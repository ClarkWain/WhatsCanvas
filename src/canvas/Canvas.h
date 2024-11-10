#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <vector>
#include <memory>
#include "Image.h"
#include "Paint.h"
#include "render/Renderer.h"
#include "base.h"

class Canvas
{
public:
    static void initialize();
    static void finalize();

public:
    void setSize(int width, int height);
    void setColor(Color color) { color_ = color; }

    void drawPoint(int x, int y, const Paint &paint);
    void drawPoint(const Point &point, const Paint &paint);
    void drawPoints(const std::vector<Point> &points, const Paint &paint);

    void beginFrame();
    void endFrame();

private:
    int width_ = 0;
    int height_ = 0;
    Color color_;
    Renderer renderer_;
};

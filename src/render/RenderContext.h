#pragma once

class RenderContext
{
public:
    RenderContext() = default;
    void setSize(int width, int height)
    {
        this->width = width;
        this->height = height;
        centerX = width / 2.0f;
        centerY = height / 2.0f;
    }

    int getWidth() const { return width; }
    int getHeight() const { return height; }

    void setScissorOffset(int x, int y)
    {
        scissorOffsetX = x;
        scissorOffsetY = y;
    }

    int getScissorOffsetX() const { return scissorOffsetX; }
    int getScissorOffsetY() const { return scissorOffsetY; }

    float getCenterX() const { return centerX; }
    float getCenterY() const { return centerY; }

private:
    int width = 0;
    int height = 0;
    int scissorOffsetX = 0;
    int scissorOffsetY = 0;
    float centerX = 0;
    float centerY = 0;
};
#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>

class Image
{
public:
    Image();
    ~Image();

    bool load(const char *imagePath);

    GLuint getTextureID() const { return textureID_; }

    int getWidth() const { return width_; }

    int getHeight() const { return height_; }

private:
    GLuint textureID_;
    int width_, height_;
};

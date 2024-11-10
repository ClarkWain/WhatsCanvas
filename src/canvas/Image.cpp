
#define STB_IMAGE_IMPLEMENTATION

#include "Image.h"
#include "stb_image.h"
#include <iostream>

Image::Image()
    : textureID_(0), width_(0), height_(0)
{
}

Image::~Image()
{
    if (textureID_)
    {
        glDeleteTextures(1, &textureID_);
    }
}

bool Image::load(const char *imagePath)
{
    int width, height, channels;
    unsigned char *data = stbi_load(imagePath, &width, &height, &channels, 0);
    if (data)
    {
        glGenTextures(1, &textureID_);
        glBindTexture(GL_TEXTURE_2D, textureID_);

        // 设置纹理参数
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        // 上传纹理数据
        GLenum format = channels == 4 ? GL_RGBA : GL_RGB;
        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
        glBindTexture(GL_TEXTURE_2D, 0);
        stbi_image_free(data);

        width_ = width;
        height_ = height;
        return true;
    }
    else
    {
        textureID_ = 0;
        width_ = 0;
        height_ = 0;

        // 处理加载失败的情况
        std::cerr << "Failed to load image: " << imagePath << std::endl;
        return false;
    }
}

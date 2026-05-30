#pragma once

#include <memory>

class IRenderer;
class ImageResource;

class Image
{
public:
    Image();
    ~Image();

    Image(const Image &) = delete;
    Image &operator=(const Image &) = delete;
    Image(Image &&other) noexcept;
    Image &operator=(Image &&other) noexcept;

    bool load(IRenderer &renderer, const char *imagePath);

    int getWidth() const { return width_; }

    int getHeight() const { return height_; }

    bool hasMipmaps() const { return mipmapsGenerated_; }

private:
    struct Storage;

    friend class Canvas;

    std::shared_ptr<ImageResource> getImageResource() const;
    void reset();

    std::unique_ptr<Storage> storage_;
    int width_ = 0;
    int height_ = 0;
    bool mipmapsGenerated_ = false;
};

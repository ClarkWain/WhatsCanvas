
#define STB_IMAGE_IMPLEMENTATION

#include "Image.h"
#include "render/IRenderer.h"
#include "render/RenderTypes.h"
#include "stb_image.h"

#include <memory>
#include <iostream>

struct Image::Storage {
    SharedImageResource imageResource;
};

Image::Image()
    : storage_(std::make_unique<Storage>())
{
}

Image::Image(Image &&other) noexcept
    : storage_(std::move(other.storage_)), width_(other.width_), height_(other.height_),
      mipmapsGenerated_(other.mipmapsGenerated_)
{
    other.width_ = 0;
    other.height_ = 0;
    other.mipmapsGenerated_ = false;
}

Image &Image::operator=(Image &&other) noexcept
{
    if (this == &other) {
        return *this;
    }

    reset();
    storage_ = std::move(other.storage_);
    width_ = other.width_;
    height_ = other.height_;
    mipmapsGenerated_ = other.mipmapsGenerated_;

    other.width_ = 0;
    other.height_ = 0;
    other.mipmapsGenerated_ = false;
    return *this;
}

Image::~Image()
{
    reset();
}

std::shared_ptr<ImageResource> Image::getImageResource() const
{
    if (!storage_) {
        return {};
    }

    return storage_->imageResource;
}

void Image::reset()
{
    if (storage_) {
        storage_->imageResource.reset();
    }
    width_ = 0;
    height_ = 0;
    mipmapsGenerated_ = false;
}

bool Image::load(IRenderer &renderer, const char *imagePath)
{
    int width, height, channels;
    unsigned char *data = stbi_load(imagePath, &width, &height, &channels, 0);
    if (data)
    {
        reset();

        if (!storage_) {
            storage_ = std::make_unique<Storage>();
        }

        storage_->imageResource = renderer.createImageResourceFromImageData(width, height, channels, data, true);
        stbi_image_free(data);

        if (!storage_ || !storage_->imageResource || !storage_->imageResource->isValid()) {
            reset();
            std::cerr << "Failed to create texture for image: " << imagePath << std::endl;
            return false;
        }

        width_ = width;
        height_ = height;
        mipmapsGenerated_ = true;
        return true;
    }
    else
    {
        reset();

        // 处理加载失败的情况
        std::cerr << "Failed to load image: " << imagePath << std::endl;
        return false;
    }
}

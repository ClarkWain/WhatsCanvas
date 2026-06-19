
#define STB_IMAGE_IMPLEMENTATION

#include "Image.h"
#include "render/IRenderer.h"
#include "render/RenderTypes.h"
#include "stb_image.h"

#include <memory>
#include <iostream>

struct wsc::Image::Storage {
    SharedImageResource imageResource;
};

wsc::Image::Image()
    : storage_(std::make_unique<Storage>())
{
}

wsc::Image::Image(Image &&other) noexcept
    : storage_(std::move(other.storage_)), width_(other.width_), height_(other.height_),
      mipmapsGenerated_(other.mipmapsGenerated_)
{
    other.width_ = 0;
    other.height_ = 0;
    other.mipmapsGenerated_ = false;
}

wsc::Image &wsc::Image::operator=(Image &&other) noexcept
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

wsc::Image::~Image()
{
    reset();
}

std::shared_ptr<ImageResource> wsc::Image::getImageResource() const
{
    if (!storage_) {
        return {};
    }

    return storage_->imageResource;
}

void wsc::Image::reset()
{
    if (storage_) {
        storage_->imageResource.reset();
    }
    width_ = 0;
    height_ = 0;
    mipmapsGenerated_ = false;
}

bool wsc::Image::load(IRenderer &renderer, const char *imagePath)
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

        // Handle image load failure
        std::cerr << "Failed to load image: " << imagePath << std::endl;
        return false;
    }
}

bool wsc::Image::isTextureValid() const
{
    if (!storage_ || !storage_->imageResource) {
        return false;
    }
    return storage_->imageResource->isValid() && width_ > 0 && height_ > 0;
}

void *wsc::Image::getTextureHandleOpaque() const
{
    return storage_ ? static_cast<void *>(&storage_->imageResource) : nullptr;
}


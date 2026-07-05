
#define STB_IMAGE_IMPLEMENTATION

#include "Image.h"
#include "../../include/wsc/Canvas.h"
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

bool wsc::Image::loadFromEncodedMemory(Canvas &canvas, const unsigned char *data, int size, bool generateMipmaps)
{
    return canvas.loadImageFromEncodedMemory(*this, data, size, generateMipmaps);
}

bool wsc::Image::loadFromRGBA(Canvas &canvas, const unsigned char *pixels, int width, int height, bool generateMipmaps)
{
    return canvas.loadImageFromRGBA(*this, pixels, width, height, generateMipmaps);
}

bool wsc::Image::loadFromRGBA(Canvas &canvas, const std::vector<unsigned char> &pixels, int width, int height,
                              bool generateMipmaps)
{
    return canvas.loadImageFromRGBA(*this, pixels, width, height, generateMipmaps);
}

bool wsc::Image::replacePixelsRGBA(Canvas &canvas, const unsigned char *pixels, int width, int height,
                                   bool generateMipmaps)
{
    return canvas.replaceImageRGBA(*this, pixels, width, height, generateMipmaps);
}

bool wsc::Image::replacePixelsRGBA(Canvas &canvas, const std::vector<unsigned char> &pixels, int width, int height,
                                   bool generateMipmaps)
{
    return canvas.replaceImageRGBA(*this, pixels, width, height, generateMipmaps);
}

bool wsc::Image::updatePixelsRGBA(Canvas &canvas, const unsigned char *pixels, int x, int y, int width, int height,
                                  bool regenerateMipmaps)
{
    return canvas.updateImageRGBA(*this, pixels, x, y, width, height, regenerateMipmaps);
}

bool wsc::Image::updatePixelsRGBA(Canvas &canvas, const std::vector<unsigned char> &pixels, int x, int y,
                                  int width, int height, bool regenerateMipmaps)
{
    return canvas.updateImageRGBA(*this, pixels, x, y, width, height, regenerateMipmaps);
}

bool wsc::Image::loadEncodedMemory(IRenderer &renderer, const unsigned char *data, int size, bool generateMipmaps)
{
    if (data == nullptr || size <= 0) {
        reset();
        return false;
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char *decoded = stbi_load_from_memory(data, size, &width, &height, &channels, 0);
    if (decoded == nullptr) {
        reset();
        return false;
    }

    reset();
    if (!storage_) {
        storage_ = std::make_unique<Storage>();
    }

    storage_->imageResource = renderer.createImageResourceFromImageData(width, height, channels, decoded, generateMipmaps);
    stbi_image_free(decoded);

    if (!storage_->imageResource || !storage_->imageResource->isValid()) {
        reset();
        return false;
    }

    width_ = width;
    height_ = height;
    mipmapsGenerated_ = generateMipmaps;
    return true;
}

bool wsc::Image::loadRGBA(IRenderer &renderer, const unsigned char *pixels, int width, int height, bool generateMipmaps)
{
    if (pixels == nullptr || width <= 0 || height <= 0) {
        reset();
        return false;
    }

    reset();
    if (!storage_) {
        storage_ = std::make_unique<Storage>();
    }

    storage_->imageResource = renderer.createImageResourceFromImageData(width, height, 4, pixels, generateMipmaps);
    if (!storage_->imageResource || !storage_->imageResource->isValid()) {
        reset();
        return false;
    }

    width_ = width;
    height_ = height;
    mipmapsGenerated_ = generateMipmaps;
    return true;
}

bool wsc::Image::replaceRGBA(IRenderer &renderer, const unsigned char *pixels, int width, int height,
                             bool generateMipmaps)
{
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return false;
    }

    if (!isTextureValid() || width != width_ || height != height_) {
        return loadRGBA(renderer, pixels, width, height, generateMipmaps);
    }

    if (!updateRGBA(renderer, pixels, 0, 0, width, height, generateMipmaps || mipmapsGenerated_)) {
        return false;
    }

    mipmapsGenerated_ = generateMipmaps || mipmapsGenerated_;
    return true;
}

bool wsc::Image::updateRGBA(IRenderer &renderer, const unsigned char *pixels, int x, int y, int width, int height,
                            bool regenerateMipmaps)
{
    if (pixels == nullptr || x < 0 || y < 0 || width <= 0 || height <= 0 || !isTextureValid()) {
        return false;
    }
    if (x > width_ - width || y > height_ - height) {
        return false;
    }

    if (!renderer.updateImageResourceRGBA(storage_->imageResource, x, y, width, height, pixels, regenerateMipmaps)) {
        return false;
    }

    mipmapsGenerated_ = regenerateMipmaps || mipmapsGenerated_;
    return true;
}

bool wsc::Image::wrapExternalTexture(Canvas &canvas, std::uint32_t textureId, int width, int height,
                                     bool mipmapsGenerated)
{
    return canvas.wrapExternalTexture(*this, textureId, width, height, mipmapsGenerated);
}

bool wsc::Image::wrapExternalImage(Canvas &canvas, const ExternalImageDescriptor &descriptor)
{
    return canvas.wrapExternalImage(*this, descriptor);
}

bool wsc::Image::wrapExternalTexture(IRenderer &renderer, std::uint32_t textureId, int width, int height,
                                     bool mipmapsGenerated)
{
    return wrapExternalImage(renderer, ExternalImageDescriptor::openGLTexture(textureId, width, height,
                                                                              mipmapsGenerated));
}

bool wsc::Image::wrapExternalImage(IRenderer &renderer, const ExternalImageDescriptor &descriptor)
{
    if (!descriptor.hasValidSize()) {
        reset();
        return false;
    }

    reset();
    if (!storage_) {
        storage_ = std::make_unique<Storage>();
    }

    storage_->imageResource = renderer.wrapExternalImageResource(descriptor);
    if (!storage_->imageResource || !storage_->imageResource->isValid()) {
        reset();
        return false;
    }

    width_ = descriptor.width;
    height_ = descriptor.height;
    mipmapsGenerated_ = descriptor.mipmapsGenerated;
    return true;
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

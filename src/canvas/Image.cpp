
#define STB_IMAGE_IMPLEMENTATION

#include "Image.h"
#include "../../include/wsc/Canvas.h"
#include "render/IRenderer.h"
#include "render/RenderTypes.h"
#include "stb_image.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <memory>
#include <iostream>
#include "core/LogInternal.h"

namespace {

bool rgbaByteCount(int width, int height, std::size_t &byteCount)
{
    if (width <= 0 || height <= 0) {
        return false;
    }
    const std::size_t w = static_cast<std::size_t>(width);
    const std::size_t h = static_cast<std::size_t>(height);
    if (w > std::numeric_limits<std::size_t>::max() / h
        || w * h > std::numeric_limits<std::size_t>::max() / 4u) {
        return false;
    }
    byteCount = w * h * 4u;
    return true;
}

} // namespace

struct wsc::Image::Storage {
    SharedImageResource imageResource;
    std::shared_ptr<std::vector<unsigned char>> cpuPixelsRGBA;
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

std::shared_ptr<const std::vector<unsigned char>>
wsc::Image::getCpuPixelsRGBA() const
{
    return storage_ ? storage_->cpuPixelsRGBA : nullptr;
}

void wsc::Image::reset()
{
    if (storage_) {
        storage_->imageResource.reset();
        storage_->cpuPixelsRGBA.reset();
    }
    width_ = 0;
    height_ = 0;
    mipmapsGenerated_ = false;
}

bool wsc::Image::load(IRenderer &renderer, const char *imagePath)
{
    int width, height, channels;
    unsigned char *data = stbi_load(imagePath, &width, &height, &channels, 4);
    if (data)
    {
        reset();

        if (!storage_) {
            storage_ = std::make_unique<Storage>();
        }

        std::size_t byteCount = 0;
        if (!rgbaByteCount(width, height, byteCount)) {
            stbi_image_free(data);
            reset();
            return false;
        }
        storage_->cpuPixelsRGBA = std::make_shared<std::vector<unsigned char>>(
            data, data + byteCount);
        storage_->imageResource = renderer.createImageResourceFromImageData(width, height, 4, data, true);
        stbi_image_free(data);

        if (!storage_ || !storage_->imageResource || !storage_->imageResource->isValid()) {
            reset();
            WSC_LOG_ERROR("Image", "Failed to create texture for image: " << imagePath);
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
        WSC_LOG_ERROR("Image", "Failed to load image: " << imagePath);
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
    unsigned char *decoded = stbi_load_from_memory(data, size, &width, &height, &channels, 4);
    if (decoded == nullptr) {
        reset();
        return false;
    }

    reset();
    if (!storage_) {
        storage_ = std::make_unique<Storage>();
    }

    std::size_t byteCount = 0;
    if (!rgbaByteCount(width, height, byteCount)) {
        stbi_image_free(decoded);
        reset();
        return false;
    }
    storage_->cpuPixelsRGBA = std::make_shared<std::vector<unsigned char>>(
        decoded, decoded + byteCount);
    storage_->imageResource = renderer.createImageResourceFromImageData(
        width, height, 4, decoded, generateMipmaps);
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

    std::size_t byteCount = 0;
    if (!rgbaByteCount(width, height, byteCount)) {
        reset();
        return false;
    }
    storage_->imageResource = renderer.createImageResourceFromImageData(width, height, 4, pixels, generateMipmaps);
    if (!storage_->imageResource || !storage_->imageResource->isValid()) {
        reset();
        return false;
    }

    width_ = width;
    height_ = height;
    mipmapsGenerated_ = generateMipmaps;
    storage_->cpuPixelsRGBA = std::make_shared<std::vector<unsigned char>>(
        pixels, pixels + byteCount);
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
    if (storage_->cpuPixelsRGBA) {
        if (!storage_->cpuPixelsRGBA.unique()) {
            storage_->cpuPixelsRGBA =
                std::make_shared<std::vector<unsigned char>>(
                    *storage_->cpuPixelsRGBA);
        }
        for (int row = 0; row < height; ++row) {
            const std::size_t sourceOffset =
                static_cast<std::size_t>(row) * static_cast<std::size_t>(width) * 4u;
            const std::size_t destinationOffset =
                (static_cast<std::size_t>(y + row) * static_cast<std::size_t>(width_)
                 + static_cast<std::size_t>(x)) * 4u;
            std::copy_n(
                pixels + sourceOffset,
                static_cast<std::size_t>(width) * 4u,
                storage_->cpuPixelsRGBA->begin()
                    + static_cast<std::ptrdiff_t>(destinationOffset));
        }
    }
    return true;
}

bool wsc::Image::wrapExternalTexture(Canvas &canvas, std::uint32_t textureId, int width, int height,
                                     bool mipmapsGenerated)
{
    return canvas.wrapExternalTexture(*this, textureId, width, height, mipmapsGenerated);
}

bool wsc::Image::wrapExternalMetalTexture(Canvas &canvas, void *texture, int width, int height,
                                          bool mipmapsGenerated)
{
    return canvas.wrapExternalMetalTexture(*this, texture, width, height, mipmapsGenerated);
}

bool wsc::Image::wrapExternalTexture(IRenderer &renderer, std::uint64_t textureHandle, int width, int height,
                                     bool mipmapsGenerated)
{
    if (textureHandle == 0 || width <= 0 || height <= 0) {
        reset();
        return false;
    }

    reset();
    if (!storage_) {
        storage_ = std::make_unique<Storage>();
    }

    storage_->imageResource = renderer.wrapExternalImageResource(ImageResourceHandle{textureHandle});
    if (!storage_->imageResource || !storage_->imageResource->isValid()) {
        reset();
        return false;
    }

    width_ = width;
    height_ = height;
    mipmapsGenerated_ = mipmapsGenerated;
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

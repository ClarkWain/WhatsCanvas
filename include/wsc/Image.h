#pragma once

#include <memory>
#include <cstdint>
#include <vector>

#include "Export.h"
#include "TextureSource.h"

class IRenderer;
class ImageResource;

namespace wsc {
class Canvas;

/// GPU-backed image resource managed by the canvas runtime.
class WSC_API Image : public ITextureSource
{
public:
	Image();
	~Image() override;

	Image(const Image &) = delete;
	Image &operator=(const Image &) = delete;
	Image(Image &&other) noexcept;
	Image &operator=(Image &&other) noexcept;

	int getWidth() const { return width_; }
	int getHeight() const { return height_; }
	bool hasMipmaps() const { return mipmapsGenerated_; }

	bool loadFromEncodedMemory(Canvas &canvas, const unsigned char *data, int size, bool generateMipmaps = true);
	bool loadFromRGBA(Canvas &canvas, const unsigned char *pixels, int width, int height, bool generateMipmaps = false);
	bool loadFromRGBA(Canvas &canvas, const std::vector<unsigned char> &pixels, int width, int height,
	                  bool generateMipmaps = false);
	bool wrapExternalTexture(Canvas &canvas, std::uint32_t textureId, int width, int height,
	                         bool mipmapsGenerated = false);

	// ITextureSource interface
	int getTextureWidth() const override { return width_; }
	int getTextureHeight() const override { return height_; }
	bool isTextureValid() const override;
	bool isRenderTarget() const override { return false; }

protected:
	void *getTextureHandleOpaque() const override;
	bool hasMipmapsGenerated() const override { return mipmapsGenerated_; }

private:
	struct Storage;

	friend class Canvas;

	bool load(::IRenderer &renderer, const char *imagePath);
	bool loadEncodedMemory(::IRenderer &renderer, const unsigned char *data, int size, bool generateMipmaps);
	bool loadRGBA(::IRenderer &renderer, const unsigned char *pixels, int width, int height, bool generateMipmaps);
	bool wrapExternalTexture(::IRenderer &renderer, std::uint32_t textureId, int width, int height,
	                         bool mipmapsGenerated);
	std::shared_ptr<::ImageResource> getImageResource() const;
	void reset();

	std::unique_ptr<Storage> storage_;
	int width_ = 0;
	int height_ = 0;
	bool mipmapsGenerated_ = false;
};
} // namespace wsc

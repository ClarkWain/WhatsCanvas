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
class ImageLifecycleTestAccess;

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

	/// Decode an in-memory image file (PNG/JPG/...) into a GPU texture.
	bool loadFromEncodedMemory(Canvas &canvas, const unsigned char *data, int size, bool generateMipmaps = true);
	/// Upload raw 8-bit RGBA pixels (width*height*4 bytes) as a GPU texture.
	bool loadFromRGBA(Canvas &canvas, const unsigned char *pixels, int width, int height, bool generateMipmaps = false);
	bool loadFromRGBA(Canvas &canvas, const std::vector<unsigned char> &pixels, int width, int height,
	                  bool generateMipmaps = false);
	/// Replace the entire image contents with new RGBA pixels (may resize).
	bool replacePixelsRGBA(Canvas &canvas, const unsigned char *pixels, int width, int height,
	                       bool generateMipmaps = false);
	bool replacePixelsRGBA(Canvas &canvas, const std::vector<unsigned char> &pixels, int width, int height,
	                       bool generateMipmaps = false);
	/// Update a sub-rectangle of the existing image with new RGBA pixels.
	bool updatePixelsRGBA(Canvas &canvas, const unsigned char *pixels, int x, int y, int width, int height,
	                      bool regenerateMipmaps = true);
	bool updatePixelsRGBA(Canvas &canvas, const std::vector<unsigned char> &pixels, int x, int y, int width,
	                      int height, bool regenerateMipmaps = true);
	/// Wrap an externally-owned GL texture id as an Image (no ownership transfer).
	bool wrapExternalTexture(Canvas &canvas, std::uint32_t textureId, int width, int height,
	                         bool mipmapsGenerated = false);
	/// Wrap an externally-owned id<MTLTexture> as an Image on a Metal Canvas.
	/// Pass the Objective-C object as an opaque pointer; ownership remains with
	/// the caller, while the Image retains it for the duration of the wrapper.
	bool wrapExternalMetalTexture(Canvas &canvas, void *texture, int width, int height,
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
	friend class ImageLifecycleTestAccess;

	bool load(::IRenderer &renderer, const char *imagePath);
	bool loadEncodedMemory(::IRenderer &renderer, const unsigned char *data, int size, bool generateMipmaps);
	bool loadRGBA(::IRenderer &renderer, const unsigned char *pixels, int width, int height, bool generateMipmaps);
	bool replaceRGBA(::IRenderer &renderer, const unsigned char *pixels, int width, int height, bool generateMipmaps);
	bool updateRGBA(::IRenderer &renderer, const unsigned char *pixels, int x, int y, int width, int height,
	                bool regenerateMipmaps);
	bool wrapExternalTexture(::IRenderer &renderer, std::uint64_t textureHandle, int width, int height,
	                         bool mipmapsGenerated);
	std::shared_ptr<::ImageResource> getImageResource() const;
	void reset();

	std::unique_ptr<Storage> storage_;
	int width_ = 0;
	int height_ = 0;
	bool mipmapsGenerated_ = false;
};
} // namespace wsc

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

/// Move-only image resource created for one Canvas backend/device.
///
/// Upload/decode calls copy their input before returning. A successfully loaded
/// Image may be drawn repeatedly by its owning Canvas; using it with an
/// unrelated backend/device is unsupported and draws nothing. Destroy Images
/// before orderly Canvas/context teardown when practical. External texture
/// wrappers retain no ownership unless explicitly stated by that overload.
///
/// Minimal usage:
/// @code{.cpp}
/// auto canvas = wsc::Canvas::create(wsc::Canvas::Backend::Software, 256, 256);
/// if (!canvas) return;
///
/// std::vector<unsigned char> rgba(256 * 256 * 4, 255);
/// wsc::Image image;
/// if (!image.loadFromRGBA(*canvas, rgba, 256, 256)) return;
///
/// canvas->beginFrame();
/// canvas->drawImage(image, 0.0f, 0.0f, 256.0f, 256.0f, wsc::Paint());
/// canvas->endFrame();
/// @endcode
///
/// Ownership and reuse rules:
/// - `Image` is bound to a specific Canvas backend/device; reusing it with a
///   different Canvas or a different GL/Metal/Vulkan device is unsupported.
/// - `Image` instances are not thread-safe and should be used on the same render
///   thread as their owning Canvas.
/// - External texture wrappers only borrow the supplied handle; the caller must
///   keep that native resource alive for as long as the wrapper is used.
class WSC_API Image : public ITextureSource
{
public:
	Image();

	~Image() override;

	Image(const Image &) = delete;

	Image &operator=(const Image &) = delete;

	Image(Image &&other) noexcept;

	Image &operator=(Image &&other) noexcept;

	/// Pixel dimensions of the current resource, or zero before a successful load.
	int getWidth() const { return width_; }

	int getHeight() const { return height_; }

	bool hasMipmaps() const { return mipmapsGenerated_; }

	/// Decode an in-memory PNG/JPEG/etc. snapshot. Returns false for null/empty
	/// or unsupported data and for backend allocation/upload failure.
	bool loadFromEncodedMemory(Canvas &canvas, const unsigned char *data, int size, bool generateMipmaps = true);

	/// Upload tightly packed straight-alpha RGBA8 pixels (`width*height*4`
	/// bytes). The dimensions must be positive.
	bool loadFromRGBA(Canvas &canvas, const unsigned char *pixels, int width, int height, bool generateMipmaps = false);

	bool loadFromRGBA(Canvas &canvas, const std::vector<unsigned char> &pixels, int width, int height,
	                  bool generateMipmaps = false);

	/// Replace the entire image snapshot with RGBA8 pixels (may resize). Failure
	/// leaves the caller responsible for checking isTextureValid() before use.
	bool replacePixelsRGBA(Canvas &canvas, const unsigned char *pixels, int width, int height,
	                       bool generateMipmaps = false);

	bool replacePixelsRGBA(Canvas &canvas, const std::vector<unsigned char> &pixels, int width, int height,
	                       bool generateMipmaps = false);

	/// Update an in-bounds sub-rectangle with tightly packed RGBA8 pixels.
	bool updatePixelsRGBA(Canvas &canvas, const unsigned char *pixels, int x, int y, int width, int height,
	                      bool regenerateMipmaps = true);

	bool updatePixelsRGBA(Canvas &canvas, const std::vector<unsigned char> &pixels, int x, int y, int width,
	                      int height, bool regenerateMipmaps = true);

	/// Wrap a non-zero externally owned GL texture from the Canvas' current
	/// context. No ownership transfers; the caller keeps it alive and deletes it.
	bool wrapExternalTexture(Canvas &canvas, std::uint32_t textureId, int width, int height,
	                         bool mipmapsGenerated = false);

	/// Wrap an externally-owned id<MTLTexture> as an Image on a Metal Canvas.
	/// Pass the Objective-C object as an opaque pointer. It must come from the
	/// Canvas' MTLDevice. Ownership remains with the caller, while Image retains
	/// the object for the duration of the wrapper.
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

	std::shared_ptr<const std::vector<unsigned char>> getCpuPixelsRGBA() const;

	void reset();

	std::unique_ptr<Storage> storage_;
	int width_ = 0;
	int height_ = 0;
	bool mipmapsGenerated_ = false;
};
} // namespace wsc

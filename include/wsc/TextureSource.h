#pragma once

#include <cstdint>

#include "Export.h"

namespace wsc {

/// Unified interface for any object that can serve as a GPU texture source.
/// Both Image and Canvas implement this interface, allowing either to be used
/// wherever a texture is expected (e.g. drawImage, SpriteBatch, etc.).
class WSC_API ITextureSource
{
public:
	ITextureSource() = default;
	ITextureSource(const ITextureSource &) = delete;
	ITextureSource &operator=(const ITextureSource &) = delete;
	virtual ~ITextureSource() = default;

	/// Width of the underlying texture in pixels.
	virtual int getTextureWidth() const = 0;

	/// Height of the underlying texture in pixels.
	virtual int getTextureHeight() const = 0;

	/// Whether the texture data is valid and ready for sampling.
	virtual bool isTextureValid() const = 0;

	/// Whether this source is a render target (Canvas) rather than a loaded image.
	virtual bool isRenderTarget() const = 0;

protected:
	friend class Canvas;

	/// Internal accessor for the backing GPU image resource.
	/// Returns an opaque handle that Canvas internals know how to interpret.
	/// Default returns nullptr. Subclasses must override.
	virtual void *getTextureHandleOpaque() const { return nullptr; }

	/// Whether mipmaps have been generated for this source.
	virtual bool hasMipmapsGenerated() const { return false; }
};

} // namespace wsc

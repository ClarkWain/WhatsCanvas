#pragma once

// Public presentation types for on-screen rendering. See the internal
// ISwapchain (src/render/Surface.h) and doc/windowed-presentation-design.md.

namespace wsc {

/// A neutral operating-system window handle. The only input common to every
/// graphics backend is the OS window itself; each backend builds its own
/// surface/swapchain from this. Populate it directly, or via a platform helper.
struct NativeSurface
{
	enum class Platform
	{
		Win32,
		Xlib,
		Xcb,
		Wayland,
		Cocoa,
		Android,
	};

	Platform platform = Platform::Win32;
	void *window = nullptr;  ///< HWND / NSView* / xcb_window / ANativeWindow* / CAMetalLayer*
	void *display = nullptr; ///< HINSTANCE / Display* / wl_display* (when the platform needs it)
};

/// Neutral swapchain preferences. Backends map these to their own concepts and
/// gracefully degrade features they cannot honor.
struct SwapchainConfig
{
	bool vsync = true;
	int imageCount = 3;
};

/// A host-owned backend render target to draw into directly (Skia-style
/// wrap-external). Lets an existing engine hand WhatsCanvas the current frame's
/// target instead of the library owning a swapchain. Only the fields for the
/// active `kind` are meaningful.
struct BackendRenderTarget
{
	enum class Kind
	{
		None,
		OpenGLFramebuffer, ///< `glFramebuffer` is a GL FBO name (0 = default).
		VulkanImage,       ///< `nativeHandle` is a VkImage; `nativeFormat` a VkFormat.
		MetalTexture,      ///< `nativeHandle` is an id<MTLTexture>.
		D3DTexture,        ///< `nativeHandle` is an ID3D11Texture2D* / D3D12 resource.
	};

	Kind kind = Kind::None;
	unsigned int glFramebuffer = 0;         ///< OpenGLFramebuffer.
	void *nativeHandle = nullptr;           ///< VkImage / id<MTLTexture> / D3D texture.
	unsigned long long nativeFormat = 0;    ///< Backend format enum (e.g. VkFormat).
	int width = 0;
	int height = 0;
};

} // namespace wsc

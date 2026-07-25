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

/// Where a canvas delivers its rendered frames. A single "output axis":
/// off-screen (read back / use as a texture), an on-screen window, or a
/// host-owned backend render target (embed into an existing GL/Vulkan renderer).
/// Construct via the static factories. See doc/windowed-presentation-design.md.
struct OutputTarget
{
	enum class Kind
	{
		Offscreen,         ///< Render into a canvas-owned target; read back with readPixelsRGBA.
		OffscreenTexture,  ///< Like Offscreen, but also usable as a texture (ITextureSource).
		Window,            ///< Present to an OS window (library owns the swapchain/blit).
		OpenGLFramebuffer, ///< Render into a host-owned GL framebuffer (wrap-external).
		VulkanImage,       ///< Render into a host-owned VkImage (wrap-external).
	};

	Kind kind = Kind::Offscreen;
	NativeSurface window;                ///< Window
	SwapchainConfig config;              ///< Window
	unsigned int glFramebuffer = 0;      ///< OpenGLFramebuffer (0 = default framebuffer)
	void *vulkanImage = nullptr;         ///< VulkanImage (a VkImage)
	unsigned long long vulkanFormat = 0; ///< VulkanImage (a VkFormat)
	int width = 0;                       ///< OpenGLFramebuffer / VulkanImage
	int height = 0;                      ///< OpenGLFramebuffer / VulkanImage
	bool opaque = false;                 ///< Destination alpha is known to remain fully opaque.

	/// Off-screen: render internally, read back with readPixelsRGBA.
	static OutputTarget Offscreen() { return OutputTarget{}; }

	/// Off-screen but also usable as a texture in another canvas (drawImage).
	static OutputTarget OffscreenTexture()
	{
		OutputTarget t;
		t.kind = Kind::OffscreenTexture;
		return t;
	}

	/// Present to an OS window (the library builds the swapchain / blit).
	static OutputTarget ToWindow(const NativeSurface &surface, const SwapchainConfig &config = SwapchainConfig())
	{
		OutputTarget t;
		t.kind = Kind::Window;
		t.window = surface;
		t.config = config;
		t.opaque = true;
		return t;
	}

	/// Render into a host-owned OpenGL framebuffer object.
	static OutputTarget GLFramebuffer(unsigned int framebuffer, int width, int height, bool opaque = false)
	{
		OutputTarget t;
		t.kind = Kind::OpenGLFramebuffer;
		t.glFramebuffer = framebuffer;
		t.width = width;
		t.height = height;
		t.opaque = opaque;
		return t;
	}

	/// Render into a host-owned VkImage (created on this canvas's Vulkan device,
	/// R8G8B8A8_UNORM with COLOR_ATTACHMENT and TRANSFER_SRC usage). `format` is
	/// a VkFormat.
	static OutputTarget VulkanImageTarget(void *image, unsigned long long format, int width, int height)
	{
		OutputTarget t;
		t.kind = Kind::VulkanImage;
		t.vulkanImage = image;
		t.vulkanFormat = format;
		t.width = width;
		t.height = height;
		return t;
	}
};

} // namespace wsc

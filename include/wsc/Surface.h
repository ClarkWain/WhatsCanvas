#pragma once

/**
 * @file Surface.h
 * @brief Backend-neutral output/presentation descriptors.
 *
 * These are non-owning descriptions: the host keeps OS windows, displays,
 * external framebuffers and images alive for as long as the Canvas uses them.
 */

namespace wsc {

/// A neutral operating-system window handle. The only input common to every
/// graphics backend is the OS window itself; each backend builds its own
/// surface/swapchain from this. All handles are borrowed, not owned.
struct NativeSurface
{
	enum class Platform
	{
		Win32,   ///< `window=HWND`, `display=HINSTANCE` when required.
		Xlib,    ///< `window=Window` encoded as a pointer, `display=Display*`.
		Xcb,     ///< `window=xcb_window_t` encoded as a pointer, `display=xcb_connection_t*`.
		Wayland, ///< `window=wl_surface*`, `display=wl_display*`.
		Cocoa,   ///< `window=NSView*` or backend-compatible `CAMetalLayer*`.
		Android, ///< `window=ANativeWindow*`.
	};

	Platform platform = Platform::Win32;
	void *window = nullptr;  ///< HWND / NSView* / xcb_window / ANativeWindow* / CAMetalLayer*
	void *display = nullptr; ///< HINSTANCE / Display* / wl_display* (when the platform needs it)
};

/// Neutral swapchain preferences. Backends map these to their own concepts and
/// gracefully degrade preferences they cannot honor. Sizes are not stored here;
/// use Canvas::setSize/resizeOutput with the physical drawable dimensions.
struct SwapchainConfig
{
	bool vsync = true; ///< Request presentation synchronized to display refresh.
	int imageCount = 3; ///< Preferred swapchain buffering count.
};

/// Where a canvas delivers its rendered frames. A single "output axis":
/// off-screen (read back / use as a texture), an on-screen window, or a
/// host-owned backend render target (embed into an existing GL/Vulkan renderer).
/// Construct via the static factories and pass to Canvas::setOutputTarget().
/// Changing targets invalidates the previous target configuration.
///
/// Minimal usage patterns:
/// - Off-screen rendering: `OutputTarget::Offscreen()`
/// - Window presentation: `OutputTarget::ToWindow(surface)` where `surface` is a
///   prepared `NativeSurface`
/// - Host-owned framebuffer: `OutputTarget::GLFramebuffer(...)`
/// - Host-owned Vulkan image: `OutputTarget::VulkanImageTarget(...)`
///
/// Typical lifecycle for an off-screen target:
/// `auto canvas = wsc::Canvas::create(...); canvas->setOutputTarget(wsc::OutputTarget::Offscreen()); canvas->beginFrame(); draw...; canvas->endFrame();`
/// For a window target, the final frame submission is `endFrame(); present();`.
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

	/// Present to a borrowed OS window. Returns a descriptor only; support and
	/// handle validity are checked when Canvas::setOutputTarget() is called.
	static OutputTarget ToWindow(const NativeSurface &surface, const SwapchainConfig &config = SwapchainConfig())
	{
		OutputTarget t;
		t.kind = Kind::Window;
		t.window = surface;
		t.config = config;
		t.opaque = true;
		return t;
	}

	/// Render into a host-owned OpenGL framebuffer in this Canvas' current
	/// context. Width/height are physical pixels; framebuffer 0 means the
	/// context's default framebuffer. The host retains ownership.
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
	/// a VkFormat encoded as an integer. The host owns synchronization and image
	/// lifetime outside WhatsCanvas submissions.
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

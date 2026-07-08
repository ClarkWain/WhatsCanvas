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

} // namespace wsc

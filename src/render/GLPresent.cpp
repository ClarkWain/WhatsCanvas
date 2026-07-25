#include "GLPresent.h"

#include "core/LogInternal.h"

#if defined(_WIN32) && !defined(WHATSCANVAS_OPENGL_ES)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#elif defined(__linux__) && defined(WHATSCANVAS_HAS_X11) \
    && !defined(WHATSCANVAS_OPENGL_ES)
#include <cstdint>
#include <GL/glx.h>
#include <X11/Xlib.h>
#endif

namespace wsc::gl {

#if defined(_WIN32) && !defined(WHATSCANVAS_OPENGL_ES)

bool glPresentSupported()
{
	return true;
}

namespace {

/// Host-owned GL swapchain: swaps the window's front/back buffers via WGL. The
/// host owns the GL context and must keep it current; WhatsCanvas renders into
/// the default framebuffer before present().
class GLSwapchain final : public ISwapchain
{
public:
	explicit GLSwapchain(HWND hwnd) : hwnd_(hwnd), hdc_(GetDC(hwnd)) {}

	~GLSwapchain() override
	{
		if (hdc_ != nullptr) {
			ReleaseDC(hwnd_, hdc_);
		}
	}

	AcquiredImage acquire() override
	{
		RECT client{};
		if (GetClientRect(hwnd_, &client)) {
			width_ = client.right - client.left;
			height_ = client.bottom - client.top;
		}
		return AcquiredImage{nullptr, width_, height_, true};
	}

	bool present() override
	{
		if (hdc_ == nullptr) {
			return false;
		}
		return SwapBuffers(hdc_) != FALSE;
	}

	void resize(int width, int height) override
	{
		if (width > 0 && height > 0) {
			width_ = width;
			height_ = height;
		}
	}

private:
	HWND hwnd_ = nullptr;
	HDC hdc_ = nullptr;
	int width_ = 0;
	int height_ = 0;
};

} // namespace

std::unique_ptr<ISwapchain> makeGLSwapchain(const NativeSurface &surface, const SwapchainConfig & /*config*/)
{
	if (surface.platform != NativeSurface::Platform::Win32 || surface.window == nullptr) {
		WSC_LOG_WARN("GLPresent", "GL presentation requires a Win32 window handle.");
		return nullptr;
	}
	return std::make_unique<GLSwapchain>(static_cast<HWND>(surface.window));
}

#elif defined(__linux__) && defined(WHATSCANVAS_HAS_X11) \
    && !defined(WHATSCANVAS_OPENGL_ES)

// NOTE: This GLX path has NOT been compiled/validated in the current
// development environment (Windows). Verify on Linux before relying on it.

bool glPresentSupported()
{
	return true;
}

namespace {

class GLXSwapchain final : public ISwapchain
{
public:
	GLXSwapchain(Display *display, GLXDrawable drawable) : display_(display), drawable_(drawable) {}

	AcquiredImage acquire() override { return AcquiredImage{nullptr, width_, height_, true}; }

	bool present() override
	{
		glXSwapBuffers(display_, drawable_);
		return true;
	}

	void resize(int width, int height) override
	{
		if (width > 0 && height > 0) {
			width_ = width;
			height_ = height;
		}
	}

private:
	Display *display_ = nullptr;
	GLXDrawable drawable_ = 0;
	int width_ = 0;
	int height_ = 0;
};

} // namespace

std::unique_ptr<ISwapchain> makeGLSwapchain(const NativeSurface &surface, const SwapchainConfig & /*config*/)
{
	if (surface.platform != NativeSurface::Platform::Xlib || surface.display == nullptr ||
	    surface.window == nullptr) {
		WSC_LOG_WARN("GLPresent", "GLX presentation requires an Xlib display and window.");
		return nullptr;
	}
	Display *display = static_cast<Display *>(surface.display);
	GLXDrawable drawable = static_cast<GLXDrawable>(reinterpret_cast<std::uintptr_t>(surface.window));
	return std::make_unique<GLXSwapchain>(display, drawable);
}

#else // unsupported platform

bool glPresentSupported()
{
	return false;
}

std::unique_ptr<ISwapchain> makeGLSwapchain(const NativeSurface &, const SwapchainConfig &)
{
	return nullptr;
}

#endif

} // namespace wsc::gl

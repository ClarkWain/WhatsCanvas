#include "SoftwarePresent.h"

#include <utility>

#include "core/LogInternal.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace wsc::software {

#if defined(_WIN32)

bool softwarePresentSupported()
{
	return true;
}

bool blitRgbaTopDownToHdc(void *hdc, const unsigned char *rgba, int srcWidth, int srcHeight, int dstWidth,
                          int dstHeight)
{
	if (hdc == nullptr || rgba == nullptr || srcWidth <= 0 || srcHeight <= 0 || dstWidth <= 0 || dstHeight <= 0) {
		return false;
	}

	// GDI 32bpp BI_RGB expects BGRA byte order; our source is RGBA. Convert into
	// a scratch buffer and use a negative height for top-down orientation so the
	// image is not flipped vertically.
	const std::size_t pixelCount = static_cast<std::size_t>(srcWidth) * static_cast<std::size_t>(srcHeight);
	std::vector<unsigned char> bgra(pixelCount * 4u);
	for (std::size_t i = 0; i < pixelCount; ++i) {
		const unsigned char r = rgba[i * 4u + 0u];
		const unsigned char g = rgba[i * 4u + 1u];
		const unsigned char b = rgba[i * 4u + 2u];
		const unsigned char a = rgba[i * 4u + 3u];
		bgra[i * 4u + 0u] = b;
		bgra[i * 4u + 1u] = g;
		bgra[i * 4u + 2u] = r;
		bgra[i * 4u + 3u] = a;
	}

	BITMAPINFO bmi{};
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = srcWidth;
	bmi.bmiHeader.biHeight = -srcHeight; // top-down
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;

	const int scanLines = StretchDIBits(static_cast<HDC>(hdc), 0, 0, dstWidth, dstHeight, 0, 0, srcWidth, srcHeight,
	                                    bgra.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
	return scanLines != 0;
}

namespace {

/// Presents a software renderer's CPU framebuffer to a Win32 window via GDI.
class SoftwareSwapchain final : public ISwapchain
{
public:
	SoftwareSwapchain(HWND hwnd, PixelSource source)
		: hwnd_(hwnd), source_(std::move(source))
	{
	}

	AcquiredImage acquire() override
	{
		// The render target is the software renderer's own framebuffer; there is
		// no external image to hand back. Report the last known size.
		return AcquiredImage{nullptr, width_, height_, true};
	}

	bool present() override
	{
		std::vector<unsigned char> pixels;
		int w = 0;
		int h = 0;
		if (!source_ || !source_(pixels, w, h) || w <= 0 || h <= 0) {
			return false;
		}
		width_ = w;
		height_ = h;

		HDC hdc = GetDC(hwnd_);
		if (hdc == nullptr) {
			WSC_LOG_WARN("SoftwarePresent", "GetDC returned null; cannot present.");
			return false;
		}

		RECT client{};
		int dstW = w;
		int dstH = h;
		if (GetClientRect(hwnd_, &client)) {
			dstW = client.right - client.left;
			dstH = client.bottom - client.top;
			if (dstW <= 0 || dstH <= 0) {
				dstW = w;
				dstH = h;
			}
		}

		const bool ok = blitRgbaTopDownToHdc(hdc, pixels.data(), w, h, dstW, dstH);
		ReleaseDC(hwnd_, hdc);
		return ok;
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
	PixelSource source_;
	int width_ = 0;
	int height_ = 0;
};

} // namespace

std::unique_ptr<ISwapchain> makeSoftwareSwapchain(const NativeSurface &surface, const SwapchainConfig & /*config*/,
                                                  PixelSource source)
{
	if (surface.platform != NativeSurface::Platform::Win32 || surface.window == nullptr) {
		WSC_LOG_WARN("SoftwarePresent", "Software presentation requires a Win32 window handle.");
		return nullptr;
	}
	return std::make_unique<SoftwareSwapchain>(static_cast<HWND>(surface.window), std::move(source));
}

#else // !_WIN32

bool softwarePresentSupported()
{
	return false;
}

bool blitRgbaTopDownToHdc(void *, const unsigned char *, int, int, int, int)
{
	return false;
}

std::unique_ptr<ISwapchain> makeSoftwareSwapchain(const NativeSurface &, const SwapchainConfig &, PixelSource)
{
	return nullptr;
}

#endif

} // namespace wsc::software

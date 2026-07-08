// Tests for the software (CPU) on-screen presentation path. The Win32/GDI blit
// is validated headlessly by blitting into an in-memory DIB section and reading
// the bytes back — no visible window required. Cross-platform checks validate
// the surface-rejection behavior.

#include <iostream>
#include <string>
#include <vector>

#include "render/Surface.h"
#include "render/software/SoftwarePresent.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace {

bool expect(bool condition, const std::string &message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << std::endl;
    }
    return condition;
}

bool testRejectsUnusableSurface()
{
    // Non-Win32 platform → unsupported → nullptr.
    NativeSurface xlib;
    xlib.platform = NativeSurface::Platform::Xlib;
    xlib.window = reinterpret_cast<void *>(0x1);
    bool ok = expect(wsc::software::makeSoftwareSwapchain(xlib, SwapchainConfig{}, nullptr) == nullptr,
                     "non-Win32 surface should not produce a software swapchain");

    // Win32 platform but no window handle → nullptr.
    NativeSurface noWindow;
    noWindow.platform = NativeSurface::Platform::Win32;
    noWindow.window = nullptr;
    ok = expect(wsc::software::makeSoftwareSwapchain(noWindow, SwapchainConfig{}, nullptr) == nullptr,
                "Win32 surface with null window should not produce a swapchain") && ok;
    return ok;
}

#if defined(_WIN32)

bool testBlitToDibSection()
{
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    if (mem == nullptr) {
        return expect(false, "CreateCompatibleDC should succeed");
    }

    const int w = 2;
    const int h = 2;
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void *bits = nullptr;
    HBITMAP dib = CreateDIBSection(mem, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (dib == nullptr || bits == nullptr) {
        DeleteDC(mem);
        return expect(false, "CreateDIBSection should succeed");
    }
    HGDIOBJ prev = SelectObject(mem, dib);

    // Top-left-origin RGBA: red, green, blue, then an arbitrary color.
    std::vector<unsigned char> rgba = {
        255, 0, 0, 255,
        0, 255, 0, 255,
        0, 0, 255, 255,
        10, 20, 30, 40,
    };

    bool ok = expect(wsc::software::blitRgbaTopDownToHdc(mem, rgba.data(), w, h, w, h),
                     "blitRgbaTopDownToHdc should succeed");
    GdiFlush();

    const unsigned char *out = static_cast<const unsigned char *>(bits);
    auto checkBgr = [&](int i, unsigned char r, unsigned char g, unsigned char b, const char *name) {
        const bool match = out[i * 4 + 0] == b && out[i * 4 + 1] == g && out[i * 4 + 2] == r;
        return expect(match, std::string("pixel ") + name + " should match after RGBA->BGRA blit");
    };
    ok = checkBgr(0, 255, 0, 0, "red") && ok;    // top-left stays top-left (no vertical flip)
    ok = checkBgr(1, 0, 255, 0, "green") && ok;
    ok = checkBgr(2, 0, 0, 255, "blue") && ok;
    ok = checkBgr(3, 10, 20, 30, "rgb") && ok;

    SelectObject(mem, prev);
    DeleteObject(dib);
    DeleteDC(mem);
    return ok;
}

#endif // _WIN32

bool testSupportFlag()
{
#if defined(_WIN32)
    return expect(wsc::software::softwarePresentSupported(), "software present should be supported on Windows");
#else
    return expect(!wsc::software::softwarePresentSupported(),
                  "software present should be unsupported off Windows");
#endif
}

} // namespace

int main()
{
    bool ok = true;
    ok = testSupportFlag() && ok;
    ok = testRejectsUnusableSurface() && ok;
#if defined(_WIN32)
    ok = testBlitToDibSection() && ok;
#endif

    if (!ok) {
        std::cerr << "SoftwarePresentTests: FAILED" << std::endl;
        return 1;
    }
    std::cout << "SoftwarePresentTests: all checks passed." << std::endl;
    return 0;
}

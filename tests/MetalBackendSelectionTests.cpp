// Public backend-selection tests for the Metal render backend.
//
// Mirrors the shape of `tests/VulkanBackendSelectionTests.cpp`: it uses only
// the stable `wsc::Canvas` public API, so the test remains meaningful in every
// configuration:
//   * When Metal is NOT compiled in (non-Apple hosts, or the option is off),
//     it asserts the graceful contract: isBackendAvailable(Metal) is false and
//     create(Metal) returns null so callers cleanly fall back to another
//     backend.
//   * When Metal IS available (Apple + WHATSCANVAS_ENABLE_METAL + a device),
//     it creates a Metal-backed canvas off-screen, renders a solid fill, reads
//     the pixels back, and asserts the expected color.

#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "wsc/wsc.h"

using namespace wsc;

namespace {

bool expect(bool condition, const std::string &message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << std::endl;
    }
    return condition;
}

bool testMetalBackendSelection()
{
    const bool available = Canvas::isBackendAvailable(Canvas::Backend::Metal);

    if (!available) {
        std::unique_ptr<Canvas> canvas = Canvas::create(Canvas::Backend::Metal, 32, 32);
        const bool ok = expect(canvas == nullptr,
                               "create(Metal) should return null when Metal is unavailable");
        std::cout << "Metal unavailable in this build: graceful fallback contract verified.\n";
        return ok;
    }

    const int w = 64;
    const int h = 64;
    std::unique_ptr<Canvas> canvas = Canvas::create(Canvas::Backend::Metal, w, h);
    bool ok = expect(canvas != nullptr, "create(Metal) should return a canvas when Metal is available");
    if (!canvas) {
        return false;
    }
    canvas->initializeContext();
    ok = expect(canvas->getWidth() == w && canvas->getHeight() == h,
                "Metal canvas should keep its requested size") && ok;

    canvas->beginFrame();
    Paint fill;
    fill.setStyle(Paint::Style::FILL);
    fill.setColor(Color(255, 0, 0, 255));
    fill.setAntiAlias(false);
    canvas->drawRect(RectF(16.0f, 16.0f, 32.0f, 32.0f), fill);
    canvas->endFrame();

    std::vector<unsigned char> pixels;
    ok = expect(canvas->readPixelsRGBA(pixels) && pixels.size() == static_cast<std::size_t>(w) * h * 4u,
                "readPixelsRGBA should succeed on the Metal canvas") && ok;
    if (!ok) {
        return false;
    }

    auto pixelAt = [&](int x, int y) { return &pixels[(static_cast<std::size_t>(y) * w + x) * 4u]; };
    const unsigned char *center = pixelAt(32, 32);
    ok = expect(center[0] > 200 && center[1] < 40 && center[2] < 40 && center[3] > 200,
                "Metal rect interior should be opaque red") && ok;
    std::cout << "Metal backend selected and rendered a solid fill off-screen.\n";
    return ok;
}

} // namespace

int main()
{
    return testMetalBackendSelection() ? 0 : 1;
}

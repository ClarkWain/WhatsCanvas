// Public backend-selection tests for the Vulkan render backend.
//
// These exercise the public Canvas Vulkan entry points (isVulkanAvailable /
// createVulkan) through the stable API surface only. The test is adaptive so it
// is meaningful in every configuration:
//   * When Vulkan is NOT compiled in (default builds), it asserts the graceful
//     contract: isVulkanAvailable() is false and createVulkan() returns null.
//   * When Vulkan IS available (WHATSCANVAS_ENABLE_VULKAN + a device), it
//     creates a Vulkan-backed canvas off-screen, renders a solid fill, reads
//     the pixels back, and asserts the expected color — proving Vulkan is a
//     first-class, user-selectable backend end to end.

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

bool testVulkanBackendSelection()
{
    const bool available = Canvas::isVulkanAvailable();

    if (!available) {
        // Contract when Vulkan is unavailable: creation returns null so callers
        // can fall back to another backend.
        std::unique_ptr<Canvas> canvas = Canvas::createVulkan(32, 32);
        bool ok = expect(canvas == nullptr,
                         "createVulkan should return null when Vulkan is unavailable");
        std::cout << "Vulkan unavailable in this build: graceful fallback contract verified.\n";
        return ok;
    }

    const int w = 64;
    const int h = 64;
    std::unique_ptr<Canvas> canvas = Canvas::createVulkan(w, h);
    bool ok = expect(canvas != nullptr, "createVulkan should return a canvas when Vulkan is available");
    if (!canvas) {
        return false;
    }
    ok = expect(canvas->getWidth() == w && canvas->getHeight() == h,
                "Vulkan canvas should keep its requested size") && ok;

    canvas->beginFrame();
    Paint fill;
    fill.setStyle(Paint::Style::FILL);
    fill.setColor(Color(255, 0, 0, 255));
    fill.setAntiAlias(false);
    canvas->drawRect(RectF(16.0f, 16.0f, 32.0f, 32.0f), fill);
    canvas->flush();

    std::vector<unsigned char> pixels;
    ok = expect(canvas->readPixelsRGBA(pixels) && pixels.size() == static_cast<std::size_t>(w) * h * 4u,
                "readPixelsRGBA should succeed on the Vulkan canvas") && ok;
    if (!ok) {
        return false;
    }

    auto pixelAt = [&](int x, int y) { return &pixels[(static_cast<std::size_t>(y) * w + x) * 4u]; };
    const unsigned char *center = pixelAt(32, 32);
    ok = expect(center[0] > 200 && center[1] < 40 && center[2] < 40 && center[3] > 200,
                "Vulkan rect interior should be opaque red") && ok;
    std::cout << "Vulkan backend selected and rendered a solid fill off-screen.\n";
    return ok;
}

} // namespace

int main()
{
    return testVulkanBackendSelection() ? 0 : 1;
}

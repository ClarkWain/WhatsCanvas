// Metal on-screen presentation example (feat/metal-backend stage 2).
//
// Uses the Canvas `setOutputTarget(ToWindow(...))` + `present()` seam to
// exercise the MetalSwapchain end-to-end: the swapchain owns the
// CAMetalLayer bound to the NSWindow's contentView, and each present()
// blit-copies the offscreen frame into layer.nextDrawable and swaps.
//
// Set WHATSCANVAS_MAX_FRAMES=N in the environment to render N frames and
// exit (useful for automated verification / CI).

#define GLFW_INCLUDE_NONE
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#import <AppKit/AppKit.h>
#import <Metal/Metal.h>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>

#include <wsc/wsc.h>

using namespace wsc;

namespace {

void drawFrame(Canvas &canvas, float t)
{
    canvas.beginFrame();
    canvas.drawColor(Color(24, 26, 34));

    Paint fill;
    fill.setStyle(Paint::Style::FILL);
    fill.setAntiAlias(true);
    const float cx = 400.0f + std::cos(t) * 120.0f;
    const float cy = 300.0f + std::sin(t * 1.3f) * 90.0f;
    fill.setLinearGradient(cx - 120.0f, cy, cx + 120.0f, cy,
                           Color(255, 140, 60, 255), Color(80, 60, 255, 255));
    canvas.drawRect(RectF(cx - 120.0f, cy - 80.0f, cx + 120.0f, cy + 80.0f), fill);

    Paint text;
    text.setColor(Color(240, 240, 240, 255));
    text.setTextSize(28.0f);
    canvas.drawText("WhatsCanvas · Metal", 30.0f, 50.0f, text);

    canvas.endFrame();
}

} // namespace

int main()
{
    if (!Canvas::isBackendAvailable(Canvas::Backend::Metal)) {
        std::cout << "[MetalPresent] Metal not available on this build; skipping." << std::endl;
        return 0;
    }

    if (!glfwInit()) {
        std::cerr << "[MetalPresent] FAIL: glfwInit failed." << std::endl;
        return 1;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    const int width = 800;
    const int height = 600;
    GLFWwindow *window = glfwCreateWindow(width, height, "WhatsCanvas · Metal Present", nullptr, nullptr);
    if (window == nullptr) {
        std::cerr << "[MetalPresent] FAIL: could not create window." << std::endl;
        glfwTerminate();
        return 1;
    }

    int drawableWidth = width;
    int drawableHeight = height;
    glfwGetFramebufferSize(window, &drawableWidth, &drawableHeight);
    if (drawableWidth <= 0) drawableWidth = width;
    if (drawableHeight <= 0) drawableHeight = height;

    auto canvas = Canvas::create(Canvas::Backend::Metal, drawableWidth, drawableHeight);
    if (!canvas) {
        std::cerr << "[MetalPresent] FAIL: create(Metal) returned null." << std::endl;
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    canvas->initializeContext();

    // Hand off the NSWindow's contentView to the swapchain — MetalRenderDevice
    // installs a CAMetalLayer on it and drives present() from the offscreen
    // MTLTexture the Canvas already renders into.
    NSWindow *nsWindow = glfwGetCocoaWindow(window);
    NSView *contentView = nsWindow.contentView;
    contentView.wantsLayer = YES;

    NativeSurface surface;
    surface.platform = NativeSurface::Platform::Cocoa;
    surface.window = (__bridge void *)contentView;
    if (!canvas->setOutputTarget(OutputTarget::ToWindow(surface))) {
        std::cerr << "[MetalPresent] FAIL: setOutputTarget(Window) failed." << std::endl;
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    canvas->resizeOutput(drawableWidth, drawableHeight);
    id<MTLDevice> device = (__bridge id<MTLDevice>)canvas->metalDevice();
    std::cout << "[MetalPresent] Presenting on device: "
              << (device ? [device.name UTF8String] : "unknown") << std::endl;

    int maxFrames = 0;
    if (const char *env = std::getenv("WHATSCANVAS_MAX_FRAMES")) {
        maxFrames = std::atoi(env);
    }

    const double start = glfwGetTime();
    int frame = 0;
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        const float t = static_cast<float>(glfwGetTime() - start);
        drawFrame(*canvas, t);
        canvas->present();
        ++frame;
        if (maxFrames > 0 && frame >= maxFrames) {
            break;
        }
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    std::cout << "[MetalPresent] Rendered " << frame << " frames." << std::endl;
    return 0;
}

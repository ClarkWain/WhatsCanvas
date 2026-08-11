// Metal on-screen presentation example (feat/metal-backend stage 2).
//
// Renders an animated frame with the WhatsCanvas Metal backend off-screen,
// then blit-copies the result into a CAMetalLayer drawable and presents. Set
// WHATSCANVAS_MAX_FRAMES=N in the environment to render N frames and exit
// (useful for automated verification / CI).

#define GLFW_INCLUDE_NONE
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <AppKit/AppKit.h>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>

#include <wsc/wsc.h>

using namespace wsc;

namespace {

CAMetalLayer *installMetalLayer(GLFWwindow *window, id<MTLDevice> device,
                                int drawableWidth, int drawableHeight)
{
    NSWindow *nsWindow = glfwGetCocoaWindow(window);
    if (nsWindow == nil) {
        return nil;
    }
    NSView *view = nsWindow.contentView;
    view.wantsLayer = YES;

    CAMetalLayer *layer = [CAMetalLayer layer];
    layer.device = device;
    layer.pixelFormat = MTLPixelFormatRGBA8Unorm;
    layer.framebufferOnly = NO; // must allow blit destination
    layer.drawableSize = CGSizeMake(drawableWidth, drawableHeight);
    view.layer = layer;
    return layer;
}

void blitAndPresent(id<MTLDevice> device, id<MTLCommandQueue> queue,
                    CAMetalLayer *layer, id<MTLTexture> src)
{
    if (device == nil || queue == nil || layer == nil || src == nil) {
        return;
    }
    @autoreleasepool {
        id<CAMetalDrawable> drawable = [layer nextDrawable];
        if (drawable == nil) {
            return;
        }
        id<MTLTexture> dst = drawable.texture;

        id<MTLCommandBuffer> cb = [queue commandBuffer];
        id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
        MTLSize copySize = MTLSizeMake(std::min<NSUInteger>(src.width, dst.width),
                                       std::min<NSUInteger>(src.height, dst.height), 1);
        [blit copyFromTexture:src
                  sourceSlice:0
                  sourceLevel:0
                 sourceOrigin:MTLOriginMake(0, 0, 0)
                    sourceSize:copySize
                    toTexture:dst
             destinationSlice:0
             destinationLevel:0
            destinationOrigin:MTLOriginMake(0, 0, 0)];
        [blit endEncoding];
        [cb presentDrawable:drawable];
        [cb commit];
    }
}

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

    id<MTLDevice> device = (__bridge id<MTLDevice>)(canvas->metalDevice());
    id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)(canvas->metalCommandQueue());
    if (device == nil || queue == nil) {
        std::cerr << "[MetalPresent] FAIL: canvas has no Metal device/queue." << std::endl;
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    CAMetalLayer *layer = installMetalLayer(window, device, drawableWidth, drawableHeight);
    if (layer == nil) {
        std::cerr << "[MetalPresent] FAIL: could not install CAMetalLayer." << std::endl;
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    std::cout << "[MetalPresent] Presenting on device: " << [layer.device.name UTF8String] << std::endl;

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
        id<MTLTexture> src = (__bridge id<MTLTexture>)(canvas->metalLastRenderedTexture());
        blitAndPresent(device, queue, layer, src);
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

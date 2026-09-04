# Chapter 9: Windowed Presentation and Interaction

> Goal of this chapter: display rendered frames on a window using the OpenGL / Vulkan / Metal / Software backends, set up a frame loop, handle window resize, and process user input.

For the Chinese version, see [`zh/09-windowed-presentation.md`](./zh/09-windowed-presentation.md).

---

## 9.1 From Offscreen to Windowed

Earlier chapters used the Software backend for offscreen rendering. This chapter enters the "real-time rendering" territory — drawing every frame to a window on screen.

Key concepts for windowed presentation:

```
Create window → Create Canvas → Set OutputTarget → Frame loop:
    beginFrame() → draw → endFrame() → present()
```

---

## 9.2 OpenGL + GLFW Window (Most Common Setup)

GLFW is a cross-platform window and OpenGL context management library. Every WhatsCanvas desktop example uses it.

### Complete Example

```cpp
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <wsc/wsc.h>
#include <cmath>

#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <GLFW/glfw3native.h>
#endif

using namespace wsc;

int main()
{
    // 1. Initialize GLFW
    if (!glfwInit()) return 1;

    // 2. Request an OpenGL version
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);  // MSAA

    // 3. Create the window
    int width = 800, height = 600;
    GLFWwindow* window = glfwCreateWindow(width, height,
        "WhatsCanvas Window", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }

    // 4. Set the OpenGL context
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);  // VSync

    // 5. Load OpenGL function pointers
    if (!Canvas::loadOpenGL(
            reinterpret_cast<Canvas::OpenGLProcAddress>(glfwGetProcAddress))) {
        glfwTerminate();
        return 1;
    }

    // 6. Create the Canvas
    auto canvas = Canvas::create(Canvas::Backend::OpenGL, width, height);
    canvas->initializeContext();

    // 7. Set the window as the output target (optional; lets Canvas manage the swap)
    NativeSurface surface;
#if defined(_WIN32)
    surface.platform = NativeSurface::Platform::Win32;
    surface.window = glfwGetWin32Window(window);
#endif
    bool useCanvasPresent = canvas->setOutputTarget(
        OutputTarget::ToWindow(surface));

    // 8. Frame loop
    double startTime = glfwGetTime();
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // Handle resize
        int fbw, fbh;
        glfwGetFramebufferSize(window, &fbw, &fbh);
        if (fbw != canvas->getWidth() || fbh != canvas->getHeight()) {
            canvas->setSize(fbw, fbh);
            if (useCanvasPresent) canvas->resizeOutput(fbw, fbh);
        }

        float t = static_cast<float>(glfwGetTime() - startTime);
        float w = static_cast<float>(canvas->getWidth());
        float h = static_cast<float>(canvas->getHeight());

        // Begin the frame
        canvas->beginFrame();

        // Background
        Paint bg;
        bg.setLinearGradient(0, 0, w, h,
            Color(24, 26, 34), Color(40, 44, 60));
        canvas->drawRect(RectF(0, 0, w, h), bg);

        // Animated circle
        float cx = w * 0.5f + std::cos(t) * 150.0f;
        float cy = h * 0.5f + std::sin(t * 1.3f) * 100.0f;
        Paint circle;
        circle.setAntiAlias(true);
        circle.setRadialGradient(cx, cy, 80.0f,
            Color(120, 200, 255), Color(40, 90, 160));
        circle.setShadowLayer(20, 0, 8, Color(0, 0, 0, 120));
        canvas->drawCircle(cx, cy, 80, circle);

        // Rotating rectangle
        canvas->save();
        canvas->translate(w * 0.5f, h * 0.5f);
        canvas->rotate(t * 0.7f);
        Paint box;
        box.setAntiAlias(true);
        box.setColor(Color(255, 180, 80));
        canvas->drawRoundRect(RectF(-60, -40, 120, 80), 16, box);
        canvas->restore();

        // End the frame
        canvas->endFrame();

        // Present
        if (useCanvasPresent) {
            canvas->present();
        } else {
            glfwSwapBuffers(window);
        }
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
```

---

## 9.3 Vulkan Windowed Presentation

The Vulkan backend fully self-manages rendering resources; no external GL context is required:

```cpp
// Vulkan window: no GL
glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

GLFWwindow* window = glfwCreateWindow(800, 600,
    "WhatsCanvas - Vulkan", nullptr, nullptr);

// Check Vulkan availability
if (!Canvas::isBackendAvailable(Canvas::Backend::Vulkan)) {
    // fall back to another backend
}

// Create the Vulkan Canvas
auto canvas = Canvas::create(Canvas::Backend::Vulkan, 800, 600);
canvas->initializeContext();

// Set up window presentation
NativeSurface surface;
#if defined(_WIN32)
surface.platform = NativeSurface::Platform::Win32;
surface.window = glfwGetWin32Window(window);
#endif
canvas->setOutputTarget(OutputTarget::ToWindow(surface));

// The frame loop is identical to OpenGL
while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();
    canvas->beginFrame();
    // ... draw ...
    canvas->endFrame();
    canvas->present();
}
```

---

## 9.4 Metal Windowed Presentation (macOS)

```objcpp
// macOS: use Metal + CAMetalLayer
glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

GLFWwindow* window = glfwCreateWindow(800, 600,
    "WhatsCanvas - Metal", nullptr, nullptr);

auto canvas = Canvas::create(Canvas::Backend::Metal, 800, 600);
canvas->initializeContext();

// Grab the NSWindow's contentView
NSWindow* nsWindow = glfwGetCocoaWindow(window);
NSView* contentView = nsWindow.contentView;
contentView.wantsLayer = YES;

NativeSurface surface;
surface.platform = NativeSurface::Platform::Cocoa;
surface.window = (__bridge void*)contentView;
canvas->setOutputTarget(OutputTarget::ToWindow(surface));

// Frame loop as above
```

---

## 9.5 Software Windowed Presentation (Windows GDI)

Pure CPU rendering can also present to a window (via a GDI blit):

```cpp
glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);  // No GL context needed

auto canvas = Canvas::create(Canvas::Backend::Software, 800, 600);
canvas->initializeContext();

NativeSurface surface;
surface.platform = NativeSurface::Platform::Win32;
surface.window = glfwGetWin32Window(window);
canvas->setOutputTarget(OutputTarget::ToWindow(surface));

// Frame loop
while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();
    canvas->beginFrame();
    // ... draw ...
    canvas->endFrame();
    canvas->present();  // GDI blit
}
```

> **Note**: Software windowed presentation currently supports Windows only.

---

## 9.6 OutputTarget Summary

```cpp
// Offscreen rendering (default) — get pixels via readPixelsRGBA
OutputTarget::Offscreen();

// Offscreen texture — result stored in a GPU texture
OutputTarget::OffscreenTexture();

// Windowed presentation — render to a window
OutputTarget::ToWindow(surface, swapchainConfig);

// Wrap an existing GL framebuffer
OutputTarget::GLFramebuffer(fbo, width, height, opaque);

// Wrap a Vulkan image
OutputTarget::VulkanImageTarget(vkImage, vkFormat, width, height);
```

---

## 9.7 Handling Window Resize

```cpp
// Detect size changes in the frame loop
int fbw, fbh;
glfwGetFramebufferSize(window, &fbw, &fbh);
float scaleX, scaleY;
glfwGetWindowContentScale(window, &scaleX, &scaleY);
const float dpr = scaleX > 0.0f ? scaleX : 1.0f;

if (fbw != canvas->getWidth() || fbh != canvas->getHeight()
    || std::abs(dpr - canvas->devicePixelRatio()) > 0.001f) {
    canvas->setSize(fbw, fbh);
    canvas->resizeOutput(fbw, fbh);  // Notify the swapchain
    canvas->setDevicePixelRatio(dpr);

    const float logicalWidth = fbw / dpr;
    const float logicalHeight = fbh / dpr;
    updateLayout(logicalWidth, logicalHeight);
}
```

---

## 9.8 Handling User Input

GLFW routes input events through callbacks. Combine them with WhatsCanvas hit-testing for interaction.

### Keyboard Input

```cpp
void keyCallback(GLFWwindow* window, int key, int scancode,
                 int action, int mods)
{
    if (action == GLFW_PRESS) {
        switch (key) {
            case GLFW_KEY_ESCAPE:
                glfwSetWindowShouldClose(window, true);
                break;
            case GLFW_KEY_SPACE:
                // Trigger some action
                break;
        }
    }
}

// Register the callback
glfwSetKeyCallback(window, keyCallback);
```

### Mouse Click + Path Hit-Testing

```cpp
struct AppState {
    Canvas* canvas;
    Path buttonPath;
    bool buttonHovered = false;
};

void mouseCallback(GLFWwindow* window, int button, int action, int mods)
{
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        double mx, my;
        glfwGetCursorPos(window, &mx, &my);

        auto* state = static_cast<AppState*>(glfwGetWindowUserPointer(window));

        // Use Path's hit-testing
        if (state->buttonPath.contains(mx, my)) {
            // Button clicked!
        }

        // Or use the Canvas hit-test (accounts for the current transform)
        if (state->canvas->hitTestPathFill(
                state->buttonPath, PointF(mx, my))) {
            // Hit in the transformed coordinate space
        }
    }
}

void cursorCallback(GLFWwindow* window, double xpos, double ypos)
{
    auto* state = static_cast<AppState*>(glfwGetWindowUserPointer(window));
    state->buttonHovered = state->buttonPath.contains(xpos, ypos);
}

glfwSetMouseButtonCallback(window, mouseCallback);
glfwSetCursorPosCallback(window, cursorCallback);
glfwSetWindowUserPointer(window, &appState);
```

---

## 9.9 HiDPI Support

```cpp
// Content scale
float scaleX, scaleY;
glfwGetWindowContentScale(window, &scaleX, &scaleY);
const float dpr = scaleX > 0.0f ? scaleX : 1.0f;

// Framebuffer vs logical dimensions
int fbW, fbH;       // Physical pixels
glfwGetFramebufferSize(window, &fbW, &fbH);

// Logical size = physical / DPR
float logicalW = fbW / dpr;
float logicalH = fbH / dpr;

// Canvas uses the physical size
canvas->setSize(fbW, fbH);
canvas->setDevicePixelRatio(dpr);
// Drawing code uses logical coordinates (Canvas scales internally by DPR)
```

`getWidth()` / `getHeight()` return the physical framebuffer size. Page layout should keep `logicalW` / `logicalH` around; after setting a DPR do not keep computing centers with the physical size. GLFW cursor callbacks report window content coordinates, usually already in the logical space; if your input source uses physical pixels, divide by the DPR before hit-testing.

---

## 9.10 Frame Rate Control and Performance Stats

### VSync

```cpp
glfwSwapInterval(1);  // Enable VSync (cap at display refresh rate)
glfwSwapInterval(0);  // Disable VSync (uncapped)
```

### SwapchainConfig

```cpp
SwapchainConfig config;
config.vsync = true;
config.imageCount = 3;  // Triple buffering

canvas->setOutputTarget(OutputTarget::ToWindow(surface, config));
```

### Render Stats

```cpp
canvas->setGpuTimingEnabled(true);

// Retrieve stats each frame
auto stats = canvas->getRenderStats();
// stats contains frame time, draw call count, texture memory usage, ...
```

---

## 9.11 Full Interactive Example: A Draggable Ball

```cpp
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <wsc/wsc.h>
#include <cmath>

using namespace wsc;

struct Ball {
    float x = 200, y = 200, radius = 40;
    bool dragging = false;
    float dragOffX = 0, dragOffY = 0;
};

Ball ball;

void mouseButton(GLFWwindow*, int button, int action, int) {
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;
    double mx, my;
    glfwGetCursorPos(glfwGetCurrentContext(), &mx, &my);

    if (action == GLFW_PRESS) {
        float dx = mx - ball.x, dy = my - ball.y;
        if (dx*dx + dy*dy <= ball.radius * ball.radius) {
            ball.dragging = true;
            ball.dragOffX = dx;
            ball.dragOffY = dy;
        }
    } else {
        ball.dragging = false;
    }
}

void cursorPos(GLFWwindow*, double x, double y) {
    if (ball.dragging) {
        ball.x = x - ball.dragOffX;
        ball.y = y - ball.dragOffY;
    }
}

int main() {
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    auto* window = glfwCreateWindow(600, 400, "Drag Demo", nullptr, nullptr);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    Canvas::loadOpenGL(reinterpret_cast<Canvas::OpenGLProcAddress>(
        glfwGetProcAddress));

    auto canvas = Canvas::create(Canvas::Backend::OpenGL, 600, 400);
    canvas->initializeContext();

    glfwSetMouseButtonCallback(window, mouseButton);
    glfwSetCursorPosCallback(window, cursorPos);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        canvas->beginFrame();
        canvas->drawColor(Color(245, 245, 245));

        // Shadow
        canvas->drawBoxShadow(
            RectF(ball.x - ball.radius, ball.y - ball.radius,
                  ball.radius * 2, ball.radius * 2),
            ball.radius, 0, 16, 0, 6, Color(0, 0, 0, 60));

        // Ball
        Paint p;
        p.setAntiAlias(true);
        p.setRadialGradient(ball.x - 10, ball.y - 10, ball.radius,
            Color(100, 200, 255), Color(30, 100, 200));
        canvas->drawCircle(ball.x, ball.y, ball.radius, p);

        // Hint text
        Paint text;
        text.setColor(Color(100, 100, 100));
        text.setTextSize(14.0f);
        text.setTextAlign(Paint::TextAlign::CENTER);
        canvas->drawText("Drag the ball!", 300, 380, text);

        canvas->endFrame();
        glfwSwapBuffers(window);
    }

    glfwTerminate();
    return 0;
}
```

---

## 9.12 Summary

This chapter covered:

- [x] Window creation and frame loop with OpenGL + GLFW
- [x] Vulkan / Metal / Software windowed presentation
- [x] The OutputTarget variants
- [x] Handling window resize
- [x] User input (keyboard / mouse) + hit-testing
- [x] HiDPI support
- [x] Frame rate control and performance stats

**Next chapter**: [Multiple Backends and Fallback](./10-multi-backend.md) — backend selection strategies and runtime downgrade.

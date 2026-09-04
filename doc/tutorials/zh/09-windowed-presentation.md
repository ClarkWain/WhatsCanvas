# 第九章：窗口呈现与交互

> 本章目标：学会使用 OpenGL/Vulkan/Metal/Software 后端将渲染结果显示到窗口，建立帧循环，处理窗口缩放和用户输入。

---

## 9.1 从离屏到窗口

前面的章节都使用 Software 后端进行离屏渲染。本章进入"实时渲染"领域——将每一帧绘制到屏幕窗口上。

窗口呈现的关键概念：

```
创建窗口 → 创建 Canvas → 设置 OutputTarget → 帧循环:
    beginFrame() → 绘制 → endFrame() → present()
```

---

## 9.2 OpenGL + GLFW 窗口（最常见方案）

GLFW 是一个跨平台的窗口和 OpenGL 上下文管理库，WhatsCanvas 的所有桌面示例都使用它。

### 完整示例

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
    // 1. 初始化 GLFW
    if (!glfwInit()) return 1;

    // 2. 设置 OpenGL 版本要求
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);  // MSAA

    // 3. 创建窗口
    int width = 800, height = 600;
    GLFWwindow* window = glfwCreateWindow(width, height,
        "WhatsCanvas Window", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }

    // 4. 设置 OpenGL 上下文
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);  // VSync

    // 5. 加载 OpenGL 函数指针
    if (!Canvas::loadOpenGL(
            reinterpret_cast<Canvas::OpenGLProcAddress>(glfwGetProcAddress))) {
        glfwTerminate();
        return 1;
    }

    // 6. 创建 Canvas
    auto canvas = Canvas::create(Canvas::Backend::OpenGL, width, height);
    canvas->initializeContext();

    // 7. 设置窗口呈现目标（可选，用于 Canvas 自管理 swap）
    NativeSurface surface;
#if defined(_WIN32)
    surface.platform = NativeSurface::Platform::Win32;
    surface.window = glfwGetWin32Window(window);
#endif
    bool useCanvasPresent = canvas->setOutputTarget(
        OutputTarget::ToWindow(surface));

    // 8. 帧循环
    double startTime = glfwGetTime();
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // 处理窗口大小变化
        int fbw, fbh;
        glfwGetFramebufferSize(window, &fbw, &fbh);
        if (fbw != canvas->getWidth() || fbh != canvas->getHeight()) {
            canvas->setSize(fbw, fbh);
            if (useCanvasPresent) canvas->resizeOutput(fbw, fbh);
        }

        float t = static_cast<float>(glfwGetTime() - startTime);
        float w = static_cast<float>(canvas->getWidth());
        float h = static_cast<float>(canvas->getHeight());

        // 开始帧
        canvas->beginFrame();

        // 绘制背景
        Paint bg;
        bg.setLinearGradient(0, 0, w, h,
            Color(24, 26, 34), Color(40, 44, 60));
        canvas->drawRect(RectF(0, 0, w, h), bg);

        // 动画圆形
        float cx = w * 0.5f + std::cos(t) * 150.0f;
        float cy = h * 0.5f + std::sin(t * 1.3f) * 100.0f;
        Paint circle;
        circle.setAntiAlias(true);
        circle.setRadialGradient(cx, cy, 80.0f,
            Color(120, 200, 255), Color(40, 90, 160));
        circle.setShadowLayer(20, 0, 8, Color(0, 0, 0, 120));
        canvas->drawCircle(cx, cy, 80, circle);

        // 旋转矩形
        canvas->save();
        canvas->translate(w * 0.5f, h * 0.5f);
        canvas->rotate(t * 0.7f);
        Paint box;
        box.setAntiAlias(true);
        box.setColor(Color(255, 180, 80));
        canvas->drawRoundRect(RectF(-60, -40, 120, 80), 16, box);
        canvas->restore();

        // 结束帧
        canvas->endFrame();

        // 呈现
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

## 9.3 Vulkan 窗口呈现

Vulkan 后端完全自管理渲染资源，不需要外部 GL 上下文：

```cpp
// Vulkan 窗口：不使用 GL
glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

GLFWwindow* window = glfwCreateWindow(800, 600,
    "WhatsCanvas - Vulkan", nullptr, nullptr);

// 检查 Vulkan 可用性
if (!Canvas::isBackendAvailable(Canvas::Backend::Vulkan)) {
    // fallback 到其他后端
}

// 创建 Vulkan Canvas
auto canvas = Canvas::create(Canvas::Backend::Vulkan, 800, 600);
canvas->initializeContext();

// 设置窗口呈现
NativeSurface surface;
#if defined(_WIN32)
surface.platform = NativeSurface::Platform::Win32;
surface.window = glfwGetWin32Window(window);
#endif
canvas->setOutputTarget(OutputTarget::ToWindow(surface));

// 帧循环与 OpenGL 完全一致
while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();
    canvas->beginFrame();
    // ... 绘制 ...
    canvas->endFrame();
    canvas->present();
}
```

---

## 9.4 Metal 窗口呈现 (macOS)

```objcpp
// macOS: 使用 Metal + CAMetalLayer
glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

GLFWwindow* window = glfwCreateWindow(800, 600,
    "WhatsCanvas - Metal", nullptr, nullptr);

auto canvas = Canvas::create(Canvas::Backend::Metal, 800, 600);
canvas->initializeContext();

// 获取 NSWindow 的 contentView
NSWindow* nsWindow = glfwGetCocoaWindow(window);
NSView* contentView = nsWindow.contentView;
contentView.wantsLayer = YES;

NativeSurface surface;
surface.platform = NativeSurface::Platform::Cocoa;
surface.window = (__bridge void*)contentView;
canvas->setOutputTarget(OutputTarget::ToWindow(surface));

// 帧循环同上
```

---

## 9.5 Software 窗口呈现 (Windows GDI)

纯 CPU 渲染也可以显示到窗口（通过 GDI blit）：

```cpp
glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);  // 无需 GL 上下文

auto canvas = Canvas::create(Canvas::Backend::Software, 800, 600);
canvas->initializeContext();

NativeSurface surface;
surface.platform = NativeSurface::Platform::Win32;
surface.window = glfwGetWin32Window(window);
canvas->setOutputTarget(OutputTarget::ToWindow(surface));

// 帧循环
while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();
    canvas->beginFrame();
    // ... 绘制 ...
    canvas->endFrame();
    canvas->present();  // GDI blit
}
```

> **注意**：Software 窗口呈现目前仅支持 Windows。

---

## 9.6 OutputTarget 类型汇总

```cpp
// 离屏渲染（默认）— 使用 readPixelsRGBA 获取结果
OutputTarget::Offscreen();

// 离屏纹理 — 结果存在 GPU 纹理中
OutputTarget::OffscreenTexture();

// 窗口呈现 — 渲染到窗口
OutputTarget::ToWindow(surface, swapchainConfig);

// 包装现有 GL Framebuffer
OutputTarget::GLFramebuffer(fbo, width, height, opaque);

// 包装 Vulkan Image
OutputTarget::VulkanImageTarget(vkImage, vkFormat, width, height);
```

---

## 9.7 处理窗口大小变化

```cpp
// 在帧循环中检测大小变化
int fbw, fbh;
glfwGetFramebufferSize(window, &fbw, &fbh);
float scaleX, scaleY;
glfwGetWindowContentScale(window, &scaleX, &scaleY);
const float dpr = scaleX > 0.0f ? scaleX : 1.0f;

if (fbw != canvas->getWidth() || fbh != canvas->getHeight()
    || std::abs(dpr - canvas->devicePixelRatio()) > 0.001f) {
    canvas->setSize(fbw, fbh);
    canvas->resizeOutput(fbw, fbh);  // 通知 swapchain 更新
    canvas->setDevicePixelRatio(dpr);

    const float logicalWidth = fbw / dpr;
    const float logicalHeight = fbh / dpr;
    updateLayout(logicalWidth, logicalHeight);
}
```

---

## 9.8 处理用户输入

GLFW 通过回调函数处理输入事件。配合 WhatsCanvas 的 hit-test 可以实现交互：

### 键盘输入

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
                // 触发某个动作
                break;
        }
    }
}

// 注册回调
glfwSetKeyCallback(window, keyCallback);
```

### 鼠标点击 + Path Hit-Testing

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

        // 使用 Path 的 hit-testing
        if (state->buttonPath.contains(mx, my)) {
            // 按钮被点击！
        }

        // 或使用 Canvas 的 hit-test（考虑变换）
        if (state->canvas->hitTestPathFill(
                state->buttonPath, PointF(mx, my))) {
            // 在变换后的坐标系中命中
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

## 9.9 HiDPI 支持

```cpp
// 获取内容缩放比
float scaleX, scaleY;
glfwGetWindowContentScale(window, &scaleX, &scaleY);
const float dpr = scaleX > 0.0f ? scaleX : 1.0f;

// 帧缓冲尺寸 vs 逻辑尺寸
int fbW, fbH;       // 物理像素
glfwGetFramebufferSize(window, &fbW, &fbH);

// 逻辑尺寸 = 物理尺寸 / DPR
float logicalW = fbW / dpr;
float logicalH = fbH / dpr;

// Canvas 使用物理尺寸
canvas->setSize(fbW, fbH);
canvas->setDevicePixelRatio(dpr);
// 绘制代码使用逻辑坐标（Canvas 内部根据 DPR 缩放）
```

`getWidth()` / `getHeight()` 返回 framebuffer 的物理尺寸。页面布局应保存 `logicalW` / `logicalH`，不能在设置 DPR 后继续用物理尺寸计算居中位置。GLFW 光标回调给出窗口内容坐标，通常已经处于逻辑坐标系；如果输入来自物理像素坐标，则先除以 DPR 再做 hit-testing。

---

## 9.10 帧率控制与性能统计

### VSync

```cpp
glfwSwapInterval(1);  // 启用 VSync（限制到显示器刷新率）
glfwSwapInterval(0);  // 关闭 VSync（尽可能快）
```

### SwapchainConfig

```cpp
SwapchainConfig config;
config.vsync = true;
config.imageCount = 3;  // Triple buffering

canvas->setOutputTarget(OutputTarget::ToWindow(surface, config));
```

### 渲染统计

```cpp
canvas->setGpuTimingEnabled(true);

// 每帧获取统计数据
auto stats = canvas->getRenderStats();
// stats 包含帧时间、draw call 数量、纹理内存等信息
```

---

## 9.11 完整交互示例：可拖动的圆

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

        // 阴影
        canvas->drawBoxShadow(
            RectF(ball.x - ball.radius, ball.y - ball.radius,
                  ball.radius * 2, ball.radius * 2),
            ball.radius, 0, 16, 0, 6, Color(0, 0, 0, 60));

        // 球
        Paint p;
        p.setAntiAlias(true);
        p.setRadialGradient(ball.x - 10, ball.y - 10, ball.radius,
            Color(100, 200, 255), Color(30, 100, 200));
        canvas->drawCircle(ball.x, ball.y, ball.radius, p);

        // 提示文字
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

## 9.12 小结

本章学习了：

- [x] OpenGL + GLFW 窗口创建和帧循环
- [x] Vulkan / Metal / Software 窗口呈现
- [x] OutputTarget 的各种类型
- [x] 窗口大小变化处理
- [x] 用户输入（键盘/鼠标）+ Hit-Testing
- [x] HiDPI 支持
- [x] 帧率控制与性能统计

**下一章**：[多后端与 Fallback](./10-multi-backend.md) —— 学习多后端切换策略和运行时降级。

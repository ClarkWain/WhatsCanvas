# 第十二章：跨平台实战

> 本章目标：学习将 WhatsCanvas 集成到 Android（JNI + GLSurfaceView）、iOS（Metal + CoreText）和 Web（Emscripten + WebGL 2）平台的完整流程与要点。

---

## 12.1 跨平台架构概述

WhatsCanvas 的跨平台设计遵循"库只负责渲染"原则：

```
┌─────────────────────────────────────────────┐
│              应用逻辑 (C++17)                │
├─────────────────────────────────────────────┤
│           WhatsCanvas API (wsc::)           │
├──────┬──────┬──────┬──────┬─────────────────┤
│  SW  │  GL  │ GLES │  VK  │     Metal       │
├──────┴──────┴──────┴──────┴─────────────────┤
│          平台宿主 (Host)                     │
│  Desktop: GLFW                              │
│  Android: GLSurfaceView + JNI              │
│  iOS: UIKit + CAMetalLayer                  │
│  Web: Emscripten + WebGL 2                  │
└─────────────────────────────────────────────┘
```

核心绘制代码写一份 C++，各平台只需实现"宿主"层（创建窗口/surface、管理生命周期、处理输入）。

---

## 12.2 Android 集成

### 概述

Android 通过 JNI 调用 C++ 渲染代码，使用 `GLSurfaceView` 提供 OpenGL ES 3.0 上下文。

### 项目结构

```
app/
├── src/main/
│   ├── java/.../MainActivity.java
│   ├── java/.../CanvasGLSurfaceView.java
│   ├── java/.../CanvasRenderer.java
│   └── cpp/
│       ├── CMakeLists.txt
│       ├── native_renderer.cpp
│       └── scene.cpp / scene.h
└── libs/
    └── whatscanvas-android-release-1.0.0.aar
```

### Gradle 配置

```groovy
// build.gradle (app)
android {
    defaultConfig {
        ndk {
            abiFilters 'armeabi-v7a', 'arm64-v8a', 'x86_64'
        }
    }
}

dependencies {
    implementation files('libs/whatscanvas-android-release-1.0.0.aar')
}
```

### CMakeLists.txt (JNI 层)

```cmake
cmake_minimum_required(VERSION 3.16)
project(myapp_native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

# Prefab AAR 提供的 WhatsCanvas
find_package(WhatsCanvas CONFIG REQUIRED)

add_library(myapp_native SHARED
    native_renderer.cpp
    scene.cpp
)

target_link_libraries(myapp_native PRIVATE
    WhatsCanvas::OpenGLES
    android
    log
    EGL
    GLESv3
)
```

### JNI 渲染代码

```cpp
// native_renderer.cpp
#include <jni.h>
#include <android/log.h>
#include <wsc/wsc.h>
#include <memory>

static std::unique_ptr<wsc::Canvas> g_canvas;

extern "C" {

JNIEXPORT void JNICALL
Java_com_example_CanvasRenderer_nativeInit(JNIEnv*, jobject, jint w, jint h)
{
    // Android GLSurfaceView 已创建好 GLES 上下文
    g_canvas = wsc::Canvas::create(wsc::Canvas::Backend::OpenGLES, w, h);
    if (g_canvas) {
        g_canvas->initializeContext();

        // 注册字体
        for (const auto& face : wsc::FontSystem::defaultSystemFontFaces()) {
            g_canvas->registerFontFace(face);
        }
        g_canvas->setFontFallbackChain(wsc::FontSystem::defaultFallbackChain());
    }
}

JNIEXPORT void JNICALL
Java_com_example_CanvasRenderer_nativeResize(JNIEnv*, jobject, jint w, jint h)
{
    if (g_canvas) {
        g_canvas->setSize(w, h);
    }
}

JNIEXPORT void JNICALL
Java_com_example_CanvasRenderer_nativeRender(JNIEnv*, jobject, jfloat time)
{
    if (!g_canvas) return;

    g_canvas->beginFrame();

    // 绘制内容（与桌面完全相同的代码）
    wsc::Paint bg;
    bg.setLinearGradient(0, 0, g_canvas->getWidth(), g_canvas->getHeight(),
        wsc::Color(30, 40, 60), wsc::Color(10, 15, 25));
    g_canvas->drawRect(wsc::RectF(0, 0,
        g_canvas->getWidth(), g_canvas->getHeight()), bg);

    // ... 更多绘制 ...

    g_canvas->endFrame();
}

JNIEXPORT void JNICALL
Java_com_example_CanvasRenderer_nativeDestroy(JNIEnv*, jobject)
{
    if (g_canvas) {
        g_canvas->finalizeContext();
        g_canvas.reset();
    }
}

}
```

### Java GLSurfaceView.Renderer

```java
public class CanvasRenderer implements GLSurfaceView.Renderer {
    private long startTime;

    @Override
    public void onSurfaceCreated(GL10 gl, EGLConfig config) {
        startTime = System.nanoTime();
    }

    @Override
    public void onSurfaceChanged(GL10 gl, int width, int height) {
        nativeInit(width, height);
        nativeResize(width, height);
    }

    @Override
    public void onDrawFrame(GL10 gl) {
        float elapsed = (System.nanoTime() - startTime) / 1_000_000_000f;
        nativeRender(elapsed);
    }

    private native void nativeInit(int width, int height);
    private native void nativeResize(int width, int height);
    private native void nativeRender(float time);
    private native void nativeDestroy();
}
```

### 要点

- AAR 内含 Prefab 元数据，Gradle 4.1+ 自动识别
- `GLSurfaceView` 在 GL 线程调用回调，Canvas 操作自然在正确线程
- 生命周期：`onPause` 时不要销毁 Canvas，`onSurfaceCreated` 可能重新调用
- 字体：Android 无 fontconfig，使用 WhatsCanvas 内置 FreeType + HarfBuzz

---

## 12.3 iOS 集成

### 概述

iOS 使用 Metal 后端 + CoreText 文本引擎，通过 XCFramework 分发。

### 项目结构

```
MyApp.xcodeproj/
├── MyApp/
│   ├── AppDelegate.swift
│   ├── ViewController.swift
│   ├── MetalCanvasView.swift
│   └── Bridging-Header.h
└── Frameworks/
    └── WhatsCanvas.xcframework/
```

### Xcode 配置

1. 将 `WhatsCanvas.xcframework` 拖入项目的 Frameworks
2. 设置 "Embed & Sign"
3. 在 Build Settings 中添加 Header Search Paths 指向 XCFramework 的 headers

### Metal 渲染视图

```swift
// MetalCanvasView.swift
import UIKit
import MetalKit

class MetalCanvasView: MTKView {
    private var canvas: UnsafeMutablePointer<WscCanvas>?
    private var startTime: CFTimeInterval = 0

    override func didMoveToWindow() {
        super.didMoveToWindow()
        guard let metalDevice = MTLCreateSystemDefaultDevice() else { return }
        self.device = metalDevice
        self.delegate = self
        setupCanvas()
    }

    private func setupCanvas() {
        let scale = UIScreen.main.scale
        let w = Int(bounds.width * scale)
        let h = Int(bounds.height * scale)

        // 通过 C++ 桥接创建 Metal Canvas
        canvas = wsc_canvas_create_metal(Int32(w), Int32(h))
        wsc_canvas_set_device_pixel_ratio(canvas, Float(scale))
        wsc_canvas_set_text_backend_coretext(canvas)
        startTime = CACurrentMediaTime()
    }
}

extension MetalCanvasView: MTKViewDelegate {
    func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {
        wsc_canvas_set_size(canvas, Int32(size.width), Int32(size.height))
    }

    func draw(in view: MTKView) {
        let elapsed = Float(CACurrentMediaTime() - startTime)
        wsc_canvas_begin_frame(canvas)
        // 绘制（通过 C 桥接调用 C++ 渲染代码）
        wsc_scene_render(canvas, elapsed)
        wsc_canvas_end_frame(canvas)
        wsc_canvas_present(canvas)
    }
}
```

### 要点

- 使用 `WhatsCanvas::Metal` target（独立，不依赖 OpenGL）
- 文本使用 CoreText 后端，无需 FreeType/HarfBuzz 依赖
- 支持 `CAMetalLayer` 窗口呈现
- 需要处理 orientation 变化和安全区域
- 真机和模拟器使用同一个 XCFramework（arm64 + x86_64 切片）

---

## 12.4 Web (Emscripten + WebGL 2)

### 概述

通过 Emscripten 将 C++ 代码编译为 WebAssembly，使用 OpenGL ES 路径（映射为 WebGL 2）。

### 编译命令

```bash
emcmake cmake -S . -B build-wasm \
    -DWHATSCANVAS_BUILD_OPENGLES=ON \
    -DWHATSCANVAS_BUILD_OPENGL=OFF \
    -DWHATSCANVAS_BUILD_SOFTWARE=OFF \
    -DWHATSCANVAS_BUILD_DEMO=OFF

emmake cmake --build build-wasm
```

### C++ 入口

```cpp
// wasm_main.cpp
#include <emscripten.h>
#include <emscripten/html5.h>
#include <wsc/wsc.h>
#include <algorithm>
#include <cmath>
#include <memory>

static std::unique_ptr<wsc::Canvas> g_canvas;
static double g_startTime = 0;
static int g_physicalWidth = 0;
static int g_physicalHeight = 0;
static float g_logicalWidth = 0.0f;
static float g_logicalHeight = 0.0f;
static float g_dpr = 1.0f;

bool resizeSurface() {
    double cssWidth = 0.0;
    double cssHeight = 0.0;
    if (emscripten_get_element_css_size(
            "#canvas", &cssWidth, &cssHeight) != EMSCRIPTEN_RESULT_SUCCESS) {
        return false;
    }

    const double rawDpr = emscripten_get_device_pixel_ratio();
    const float dpr = std::isfinite(rawDpr) && rawDpr > 0.0
        ? static_cast<float>(rawDpr) : 1.0f;
    const int physicalWidth = std::max(1, static_cast<int>(std::lround(cssWidth * dpr)));
    const int physicalHeight = std::max(1, static_cast<int>(std::lround(cssHeight * dpr)));

    if (physicalWidth == g_physicalWidth
        && physicalHeight == g_physicalHeight
        && std::abs(dpr - g_dpr) <= 0.001f) {
        return true;
    }

    emscripten_set_canvas_element_size("#canvas", physicalWidth, physicalHeight);
    g_physicalWidth = physicalWidth;
    g_physicalHeight = physicalHeight;
    g_logicalWidth = static_cast<float>(cssWidth);
    g_logicalHeight = static_cast<float>(cssHeight);
    g_dpr = dpr;

    if (g_canvas) {
        g_canvas->setSize(g_physicalWidth, g_physicalHeight);
        g_canvas->setDevicePixelRatio(g_dpr);
    }
    return true;
}

void mainLoop() {
    if (!resizeSurface()) return;

    double now = emscripten_get_now() / 1000.0;
    float elapsed = static_cast<float>(now - g_startTime);

    g_canvas->beginFrame();

    // 绘制内容
    wsc::Paint bg;
    bg.setLinearGradient(0, 0, g_logicalWidth, g_logicalHeight,
        wsc::Color(20, 30, 50), wsc::Color(60, 20, 80));
    g_canvas->drawRect(wsc::RectF(0, 0,
        g_logicalWidth, g_logicalHeight), bg);

    wsc::Paint text;
    text.setColor(wsc::Color::WHITE);
    text.setTextSize(32.0f);
    text.setTextAlign(wsc::Paint::TextAlign::CENTER);
    g_canvas->drawText("WhatsCanvas on Web!",
        g_logicalWidth / 2.0f, g_logicalHeight / 2.0f, text);

    g_canvas->endFrame();
}

int main() {
    // 创建 WebGL 2 上下文
    EmscriptenWebGLContextAttributes attrs;
    emscripten_webgl_init_context_attributes(&attrs);
    attrs.majorVersion = 2;
    attrs.minorVersion = 0;
    attrs.antialias = true;
    attrs.stencil = true;

    auto ctx = emscripten_webgl_create_context("#canvas", &attrs);
    emscripten_webgl_make_context_current(ctx);

    // CSS 尺寸是逻辑尺寸，drawing buffer 尺寸是 CSS 尺寸乘以 DPR。
    if (!resizeSurface()) return 1;

    // 创建 WhatsCanvas (OpenGL ES → WebGL 2)
    g_canvas = wsc::Canvas::create(
        wsc::Canvas::Backend::OpenGLES, g_physicalWidth, g_physicalHeight);
    g_canvas->setDevicePixelRatio(g_dpr);
    if (!g_canvas->initializeContext()) return 1;

    g_startTime = emscripten_get_now() / 1000.0;

    // 设置主循环
    emscripten_set_main_loop(mainLoop, 0, true);
    return 0;
}
```

### HTML 模板

```html
<!DOCTYPE html>
<html>
<head>
    <style>
        canvas { width: 100vw; height: 100vh; display: block; }
    </style>
</head>
<body>
    <canvas id="canvas"></canvas>
    <script src="whatscanvas_app.js"></script>
</body>
</html>
```

### 要点

- Emscripten 将 OpenGL ES 3.0 调用映射到 WebGL 2
- 使用 `emscripten_set_main_loop` 代替传统 while 循环
- CSS 尺寸用于逻辑布局，drawing buffer 按 `CSS 尺寸 × DPR` 分配
- DPR 通过 `emscripten_get_device_pixel_ratio()` 获取，并传给 `setDevicePixelRatio`
- `getWidth()` / `getHeight()` 是物理尺寸，不能用来计算设置 DPR 后的页面中心
- 字体需要预加载或从 URL 异步加载
- 尚无 WebGPU 后端和预编译 Web 发布包

---

## 12.5 共享绘制代码

跨平台的关键是**绘制代码只写一份**。推荐使用 Scene 接口模式：

```cpp
// scene.h — 跨平台共享
#pragma once
#include <wsc/wsc.h>

class IScene {
public:
    virtual ~IScene() = default;
    virtual void onInit(wsc::Canvas& canvas) = 0;
    virtual void onResize(wsc::Canvas& canvas, float w, float h) = 0;
    virtual void onFrame(wsc::Canvas& canvas, float elapsed) = 0;
    virtual void onDestroy() = 0;
};

// 具体 Scene 实现（纯 C++ 绘制逻辑）
class MyAppScene : public IScene {
public:
    void onInit(wsc::Canvas& canvas) override {
        // 注册字体、加载图片
    }

    void onResize(wsc::Canvas& canvas, float w, float h) override {
        width_ = w;
        height_ = h;
    }

    void onFrame(wsc::Canvas& canvas, float elapsed) override {
        // 全部绘制代码在这里
        // 在所有平台上执行相同的代码
    }

    void onDestroy() override {
        // 释放资源
    }

private:
    float width_ = 0, height_ = 0;
};
```

各平台宿主只需要：

```
Desktop: GlfwHost 调用 scene->onFrame()
Android: JNI 层转发 GLSurfaceView 回调
iOS:     MTKViewDelegate 调用 scene->onFrame()
Web:     emscripten_set_main_loop 调用 scene->onFrame()
```

---

## 12.6 平台差异处理

| 差异点 | 处理方式 |
|--------|---------|
| 字体 | 各平台注册不同的系统字体或内嵌字体 |
| DPR | 各平台 API 不同，统一设置 `setDevicePixelRatio` |
| 安全区域 | iOS 需要 `safeAreaInsets`，其他平台全屏 |
| 生命周期 | Android pause/resume，iOS background/foreground |
| 触摸 vs 鼠标 | 输入层抽象为统一的事件模型 |
| 文件路径 | Android assets、iOS bundle、Web URL |

### 字体加载的平台适配

```cpp
void loadPlatformFonts(wsc::Canvas& canvas) {
#if defined(__ANDROID__)
    // Android: 从 assets 加载
    auto fontData = loadAsset("fonts/NotoSansSC-Regular.otf");
    wsc::FontFace face = wsc::FontFace::fromMemory(
        wsc::FontDescriptor("Noto Sans SC"), std::move(fontData));
    canvas.registerFontFace(face);

#elif defined(__APPLE__)
    // iOS/macOS: 使用 CoreText 后端，系统字体自动可用
    canvas.setTextBackend(wsc::Canvas::TextBackend::CoreText);

#elif defined(__EMSCRIPTEN__)
    // Web: 预加载字体或使用默认 fallback
    // 字体文件需要通过 --preload-file 嵌入

#else
    // Desktop: 发现系统字体
    for (const auto& face : wsc::FontSystem::discoverInstalledFontFaces()) {
        canvas.registerFontFace(face);
    }
#endif

    canvas.setFontFallbackChain(wsc::FontSystem::defaultFallbackChain());
}
```

---

## 12.7 发布包形式汇总

| 平台 | 发布形式 | 后端 | 特殊说明 |
|------|---------|------|---------|
| Windows x64 | ZIP (shared) | OpenGL, GLES, Software | DLL 需随 exe 分发 |
| Linux x64 | ZIP (static) | OpenGL, Software | 需要 Mesa GL 驱动 |
| macOS universal | ZIP (static) | OpenGL, Metal, Software | x86_64 + arm64 |
| Android | AAR (Prefab) | OpenGL ES | 3 ABI: armeabi-v7a, arm64-v8a, x86_64 |
| iOS | XCFramework (static) | Metal | arm64 真机 + 模拟器 |
| Web | 源码构建 | OpenGL ES (WebGL 2) | 无预编译包 |

---

## 12.8 CI/CD 跨平台验证

WhatsCanvas 仓库的 CI 覆盖：

```yaml
# .github/workflows/cross-platform-validation.yml 概要
jobs:
  windows:
    - MSVC 单元测试
    - OpenGL/Software 像素回归
    - Package consumer 集成
  linux:
    - GCC 构建 + 单元测试
    - Mesa/Xvfb OpenGL 像素门禁
    - OpenGL ES 滤镜像素对齐
  macos:
    - Clang universal 构建
    - Metal 像素/契约门禁
  android:
    - NDK 三 ABI 构建
    - AAR 打包
  ios:
    - XCFramework 构建
    - 模拟器 UI 测试
  web:
    - Emscripten 构建
    - 浏览器 headless 测试
```

---

## 12.9 小结

本章学习了：

- [x] 跨平台架构设计（Host + Scene 模式）
- [x] Android 集成（JNI + GLSurfaceView + AAR）
- [x] iOS 集成（Metal + CoreText + XCFramework）
- [x] Web 集成（Emscripten + WebGL 2）
- [x] 共享绘制代码的 Scene 接口模式
- [x] 平台差异处理（字体、DPR、生命周期）
- [x] 各平台发布包形式
- [x] CI/CD 跨平台验证

---

## 教程完结

恭喜你完成了 WhatsCanvas 全部 12 章教程！你现在掌握了：

1. **基础绘制** — 图形、Paint、Path、变换
2. **进阶能力** — 图片、文本、图层滤镜
3. **工程实践** — 窗口呈现、多后端、性能优化、跨平台

更多资源：
- [API Reference](https://clarkwain.github.io/WhatsCanvas/)
- [Visual API Gallery](../visual-api-gallery.md)
- [仓库示例代码](https://github.com/ClarkWain/WhatsCanvas/tree/main/examples)
- [WhatsUI — 基于 WhatsCanvas 的 UI 框架](https://github.com/ClarkWain/WhatsUI)

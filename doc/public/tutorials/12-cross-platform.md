# Chapter 12: Cross-Platform in Practice

> Goal of this chapter: learn the complete integration flow and key considerations for Android (JNI + GLSurfaceView), iOS (Metal + CoreText), and Web (Emscripten + WebGL 2).

For the Chinese version, see [`zh/12-cross-platform.md`](./zh/12-cross-platform.md).

---

## 12.1 Cross-Platform Architecture Overview

WhatsCanvas cross-platform design follows the "the library only renders" principle:

```
┌─────────────────────────────────────────────┐
│              Application logic (C++17)      │
├─────────────────────────────────────────────┤
│           WhatsCanvas API (wsc::)           │
├──────┬──────┬──────┬──────┬─────────────────┤
│  SW  │  GL  │ GLES │  VK  │     Metal       │
├──────┴──────┴──────┴──────┴─────────────────┤
│           Platform host                     │
│  Desktop: GLFW                              │
│  Android: GLSurfaceView + JNI              │
│  iOS: UIKit + CAMetalLayer                  │
│  Web: Emscripten + WebGL 2                  │
└─────────────────────────────────────────────┘
```

The core drawing code is written once in C++. Each platform only implements a "host" layer (create window / surface, manage lifecycle, handle input).

---

## 12.2 Android Integration

### Overview

Android calls the C++ rendering code through JNI and gets an OpenGL ES 3.0 context from `GLSurfaceView`.

### Project Layout

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
    └── whatscanvas-android-release-1.1.0.aar
```

### Gradle Setup

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
    implementation files('libs/whatscanvas-android-release-1.1.0.aar')
}
```

### CMakeLists.txt (JNI Layer)

```cmake
cmake_minimum_required(VERSION 3.16)
project(myapp_native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

# WhatsCanvas provided by the Prefab AAR
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

### JNI Render Code

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
    // GLSurfaceView already has a GLES context
    g_canvas = wsc::Canvas::create(wsc::Canvas::Backend::OpenGLES, w, h);
    if (g_canvas) {
        g_canvas->initializeContext();

        // Register fonts
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

    // Draw (identical to the desktop code)
    wsc::Paint bg;
    bg.setLinearGradient(0, 0, g_canvas->getWidth(), g_canvas->getHeight(),
        wsc::Color(30, 40, 60), wsc::Color(10, 15, 25));
    g_canvas->drawRect(wsc::RectF(0, 0,
        g_canvas->getWidth(), g_canvas->getHeight()), bg);

    // ... more drawing ...

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

### Key Points

- The AAR contains Prefab metadata; Gradle 4.1+ picks it up automatically.
- `GLSurfaceView` invokes callbacks on the GL thread, so Canvas calls naturally happen on the correct thread.
- Lifecycle: do not destroy the Canvas on `onPause`; `onSurfaceCreated` may fire again.
- Fonts: Android has no fontconfig; use the built-in FreeType + HarfBuzz path.

---

## 12.3 iOS Integration

### Overview

iOS uses the Metal backend + the CoreText text engine and is distributed as an XCFramework.

### Project Layout

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

### Xcode Setup

1. Drag `WhatsCanvas.xcframework` into the Frameworks group of your project.
2. Set "Embed & Sign".
3. In Build Settings, add Header Search Paths pointing at the XCFramework's headers.

### Metal Render View

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

        // Create a Metal Canvas through the C++ bridge
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
        // Draw (calls into C++ rendering code through a C bridge)
        wsc_scene_render(canvas, elapsed)
        wsc_canvas_end_frame(canvas)
        wsc_canvas_present(canvas)
    }
}
```

### Key Points

- Use the `WhatsCanvas::Metal` target (standalone, does not depend on OpenGL).
- Text goes through the CoreText backend; no FreeType / HarfBuzz dependency.
- `CAMetalLayer` window presentation is supported.
- Handle orientation changes and safe area insets.
- Device and simulator share one XCFramework (arm64 + x86_64 slices).

---

## 12.4 Web (Emscripten + WebGL 2)

### Overview

Compile C++ code to WebAssembly with Emscripten; use the OpenGL ES path (mapped to WebGL 2).

### Build Command

```bash
emcmake cmake -S . -B build-wasm \
    -DWHATSCANVAS_BUILD_OPENGLES=ON \
    -DWHATSCANVAS_BUILD_OPENGL=OFF \
    -DWHATSCANVAS_BUILD_SOFTWARE=OFF \
    -DWHATSCANVAS_BUILD_DEMO=OFF

emmake cmake --build build-wasm
```

### C++ Entry

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

    // Draw
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
    // Create a WebGL 2 context
    EmscriptenWebGLContextAttributes attrs;
    emscripten_webgl_init_context_attributes(&attrs);
    attrs.majorVersion = 2;
    attrs.minorVersion = 0;
    attrs.antialias = true;
    attrs.stencil = true;

    auto ctx = emscripten_webgl_create_context("#canvas", &attrs);
    emscripten_webgl_make_context_current(ctx);

    // CSS size is logical size; the drawing buffer size is CSS size × DPR.
    if (!resizeSurface()) return 1;

    // Create WhatsCanvas (OpenGL ES → WebGL 2)
    g_canvas = wsc::Canvas::create(
        wsc::Canvas::Backend::OpenGLES, g_physicalWidth, g_physicalHeight);
    g_canvas->setDevicePixelRatio(g_dpr);
    if (!g_canvas->initializeContext()) return 1;

    g_startTime = emscripten_get_now() / 1000.0;

    // Set up the main loop
    emscripten_set_main_loop(mainLoop, 0, true);
    return 0;
}
```

### HTML Template

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

### Key Points

- Emscripten maps OpenGL ES 3.0 calls onto WebGL 2.
- Use `emscripten_set_main_loop` instead of a traditional while-loop.
- CSS sizes drive logical layout; the drawing buffer is `CSS size × DPR`.
- Get the DPR from `emscripten_get_device_pixel_ratio()` and pass it to `setDevicePixelRatio`.
- `getWidth()` / `getHeight()` are physical sizes; do not use them to compute page centers after setting a DPR.
- Fonts need to be preloaded or loaded async from a URL.
- There is no WebGPU backend or precompiled Web release package yet.

---

## 12.5 Shared Drawing Code

The core of cross-platform is to **write drawing code once**. A Scene interface pattern works well:

```cpp
// scene.h — shared across platforms
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

// Concrete Scene (pure C++ drawing logic)
class MyAppScene : public IScene {
public:
    void onInit(wsc::Canvas& canvas) override {
        // Register fonts, load images
    }

    void onResize(wsc::Canvas& canvas, float w, float h) override {
        width_ = w;
        height_ = h;
    }

    void onFrame(wsc::Canvas& canvas, float elapsed) override {
        // All drawing code lives here
        // The same code runs on every platform
    }

    void onDestroy() override {
        // Release resources
    }

private:
    float width_ = 0, height_ = 0;
};
```

Each platform host only needs to:

```
Desktop: GlfwHost calls scene->onFrame()
Android: JNI layer forwards GLSurfaceView callbacks
iOS:     MTKViewDelegate calls scene->onFrame()
Web:     emscripten_set_main_loop calls scene->onFrame()
```

---

## 12.6 Handling Platform Differences

| Difference | Approach |
|------------|----------|
| Fonts | Register different system or embedded fonts per platform |
| DPR | Different APIs per platform; call `setDevicePixelRatio` uniformly |
| Safe area | iOS needs `safeAreaInsets`; other platforms are full-screen |
| Lifecycle | Android pause/resume; iOS background/foreground |
| Touch vs. mouse | Abstract input as a unified event model |
| File paths | Android assets, iOS bundle, Web URL |

### Platform-Specific Font Loading

```cpp
void loadPlatformFonts(wsc::Canvas& canvas) {
#if defined(__ANDROID__)
    // Android: load from assets
    auto fontData = loadAsset("fonts/NotoSansSC-Regular.otf");
    wsc::FontFace face = wsc::FontFace::fromMemory(
        wsc::FontDescriptor("Noto Sans SC"), std::move(fontData));
    canvas.registerFontFace(face);

#elif defined(__APPLE__)
    // iOS/macOS: use the CoreText backend; system fonts are available automatically
    canvas.setTextBackend(wsc::Canvas::TextBackend::CoreText);

#elif defined(__EMSCRIPTEN__)
    // Web: preload fonts or use the default fallback.
    // Font files need to be embedded via --preload-file.

#else
    // Desktop: discover system fonts
    for (const auto& face : wsc::FontSystem::discoverInstalledFontFaces()) {
        canvas.registerFontFace(face);
    }
#endif

    canvas.setFontFallbackChain(wsc::FontSystem::defaultFallbackChain());
}
```

---

## 12.7 Release Package Formats

| Platform | Format | Backends | Notes |
|----------|--------|----------|-------|
| Windows x64 | ZIP (shared) | OpenGL, GLES, Software | Distribute DLLs next to the exe |
| Linux x64 | ZIP (static) | OpenGL, Software | Requires the Mesa GL driver |
| macOS universal | ZIP (static) | OpenGL, Metal, Software | x86_64 + arm64 |
| Android | AAR (Prefab) | OpenGL ES | Three ABIs: armeabi-v7a, arm64-v8a, x86_64 |
| iOS | XCFramework (static) | Metal | arm64 device + simulator |
| Web | Source build | OpenGL ES (WebGL 2) | No precompiled package |

---

## 12.8 CI/CD Cross-Platform Validation

The WhatsCanvas repository CI covers:

```yaml
# .github/workflows/cross-platform-validation.yml (excerpt)
jobs:
  windows:
    - MSVC unit tests
    - OpenGL / Software pixel regression
    - Package consumer integration
  linux:
    - GCC build + unit tests
    - Mesa / Xvfb OpenGL pixel gate
    - OpenGL ES filter pixel parity
  macos:
    - Clang universal build
    - Metal pixel / contract gates
  android:
    - NDK three-ABI build
    - AAR packaging
  ios:
    - XCFramework build
    - Simulator UI tests
  web:
    - Emscripten build
    - Headless browser tests
```

---

## 12.9 Summary

This chapter covered:

- [x] Cross-platform architecture (Host + Scene pattern)
- [x] Android integration (JNI + GLSurfaceView + AAR)
- [x] iOS integration (Metal + CoreText + XCFramework)
- [x] Web integration (Emscripten + WebGL 2)
- [x] Sharing drawing code with the Scene interface
- [x] Handling platform differences (fonts, DPR, lifecycle)
- [x] Release package formats per platform
- [x] Cross-platform CI/CD validation

---

## Tutorial Complete

Congratulations on finishing all 12 chapters of the WhatsCanvas tutorial! You now have a working command of:

1. **Fundamentals** — shapes, Paint, Path, transforms
2. **Advanced capabilities** — images, text, layer filters
3. **Engineering practice** — windowed presentation, multi-backend, performance, cross-platform

More resources:
- [API Reference](https://clarkwain.github.io/WhatsCanvas/)
- [Visual API Gallery](../reference/visual-api-gallery.md)
- [Example code in the repository](https://github.com/ClarkWain/WhatsCanvas/tree/main/examples)
- [WhatsUI — a UI framework built on WhatsCanvas](https://github.com/ClarkWain/WhatsUI)

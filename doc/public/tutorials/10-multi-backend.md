# Chapter 10: Multiple Backends and Fallback

> Goal of this chapter: understand the WhatsCanvas multi-backend architecture, select and switch backends at runtime, build automatic downgrade strategies, and know the differences and caveats between backends.

For the Chinese version, see [`zh/10-multi-backend.md`](./zh/10-multi-backend.md).

---

## 10.1 Backend Overview

WhatsCanvas supports 5 render backends:

| Backend | CMake Target | GPU Requirement | Typical Use |
|---------|--------------|-----------------|-------------|
| Software | `WhatsCanvas::Software` | None | Tests, CI, offscreen image generation, fallback |
| OpenGL 3.3 Core | `WhatsCanvas::OpenGL` | A GL context | Primary desktop path |
| OpenGL ES 3.0 | `WhatsCanvas::OpenGLES` | EGL / GLES | Mobile, WebGL |
| Vulkan | Compiled into the OpenGL target | Vulkan SDK / driver | High-performance, low overhead |
| Metal | `WhatsCanvas::Metal` | Apple GPU | macOS / iOS |

---

## 10.2 Static Selection: Decide at Compile Time

Simplest option — link only the backend you need:

```cmake
# Software only (no GPU dependency)
find_package(WhatsCanvas 1.1.0 CONFIG REQUIRED)
target_link_libraries(MyApp PRIVATE WhatsCanvas::Software)
```

```cmake
# Desktop app, OpenGL
target_link_libraries(MyApp PRIVATE WhatsCanvas::OpenGL)
```

```cmake
# Apple platform, Metal
target_link_libraries(MyApp PRIVATE WhatsCanvas::Metal)
```

---

## 10.3 Runtime Selection: Single Backend

```cpp
using Backend = wsc::Canvas::Backend;

// Choose the backend explicitly
auto canvas = wsc::Canvas::create(Backend::OpenGL, 800, 600);
if (!canvas) {
    // OpenGL is not available (no context, driver problems, ...)
}
```

---

## 10.4 Automatic Downgrade: Backend Priority List

WhatsCanvas can take a list of candidate backends and try them in priority order:

```cpp
using Backend = wsc::Canvas::Backend;

// Prefer Vulkan → Metal → OpenGL → Software
auto canvas = wsc::Canvas::create(
    {Backend::Vulkan, Backend::Metal, Backend::OpenGL, Backend::Software},
    800, 600);

if (!canvas) {
    // All backends failed (edge case)
    return 1;
}

// Inspect which backend was actually used
Backend actual = canvas->backend();
switch (actual) {
    case Backend::Vulkan:   printf("Using Vulkan\n"); break;
    case Backend::Metal:    printf("Using Metal\n"); break;
    case Backend::OpenGL:   printf("Using OpenGL\n"); break;
    case Backend::Software: printf("Using Software\n"); break;
    default: break;
}
```

---

## 10.5 Backend Availability Probing

Check whether a backend is available before creating a Canvas:

```cpp
if (wsc::Canvas::isBackendAvailable(Backend::Vulkan)) {
    // Vulkan SDK and driver are ready
}

if (wsc::Canvas::isBackendAvailable(Backend::Metal)) {
    // Apple Metal is available
}

if (wsc::Canvas::isBackendAvailable(Backend::OpenGL)) {
    // Note: this only tests compile-time support; the GL context is still up to the app
}

// Software is always available
assert(wsc::Canvas::isBackendAvailable(Backend::Software));
```

---

## 10.6 Context Requirements per Backend

### Software

```cpp
// No external dependency
auto canvas = wsc::Canvas::create(Backend::Software, w, h);
canvas->initializeContext();  // Always succeeds
```

### OpenGL

```cpp
// The app must have created and made a GL context current first
glfwMakeContextCurrent(window);

// Then load GL function pointers
wsc::Canvas::loadOpenGL(
    reinterpret_cast<wsc::Canvas::OpenGLProcAddress>(glfwGetProcAddress));

// Create the Canvas (binds to the current GL context)
auto canvas = wsc::Canvas::create(Backend::OpenGL, w, h);
canvas->initializeContext();
```

### OpenGL ES

```cpp
// Similar to OpenGL, but the context is EGL / GLES
// Android: obtained via GLSurfaceView
// Web: created via Emscripten as a WebGL 2 context
```

### Vulkan

```cpp
// Vulkan self-manages instance, device, and queue
auto canvas = wsc::Canvas::create(Backend::Vulkan, w, h);
canvas->initializeContext();

// Vulkan objects are exposed for interop
void* instance = canvas->vulkanInstance();
void* device = canvas->vulkanDevice();
void* queue = canvas->vulkanQueue();
unsigned int queueFamily = canvas->vulkanQueueFamily();
```

### Metal

```cpp
// Metal self-manages device and command queue
auto canvas = wsc::Canvas::create(Backend::Metal, w, h);
canvas->initializeContext();

// Metal objects are exposed for interop
void* device = canvas->metalDevice();        // id<MTLDevice>
void* cmdQueue = canvas->metalCommandQueue(); // id<MTLCommandQueue>
```

---

## 10.7 Trim Unused Backends at Build Time

Use CMake options to control which backends compile in, keeping the binary small:

```cmake
# Minimal build: Software only
set(WHATSCANVAS_BUILD_OPENGL OFF)
set(WHATSCANVAS_BUILD_OPENGLES OFF)
set(WHATSCANVAS_BUILD_METAL OFF)
set(WHATSCANVAS_BUILD_SOFTWARE ON)
set(WHATSCANVAS_ENABLE_VULKAN OFF)

# Mobile build: OpenGL ES only
set(WHATSCANVAS_BUILD_OPENGL OFF)
set(WHATSCANVAS_BUILD_OPENGLES ON)
set(WHATSCANVAS_BUILD_SOFTWARE OFF)

# Standalone Metal build for Apple
set(WHATSCANVAS_BUILD_OPENGL OFF)
set(WHATSCANVAS_BUILD_METAL ON)
set(WHATSCANVAS_BUILD_SOFTWARE OFF)
```

---

## 10.8 Backend Differences and Caveats

### Pixel Consistency

| Scenario | Notes |
|----------|-------|
| Software vs. Software | Deterministic (same input = same output) |
| Software vs. GPU | Differences exist (float precision, AA implementation) |
| GPU vs. GPU (different drivers) | May differ slightly |

**Best practice**:
- Use Software for pixel regression tests.
- Use tolerance-based comparison for GPU regressions.

### Feature Differences

| Feature | Software | OpenGL | Vulkan | Metal |
|---------|:--------:|:------:|:------:|:-----:|
| Basic shapes | All | All | All | All |
| Layer filters | All | All | All | All |
| Windowed presentation | Win32 only | All platforms | Win32 | macOS / iOS |
| External textures | N/A | GL texture | Vulkan image | MTLTexture |
| Async pixel readback | Sync-emulated | PBO async | Async | Async |

### Thread Safety

```
⚠️ Canvas is not thread-safe.
Each Canvas instance must be used on the render thread that created it.
Different Canvas instances can work independently on different threads
(when the backend supports that).
```

---

## 10.9 Practical Patterns for Conditional Backend Selection

### Pattern 1: Environment Variable Override

```cpp
Backend selectBackend() {
    const char* env = std::getenv("WSC_BACKEND");
    if (env) {
        if (strcmp(env, "vulkan") == 0) return Backend::Vulkan;
        if (strcmp(env, "metal") == 0)  return Backend::Metal;
        if (strcmp(env, "gl") == 0)     return Backend::OpenGL;
        if (strcmp(env, "sw") == 0)     return Backend::Software;
    }
    // Default: platform-preferred choice
#if defined(__APPLE__)
    return Backend::Metal;
#else
    return Backend::OpenGL;
#endif
}
```

### Pattern 2: Selection from a Config File

```cpp
Backend backendFromConfig(const Config& cfg) {
    if (cfg.renderer == "vulkan" && Canvas::isBackendAvailable(Backend::Vulkan))
        return Backend::Vulkan;
    if (cfg.renderer == "metal" && Canvas::isBackendAvailable(Backend::Metal))
        return Backend::Metal;
    return Backend::OpenGL;  // safe default
}
```

### Pattern 3: Performance Probe (Advanced)

```cpp
// Create a tiny probe Canvas and measure one frame
auto probe = Canvas::create(Backend::Vulkan, 64, 64);
if (probe && probe->initializeContext()) {
    probe->beginFrame();
    // ... draw simple content ...
    probe->endFrame();
    // If it worked, use Vulkan
    probe.reset();
    return Backend::Vulkan;
}
return Backend::OpenGL;
```

---

## 10.10 Rendering to a GL Framebuffer (embed in an existing engine)

If your app already has a GL render pipeline, let WhatsCanvas render into an FBO:

```cpp
GLuint fbo;
glGenFramebuffers(1, &fbo);
glBindFramebuffer(GL_FRAMEBUFFER, fbo);
// ... configure color attachment ...

auto canvas = Canvas::create(Backend::OpenGL, 512, 512);
canvas->initializeContext();
canvas->setOutputTarget(OutputTarget::GLFramebuffer(fbo, 512, 512, false));

// Render WhatsCanvas content into the FBO
canvas->beginFrame();
// ... draw ...
canvas->endFrame();

// Then use the FBO's color attachment as a texture in your main pipeline
```

---

## 10.11 Vulkan External Image Interop

```cpp
// Direct WhatsCanvas output into an external Vulkan image
VkImage externalImage = ...;
VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;

canvas->setOutputTarget(
    OutputTarget::VulkanImageTarget(externalImage, format, w, h));

canvas->beginFrame();
// ... draw ...
canvas->endFrame();
// externalImage now contains the rendered result
```

---

## 10.12 Integrated Example: An Adaptive-Backend App Skeleton

```cpp
#include <wsc/wsc.h>
#include <cstdio>

using namespace wsc;

struct AppConfig {
    Canvas::Backend preferredBackend = Canvas::Backend::Auto;
    int width = 800;
    int height = 600;
};

class App {
public:
    bool initialize(const AppConfig& config) {
        // Auto-select backend
        if (config.preferredBackend == Canvas::Backend::Auto) {
            canvas_ = Canvas::create(
                {Canvas::Backend::Metal, Canvas::Backend::Vulkan,
                 Canvas::Backend::OpenGL, Canvas::Backend::Software},
                config.width, config.height);
        } else {
            canvas_ = Canvas::create(config.preferredBackend,
                                     config.width, config.height);
        }

        if (!canvas_) {
            fprintf(stderr, "Failed to create any Canvas backend\n");
            return false;
        }

        if (!canvas_->initializeContext()) {
            fprintf(stderr, "Failed to initialize context\n");
            return false;
        }

        printf("Initialized with backend: %s\n", backendName());
        return true;
    }

    void renderFrame() {
        canvas_->beginFrame();
        onDraw(*canvas_);
        canvas_->endFrame();
    }

    Canvas& canvas() { return *canvas_; }

private:
    virtual void onDraw(Canvas& canvas) {
        // Subclasses provide the actual drawing
        canvas.drawColor(Color(30, 30, 30));
    }

    const char* backendName() const {
        switch (canvas_->backend()) {
            case Backend::Software: return "Software";
            case Backend::OpenGL:   return "OpenGL";
            case Backend::OpenGLES: return "OpenGL ES";
            case Backend::Vulkan:   return "Vulkan";
            case Backend::Metal:    return "Metal";
            default:                return "Unknown";
        }
    }

    std::unique_ptr<Canvas> canvas_;
};
```

---

## 10.13 Summary

This chapter covered:

- [x] Characteristics and use cases of the 5 backends
- [x] Compile-time (CMake target) and runtime selection
- [x] Backend priority list and automatic downgrade
- [x] Availability probing with `isBackendAvailable`
- [x] Context requirements per backend
- [x] Trimming unused backends at build time
- [x] Pixel consistency and feature differences
- [x] GL framebuffer and Vulkan image interop
- [x] Practical patterns for backend selection

**Next chapter**: [Performance Optimization](./11-performance.md) — Picture caching, quickReject, render stats, and other performance techniques.

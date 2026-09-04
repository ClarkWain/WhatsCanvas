# 第十章：多后端与 Fallback

> 本章目标：理解 WhatsCanvas 的多后端架构，学会在运行时选择和切换后端，实现自动降级策略，以及不同后端的差异与注意事项。

---

## 10.1 后端概览

WhatsCanvas 支持 5 种渲染后端：

| 后端 | CMake Target | GPU 要求 | 典型用途 |
|------|-------------|---------|---------|
| Software | `WhatsCanvas::Software` | 无 | 测试、CI、离屏图片生成、fallback |
| OpenGL 3.3 Core | `WhatsCanvas::OpenGL` | 需要 GL 上下文 | 桌面应用主力 |
| OpenGL ES 3.0 | `WhatsCanvas::OpenGLES` | 需要 EGL/GLES | 移动端、WebGL |
| Vulkan | 编入 OpenGL target | 需要 Vulkan SDK/驱动 | 高性能低开销 |
| Metal | `WhatsCanvas::Metal` | Apple GPU | macOS / iOS |

---

## 10.2 静态选择：编译期决定

最简单的方式——构建时只链接需要的后端：

```cmake
# 只需要 Software（无 GPU 依赖）
find_package(WhatsCanvas 1.1.0 CONFIG REQUIRED)
target_link_libraries(MyApp PRIVATE WhatsCanvas::Software)
```

```cmake
# 桌面应用，使用 OpenGL
target_link_libraries(MyApp PRIVATE WhatsCanvas::OpenGL)
```

```cmake
# Apple 平台，使用 Metal
target_link_libraries(MyApp PRIVATE WhatsCanvas::Metal)
```

---

## 10.3 运行时选择：单后端创建

```cpp
using Backend = wsc::Canvas::Backend;

// 明确指定后端
auto canvas = wsc::Canvas::create(Backend::OpenGL, 800, 600);
if (!canvas) {
    // OpenGL 不可用（没有上下文、驱动问题等）
}
```

---

## 10.4 自动降级：后端优先级列表

WhatsCanvas 支持传入一组备选后端，按优先级依次尝试：

```cpp
using Backend = wsc::Canvas::Backend;

// 优先 Vulkan → Metal → OpenGL → Software
auto canvas = wsc::Canvas::create(
    {Backend::Vulkan, Backend::Metal, Backend::OpenGL, Backend::Software},
    800, 600);

if (!canvas) {
    // 所有后端都不可用（极端情况）
    return 1;
}

// 查看实际使用了哪个后端
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

## 10.5 后端可用性探测

在创建 Canvas 之前检查后端是否可用：

```cpp
if (wsc::Canvas::isBackendAvailable(Backend::Vulkan)) {
    // Vulkan SDK 和驱动都就绪
}

if (wsc::Canvas::isBackendAvailable(Backend::Metal)) {
    // Apple Metal 可用
}

if (wsc::Canvas::isBackendAvailable(Backend::OpenGL)) {
    // 注意：这只检查编译时支持，GL 上下文仍需应用创建
}

// Software 始终可用
assert(wsc::Canvas::isBackendAvailable(Backend::Software));
```

---

## 10.6 各后端的上下文要求

### Software

```cpp
// 无任何外部依赖
auto canvas = wsc::Canvas::create(Backend::Software, w, h);
canvas->initializeContext();  // 总是成功
```

### OpenGL

```cpp
// 应用必须先创建并激活 GL 上下文
glfwMakeContextCurrent(window);

// 然后加载 GL 函数指针
wsc::Canvas::loadOpenGL(
    reinterpret_cast<wsc::Canvas::OpenGLProcAddress>(glfwGetProcAddress));

// 创建 Canvas（绑定到当前 GL 上下文）
auto canvas = wsc::Canvas::create(Backend::OpenGL, w, h);
canvas->initializeContext();
```

### OpenGL ES

```cpp
// 类似 OpenGL，但上下文是 EGL/GLES
// Android: 通过 GLSurfaceView 获取
// Web: 通过 Emscripten 创建 WebGL 2 上下文
```

### Vulkan

```cpp
// Vulkan 自管理实例、设备、队列
auto canvas = wsc::Canvas::create(Backend::Vulkan, w, h);
canvas->initializeContext();

// 可获取 Vulkan 对象用于互操作
void* instance = canvas->vulkanInstance();
void* device = canvas->vulkanDevice();
void* queue = canvas->vulkanQueue();
unsigned int queueFamily = canvas->vulkanQueueFamily();
```

### Metal

```cpp
// Metal 自管理设备和命令队列
auto canvas = wsc::Canvas::create(Backend::Metal, w, h);
canvas->initializeContext();

// 可获取 Metal 对象用于互操作
void* device = canvas->metalDevice();        // id<MTLDevice>
void* cmdQueue = canvas->metalCommandQueue(); // id<MTLCommandQueue>
```

---

## 10.7 构建时按需裁剪

通过 CMake 选项控制编译哪些后端，减小最终体积：

```cmake
# 最小构建：仅 Software
set(WHATSCANVAS_BUILD_OPENGL OFF)
set(WHATSCANVAS_BUILD_OPENGLES OFF)
set(WHATSCANVAS_BUILD_METAL OFF)
set(WHATSCANVAS_BUILD_SOFTWARE ON)
set(WHATSCANVAS_ENABLE_VULKAN OFF)

# 移动端构建：仅 OpenGL ES
set(WHATSCANVAS_BUILD_OPENGL OFF)
set(WHATSCANVAS_BUILD_OPENGLES ON)
set(WHATSCANVAS_BUILD_SOFTWARE OFF)

# Apple 独立 Metal 构建
set(WHATSCANVAS_BUILD_OPENGL OFF)
set(WHATSCANVAS_BUILD_METAL ON)
set(WHATSCANVAS_BUILD_SOFTWARE OFF)
```

---

## 10.8 后端差异与注意事项

### 像素一致性

| 场景 | 说明 |
|------|------|
| Software vs Software | 确定性一致（相同输入 = 相同输出） |
| Software vs GPU | 存在差异（浮点精度、AA 实现不同） |
| GPU vs GPU（不同驱动） | 可能存在微小差异 |

**最佳实践**：
- 像素回归测试使用 Software 后端
- GPU 后端回归使用容差比较（tolerance）

### 功能差异

| 功能 | Software | OpenGL | Vulkan | Metal |
|------|:--------:|:------:|:------:|:-----:|
| 基础图形 | 全部 | 全部 | 全部 | 全部 |
| 图层滤镜 | 全部 | 全部 | 全部 | 全部 |
| 窗口呈现 | 仅 Win32 | 全平台 | Win32 | macOS/iOS |
| 外部纹理 | N/A | GL texture | Vulkan image | MTLTexture |
| 异步像素回读 | 同步模拟 | PBO 异步 | 异步 | 异步 |

### 线程安全

```
⚠️ Canvas 不是线程安全的。
每个 Canvas 实例必须在创建它的渲染线程上使用。
不同 Canvas 实例可以在不同线程上独立工作（前提是后端支持）。
```

---

## 10.9 实用模式：条件后端选择

### 模式一：环境变量覆盖

```cpp
Backend selectBackend() {
    const char* env = std::getenv("WSC_BACKEND");
    if (env) {
        if (strcmp(env, "vulkan") == 0) return Backend::Vulkan;
        if (strcmp(env, "metal") == 0)  return Backend::Metal;
        if (strcmp(env, "gl") == 0)     return Backend::OpenGL;
        if (strcmp(env, "sw") == 0)     return Backend::Software;
    }
    // 默认：平台最优选择
#if defined(__APPLE__)
    return Backend::Metal;
#else
    return Backend::OpenGL;
#endif
}
```

### 模式二：配置文件选择

```cpp
Backend backendFromConfig(const Config& cfg) {
    if (cfg.renderer == "vulkan" && Canvas::isBackendAvailable(Backend::Vulkan))
        return Backend::Vulkan;
    if (cfg.renderer == "metal" && Canvas::isBackendAvailable(Backend::Metal))
        return Backend::Metal;
    return Backend::OpenGL;  // safe default
}
```

### 模式三：性能探测（高级）

```cpp
// 创建一个小的测试 Canvas，跑一帧测量时间
auto probe = Canvas::create(Backend::Vulkan, 64, 64);
if (probe && probe->initializeContext()) {
    probe->beginFrame();
    // ... draw simple content ...
    probe->endFrame();
    // 如果成功，采用 Vulkan
    probe.reset();
    return Backend::Vulkan;
}
return Backend::OpenGL;
```

---

## 10.10 渲染到 GL Framebuffer（嵌入到现有引擎）

如果你的应用已经有 GL 渲染管线，可以让 WhatsCanvas 渲染到一个 FBO：

```cpp
GLuint fbo;
glGenFramebuffers(1, &fbo);
glBindFramebuffer(GL_FRAMEBUFFER, fbo);
// ... 配置 color attachment ...

auto canvas = Canvas::create(Backend::OpenGL, 512, 512);
canvas->initializeContext();
canvas->setOutputTarget(OutputTarget::GLFramebuffer(fbo, 512, 512, false));

// 渲染 WhatsCanvas 内容到 FBO
canvas->beginFrame();
// ... 绘制 ...
canvas->endFrame();

// 然后在主渲染管线中使用这个 FBO 的 color attachment 作为纹理
```

---

## 10.11 Vulkan 外部 Image 互操作

```cpp
// 将 WhatsCanvas 渲染结果输出到外部 Vulkan image
VkImage externalImage = ...;
VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;

canvas->setOutputTarget(
    OutputTarget::VulkanImageTarget(externalImage, format, w, h));

canvas->beginFrame();
// ... 绘制 ...
canvas->endFrame();
// externalImage 现在包含渲染结果
```

---

## 10.12 综合示例：自适应后端的应用框架

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
        // 自动选择后端
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
        // 子类实现具体绘制
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

## 10.13 小结

本章学习了：

- [x] 5 种后端的特性和适用场景
- [x] 编译期选择（CMake target）和运行时选择
- [x] 后端优先级列表与自动降级
- [x] `isBackendAvailable` 可用性探测
- [x] 各后端的上下文要求差异
- [x] 构建时裁剪不需要的后端
- [x] 像素一致性与功能差异
- [x] GL Framebuffer 和 Vulkan Image 互操作
- [x] 实用的后端选择模式

**下一章**：[性能优化](./11-performance.md) —— 学习 Picture 缓存、quickReject、渲染统计等性能优化技巧。

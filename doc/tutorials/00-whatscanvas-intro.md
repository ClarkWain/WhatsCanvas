# WhatsCanvas —— 一个介于 NanoVG 和 Skia 之间的 C++17 2D 渲染库

> 本文介绍 WhatsCanvas，一个面向原生应用的可嵌入式 2D 渲染库。如果你正在寻找一个 API 风格接近 HTML Canvas、支持多后端渲染、具备完整文本排版和图层滤镜能力的 C++ 绘图方案，这篇文章值得一读。

---

## 一、背景：为什么需要 WhatsCanvas？

在原生 C++ 2D 渲染领域，开发者通常面临这样的选择：

| 方案 | 优点 | 缺点 |
|------|------|------|
| **NanoVG** | 轻量、易集成 | 无多语言文本、无图层滤镜、无像素回归 |
| **Skia** | 功能完整、工业级 | 体量大、构建复杂、API 面广 |
| **Cairo** | 成熟稳定 | 文本能力有限、缺少 GPU 后端 |

WhatsCanvas 的定位正好填补了 NanoVG 和 Skia 之间的空白：

- 提供 **Canvas / Paint / Path** 三件套 API（类似 HTML Canvas，对前端开发者友好）
- 支持 **多语言文本** (CJK、RTL、双向文本、HarfBuzz shaping)
- 内置 **图层滤镜** (模糊、内阴影、毛玻璃效果)
- 支持 **5 种渲染后端** (Software、OpenGL、OpenGL ES、Vulkan、Metal)
- 覆盖 **6 个平台** (Windows、Linux、macOS、Android、iOS、Web)
- 配备完整的 **像素回归测试** 基础设施

---

## 二、快速上手：60 秒画出第一帧

WhatsCanvas 提供 Software 后端，无需 GPU、窗口或图形上下文，非常适合快速验证：

```cpp
#include <wsc/wsc.h>

int main()
{
    // 创建 256x256 的 Software Canvas
    auto canvas = wsc::Canvas::create(wsc::Canvas::Backend::Software, 256, 256);
    if (!canvas || !canvas->initializeContext()) {
        return 1;
    }

    canvas->beginFrame();

    // 设置画笔：蓝色填充 + 抗锯齿
    wsc::Paint fill;
    fill.setColor(wsc::Color(40, 120, 240, 255));
    fill.setAntiAlias(true);

    // 绘制圆角矩形
    canvas->drawRoundRect(wsc::RectF(40, 40, 176, 176), 24.0f, fill);

    canvas->endFrame();

    // 输出到 PPM 文件
    return canvas->savePixelsPPM("first.ppm") ? 0 : 1;
}
```

仅 24 行代码，就能完成一次完整的离屏渲染。CMake 配置也非常简洁：

```cmake
cmake_minimum_required(VERSION 3.16)
project(MyApp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
find_package(WhatsCanvas 1.0.0 CONFIG REQUIRED)

add_executable(MyApp main.cpp)
target_link_libraries(MyApp PRIVATE WhatsCanvas::Software)
```

### 2.1 先确定物理尺寸、逻辑尺寸和 DPR

上面的离屏示例没有设置 DPR，因此 1 个绘制单位对应 1 个输出像素。这适合生成小图，但不应直接照搬到高密度窗口或手机界面。

WhatsCanvas 把三个尺寸概念分开处理：

| 概念 | 含义 | 示例 |
|------|------|------|
| 物理尺寸 | Canvas 缓冲区或 framebuffer 的像素宽高 | `720 × 820 px` |
| DPR | 一个逻辑单位对应多少物理像素 | `2.0` |
| 逻辑尺寸 | 业务布局使用的宽高 | `360 × 410` |

```cpp
constexpr int physicalWidth = 720;
constexpr int physicalHeight = 820;
constexpr float dpr = 2.0f;

auto canvas = wsc::Canvas::create(
    wsc::Canvas::Backend::Software, physicalWidth, physicalHeight);
canvas->setDevicePixelRatio(dpr);

const float logicalWidth = physicalWidth / dpr;
const float logicalHeight = physicalHeight / dpr;
```

设置 DPR 后，坐标、圆角、描边和文字大小都使用逻辑单位，Canvas 在输出时统一映射到物理像素。`getWidth()` 和 `getHeight()` 仍返回物理尺寸，不能直接拿它们作为逻辑布局宽高。

Android 宿主通常把 `DisplayMetrics.density` 传给 `setDevicePixelRatio`，于是一个逻辑单位可以按 1 dp 使用。`Paint::setTextSize` 本身不认识 sp；文字需要由 Android 宿主使用 `TypedValue.applyDimension(COMPLEX_UNIT_SP, ...)` 换算，再除以 density 得到 Canvas 逻辑字号。完整约定和示例见[教程首页的尺寸说明](./README.md)。

---

## 三、核心架构与 API 设计

### 3.1 三大核心对象

WhatsCanvas 的 API 设计围绕三个核心概念：

```
Canvas  ──  绘制表面，管理帧生命周期和状态栈
Paint   ──  绘制属性（颜色、渐变、文本、混合模式等）
Path    ──  2D 几何路径（贝塞尔曲线、hit-testing 等）
```

这种设计对有 HTML Canvas 或 Android Canvas 使用经验的开发者来说极其自然。

### 3.2 Canvas：绘制 API 一览

```cpp
// 基础图形
canvas->drawRect(rect, paint);
canvas->drawRoundRect(rect, radius, paint);
canvas->drawCircle(cx, cy, radius, paint);
canvas->drawOval(bounds, paint);
canvas->drawPath(path, paint);
canvas->drawLine(x1, y1, x2, y2, paint);
canvas->drawBoxShadow(rect, radius, spread, blur, dx, dy, color);

// 图片绘制
canvas->drawImage(image, x, y, paint);
canvas->drawImageFit(image, dst, ImageFit::COVER, paint);
canvas->drawImageRounded(image, dst, radius, paint);
canvas->drawImageNinePatch(image, centerSrc, dst, paint);
canvas->drawImageTiled(image, dst, paint);

// 文本绘制
canvas->drawText("Hello", x, y, paint);
canvas->drawTextBox(text, bounds, lineHeight, maxLines, ellipsize, paint);
canvas->drawTextOnPath(text, path, hOffset, vOffset, paint);
float width = canvas->measureText(text, paint);

// 状态管理
canvas->save();
canvas->translate(dx, dy);
canvas->scale(sx, sy);
canvas->rotate(radians);
canvas->clipPath(path);
canvas->restore();
```

### 3.3 Paint：丰富的绘制属性

```cpp
wsc::Paint paint;

// 颜色和样式
paint.setColor(wsc::Color(40, 120, 240, 255));
paint.setStyle(wsc::Paint::Style::FILL);  // FILL / STROKE / FILL_AND_STROKE
paint.setStrokeWidth(2.0f);
paint.setAntiAlias(true);

// 渐变
paint.setLinearGradient(x1, y1, x2, y2, {
    {0.0f, Color::RED}, {0.5f, Color::GREEN}, {1.0f, Color::BLUE}
});
paint.setRadialGradient(cx, cy, radius, startColor, endColor);

// 阴影
paint.setShadowLayer(radius, dx, dy, shadowColor);

// 混合模式（14 种）
paint.setBlendMode(wsc::Paint::BlendMode::MULTIPLY);

// 文本
paint.setTextSize(24.0f);
paint.setFontFamily("Roboto");
paint.setFontWeight(700);
paint.setTextAlign(wsc::Paint::TextAlign::CENTER);

// 路径效果
paint.setDashPathEffect({10.0f, 5.0f}, 0.0f);
paint.setCornerPathEffect(8.0f);
```

### 3.4 Path：路径构造与查询

```cpp
wsc::Path path;
path.moveTo(10, 10);
path.lineTo(100, 10);
path.quadTo(controlX, controlY, endX, endY);
path.cubicTo(c1x, c1y, c2x, c2y, ex, ey);
path.close();

// 形状辅助
path.addRoundRect(rect, radius);
path.addCircle(cx, cy, radius);

// 查询与 hit-testing
bool hit = path.contains(x, y);
bool strokeHit = path.strokeContains(x, y, strokeWidth);
RectF bounds = path.getBounds();
```

---

## 四、图层滤镜：毛玻璃效果开箱即用

WhatsCanvas 内置了一套完整的图层滤镜系统，支持 content blur、backdrop blur、内阴影、毛玻璃等常见 UI 效果：

```cpp
// 创建毛玻璃滤镜
auto frosted = wsc::ImageFilter::frostedGlass(
    8.0f,   // 模糊半径
    1.18f,  // 饱和度
    1.04f,  // 亮度
    1.02f,  // 对比度
    0.012f  // 颗粒感
);

// 创建带背景模糊的图层
canvas->saveLayer(bounds, paint, wsc::LayerOptions()
    .setBackdropFilter(frosted));

// 在模糊背景上绘制前景内容
canvas->drawRoundRect(rect, radius, foregroundPaint);

canvas->restore();
```

支持的滤镜类型包括：
- `ImageFilter::blur()` — 高斯模糊
- `ImageFilter::innerShadow()` — 内阴影
- `ImageFilter::frostedGlass()` — 毛玻璃（模糊 + 饱和度 + 亮度 + 对比度 + 颗粒）
- `ImageFilterChain` — 多滤镜链式组合
- 颜色矩阵变换

---

## 五、多后端架构

WhatsCanvas 通过统一的 `Canvas::create()` API 支持多种渲染后端：

```cpp
using Backend = wsc::Canvas::Backend;

// 按优先级尝试多个后端，自动 fallback
auto canvas = wsc::Canvas::create(
    {Backend::Vulkan, Backend::Metal, Backend::OpenGL, Backend::Software},
    width, height);
```

| 后端 | CMake Target | 适用场景 |
|------|-------------|---------|
| Software | `WhatsCanvas::Software` | 测试、headless、截图、CI 环境 |
| OpenGL 3.3 | `WhatsCanvas::OpenGL` | 桌面应用主力路径 |
| OpenGL ES 3.0 | `WhatsCanvas::OpenGLES` | 移动端、WebGL |
| Vulkan | 编入 OpenGL target | 高性能、低开销渲染 |
| Metal | `WhatsCanvas::Metal` | macOS / iOS 原生 |

后端可在构建时按需裁剪，不需要的后端完全不参与编译和链接：

```cmake
# 只需 CPU 渲染，零 GPU 依赖
set(WHATSCANVAS_BUILD_OPENGL OFF)
set(WHATSCANVAS_BUILD_SOFTWARE ON)
```

---

## 六、文本排版：不只是画字

WhatsCanvas 的文本系统远超一般 2D 绘图库的水平：

- **字体发现与 fallback 链**：自动发现系统字体，支持多级回退
- **CJK 排版**：无空格换行、正确的标点处理
- **RTL 与双向文本**：完整 UAX #9 实现（861,948 例测试全部通过）
- **HarfBuzz shaping**：阿拉伯文、天城文等复杂文字正确成形
- **字体特性**：weight / slant / OpenType features / 可变轴
- **COLR/CPAL 彩色 emoji**：支持 v0 和常见 COLRv1 paint graph
- **平台原生后端**：Windows 可选 DirectWrite，Apple 可选 CoreText

```cpp
// 文本绘制示例
wsc::Paint textPaint;
textPaint.setTextSize(18.0f);
textPaint.setFontFamily("Noto Sans SC");
textPaint.setColor(wsc::Color(33, 33, 33, 255));

// 多行文本 + 省略号
canvas->drawTextBox(
    u8"WhatsCanvas 支持中日韩文本排版，包括自动换行和省略号处理。",
    wsc::RectF(20, 20, 300, 200),
    1.5f,    // 行高倍数
    3,       // 最大行数
    true,    // 启用省略号
    textPaint
);

// 文字沿路径绘制
wsc::Path arc;
arc.addArc(bounds, 0.0f, 3.14f);
canvas->drawTextOnPath(u8"Text on Path", arc, 0, 0, textPaint);
```

---

## 七、跨平台支持

| 平台 | 自动化状态 | 渲染后端 |
|------|-----------|---------|
| Windows x64 | CI 单元测试 + 像素回归 + 包消费验证 | OpenGL, GLES, Software, Vulkan |
| Linux x64 | CI 构建 + 单元测试 + 像素门禁 | OpenGL, GLES, Software |
| macOS | CI 单元测试 + Metal 像素门禁 + universal 发布包 | Metal, OpenGL, Software |
| Android | 三 ABI (armeabi-v7a, arm64-v8a, x86_64) | OpenGL ES |
| iOS | 真机 + 模拟器 Metal/CoreText | Metal |
| Web | Emscripten/WebGL 2 + 浏览器自动化测试 | OpenGL ES (编译为 WebGL 2) |

---

## 八、性能表现

WhatsCanvas 仓库内归档了与 NanoVG 的详细对比基准测试（Windows / Core i7-8700 / GTX 1060 / 1920x1080 / OpenGL）：

| 场景 | 结果 |
|------|------|
| 抗锯齿几何（256~4096 图形） | 8 项领先、1 项持平，最大帧时间下降 26.7% |
| 图片绘制（64~1024 张） | 9/9 领先，最大帧时间下降 58.5% |
| 动态文字（64~1024 次绘制） | 9/9 领先，最大帧时间下降 32.0% |

总计 **26 项领先、0 项落后、1 项持平**，27 项像素质量验证全部通过。

> 测试方法：每进程预热 5 帧、测量 30 帧，每个 cell 使用 2 个 ABBA block、每端 4 个新进程和 10,000 次 bootstrap，保证统计可信度。

---

## 九、接入方式

WhatsCanvas 提供多种接入方式，适应不同工程实践：

### 方式 1：GitHub Release 预编译包

```bash
# 下载 whatscanvas-win64-release-1.0.0.zip
# 解压后通过 CMAKE_PREFIX_PATH 指向安装目录
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/whatscanvas
```

### 方式 2：作为 CMake 子目录

```cmake
add_subdirectory(third_party/WhatsCanvas)
target_link_libraries(MyApp PRIVATE WhatsCanvas::OpenGL)
```

### 方式 3：从源码构建

```bash
git clone --recursive https://github.com/ClarkWain/WhatsCanvas.git
cd WhatsCanvas
# Windows
build.bat --release --package --no-run
# Linux / macOS
sh ./build.sh --release --package --no-run
```

---

## 十、项目结构概览

```
WhatsCanvas/
├── include/wsc/          # 16 个公开头文件（~74 KiB）
├── src/                  # 实现代码
│   ├── canvas/           # Canvas 实现、命令录制
│   ├── opengl/           # OpenGL/GLES/Vulkan/Metal 后端
│   ├── render/           # 渲染器接口、帧编译
│   └── text/             # 文本 shaping、布局、字体后端
├── examples/             # 使用示例
│   ├── hello_world/      # 最小示例（24 行代码）
│   ├── game/tetris/      # 俄罗斯方块游戏
│   └── game/racer/       # 赛车游戏
├── platforms/            # 平台宿主
│   ├── android/          # Android GLSurfaceView/JNI
│   ├── ios/              # iOS UIKit/Metal/CoreText
│   ├── desktop/          # GLFW 桌面宿主
│   └── wasm/             # Emscripten/WebGL 2
├── tests/                # 单元测试 + 像素回归
├── benchmarks/           # 性能基线（NanoVG 对比矩阵）
├── doc/                  # 50+ 文档文件
└── third_party/          # FreeType, HarfBuzz, GLAD, GLFW, stb 等
```

---

## 十一、适用场景总结

**推荐使用 WhatsCanvas 的场景：**

- 原生应用自定义 UI 渲染（非标准控件）
- 数据可视化、图表、HUD
- 2D 游戏渲染层
- 服务端离屏图片生成
- 跨平台 UI 框架的渲染底座
- 需要像素级回归测试的渲染管线

**不建议使用的场景：**

- 需要现成 UI 控件体系（考虑基于 WhatsCanvas 的 [WhatsUI](https://github.com/ClarkWain/WhatsUI)）
- 必须使用 WebGPU
- 需要严格色彩管理或 PDF 生成
- 需要复杂富文本编辑器
- 需要稳定 ABI 的插件系统

---

## 十二、与同类方案对比

| 特性 | WhatsCanvas | NanoVG | Skia | Cairo |
|------|:-----------:|:------:|:----:|:-----:|
| 多语言文本（CJK/RTL/Bidi） | ✅ | ❌ | ✅ | 有限 |
| 图层滤镜（模糊/毛玻璃） | ✅ | ❌ | ✅ | ❌ |
| 多渲染后端 | 5 种 | 仅 GL | 多种 | 2 种 |
| 像素回归测试 | ✅ | ❌ | ✅ | ✅ |
| 构建复杂度 | 中等 | 极低 | 极高 | 中等 |
| 库体量（静态） | ~5-8 MiB | <1 MiB | >30 MiB | ~5 MiB |
| API 学习曲线 | 低 | 低 | 高 | 中等 |
| 移动端支持 | ✅ | 需自行集成 | ✅ | 有限 |

---

## 十三、Retained Rendering：Picture 录制与缓存

对于需要反复绘制的静态内容，WhatsCanvas 提供 `Picture` 机制减少重复开销：

```cpp
// 录制绘制命令
auto picture = canvas->recordPicture([](wsc::Canvas& c) {
    wsc::Paint p;
    p.setColor(wsc::Color(255, 100, 50, 255));
    c.drawCircle(128, 128, 100, p);
    // ... 更多复杂绘制 ...
});

// 多次重放
canvas->drawPicture(*picture);

// 带 GPU 缓存的重放（光栅化到 texture 后复用）
canvas->drawPictureRasterized(*picture);
```

---

## 十四、总结

WhatsCanvas 是一个 **定位精准** 的 C++17 2D 渲染库：

1. **API 友好** —— Canvas/Paint/Path 三件套，上手门槛低
2. **功能完备** —— 文本、图片、滤镜、路径一应俱全
3. **跨平台** —— 6 平台 5 后端，按需裁剪
4. **工程严谨** —— 像素回归、性能基线、CI 全覆盖
5. **体量适中** —— 比 Skia 轻得多，比 NanoVG 强得多

如果你的项目需要一个 "比 NanoVG 强、比 Skia 轻" 的原生 2D 渲染引擎，WhatsCanvas 值得一试。

---

**项目地址**：[https://github.com/ClarkWain/WhatsCanvas](https://github.com/ClarkWain/WhatsCanvas)
**在线文档**：[https://clarkwain.github.io/WhatsCanvas/](https://clarkwain.github.io/WhatsCanvas/)
**许可证**：MIT

---

*如果这篇文章对你有帮助，欢迎点赞、收藏、关注，你的支持是我持续输出的动力！*

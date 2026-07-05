# WhatsCanvas

WhatsCanvas 是一个用 C++17 编写的轻量级二维渲染引擎项目，以 Canvas 的使用方式对外呈现。

它不是要取代 Skia、Cocos2d 这类成熟的大型框架，也不是只停留在极简轻量绘制层。它更像是介于两者之间的一种选择：比大型框架更轻、更容易接入和阅读，比极简绘图库更完整，既能拿来做 UI、工具界面和 2D 游戏项目，也适合作为学习 Canvas 渲染原理的工程样本。

本项目在设计时参考了 love2d 的 Canvas 风格和 Skia 的渲染抽象。

## 项目定位

- 当前以 OpenGL 路线最完整，并已提供 OpenGLES 编译目标；Vulkan、Metal 等后端仍保留扩展空间。
- 对外提供的是 Canvas 风格 API，而不是底层图形接口的直接暴露。
- 定位偏轻量，强调易接入、易阅读、易验证，适合中小型项目、工具型界面、2D 游戏和教学场景。
- 项目自带根工程演示、游戏示例、跨平台 CI、冒烟脚本、像素回归钩子和专题文档，方便试用、学习和继续演进。

## 能力总览

WhatsCanvas 的公开接口仍然是熟悉的 `Canvas` / `Paint` / `Path` / `Image` / `FontFace`，但按能力看会更直观：

| 能力组 | 已实现能力 | 代表入口 |
| --- | --- | --- |
| 基础绘制 | 点、线、折线、多边形、矩形、圆角矩形、圆、椭圆、圆弧、任意路径。 | `drawPoint`、`drawLine`、`drawRect`、`drawRoundRect`、`drawCircle`、`drawOval`、`drawArc`、`drawPath` |
| 路径与几何 | `Path` 构建、曲线 flatten、路径 bounds、fill/stroke hit-test、stroke bounds、虚线、圆角路径效果、Polyline2D 描边网格。 | `Path`、`measureStrokeBounds`、`hitTestPathFill`、`hitTestPathStroke`、`Paint::setDashPathEffect` |
| 绘制样式 | 填充、描边、透明度、逐 `Paint` 解析抗锯齿、线性 / 径向 / 多 stop 渐变、混合模式、真高斯模糊阴影（填充 / 描边 / 文本）、采样质量、图像 tile mode、颜色矩阵。 | `Paint`、`setAntiAlias`、`setLinearGradient`、`setRadialGradient`、`setBlendMode`、`setShadowLayer`、`setColorMatrix` |
| Canvas 状态 | `save` / `restore`、矩阵变换、矩形裁剪、抗锯齿路径裁剪、`saveLayer` 离屏层、render-target canvas、clip 查询、quick reject。 | `save`、`restore`、`translate`、`scale`、`rotate`、`clipRect`、`clipPath`、`saveLayer`、`quickReject` |
| 图像与纹理 | 文件解码、encoded memory、raw RGBA、OpenGL 外部纹理包装、跨后端 typed external image descriptor、整图替换、局部更新、contain / cover / fill 布局、锚点、九宫格、圆角裁剪、圆形裁剪、平铺绘制。 | `Image`、`drawImage`、`drawImageFit`、`drawImageNinePatch`、`drawImageRounded`、`drawImageCircle`、`drawImageTiled`、`wrapExternalTexture`、`wrapExternalImage`、`ExternalImageDescriptor` |
| 字体与文本 | 系统默认字体发现、默认 fallback chain、weight / slant face matching、跨平台 TrueType / TTC 注册、file / memory 字体、collection face index、FreeType glyph lookup / metrics / kerning / rasterization、stb fallback、HarfBuzz shaping、simple shaping fallback、多字体 fallback 分段、真实 ascent / descent / lineGap、字体资源 LRU cache 上限 / 释放 / 统计、GPU glyph atlas、atlas resize-before-evict、dirty rect atlas update、dirty rect count/area collapse stats、RGBA atlas、COLR/CPAL v0 color glyph、UTF-8 layout、CJK no-space wrapping、Unicode space、zero-width break、ellipsis、baseline、letter spacing、渐变文本、描边文本、文本阴影、text-on-path、缺字诊断、raster / shaper / atlas 回退诊断、Unicode UAX #9 全量通过。 | `FontSystem`、`FontFace`、`FontManager`、`FontFallbackChain`、`registerFontFace`、`setFontFallbackChain`、`drawText`、`drawTextBox`、`layoutTextBox`、`drawTextOnPath`、`measureTextMetrics` |
| 渲染后端 | 桌面 OpenGL 主路径、OpenGLES 目标、共享 GL-family 后端、proc-address 注入、上下文生命周期、资源释放与重建、shader portability。 | `Canvas::loadOpenGL`、`WhatsCanvas::OpenGL`、`WhatsCanvas::OpenGLES`、`initializeContext`、`releaseResources` |
| 性能与资源 | 流式顶点缓冲、图片命令同纹理合批、路径命令合批、全局 quad index buffer、离屏 render target 复用池、GPU glyph atlas 复用、indexed glyph lookup、填充三角化 / 描边网格 / 裁剪掩码 LRU 缓存、桌面 GL texel buffer 渐变 stop、OpenGLES fallback。 | `Renderer`、`RenderTargetPool`、`GlyphAtlas`、`LruCache`、`RenderStats` |
| 诊断与验证 | 同步 / 异步像素回读、PPM 截图、像素哈希、fuzzy PPM 对比、固定时间首帧冒烟、OpenGLES 构建冒烟、示例构建冒烟、Unicode Bidi conformance、跨平台 CI。 | `readPixelsRGBA`、`readPixelsRGBAAsync`、`savePixelsPPM`、`computePixelsHashRGBA`、`ctest`、`scripts/*_smoke.*` |

## 与常见 2D 图形库的能力参照

下表是粗粒度的功能定位对比，用来说明 WhatsCanvas 当前更接近哪类使用场景；它不是性能排名，也不表示其它项目不能通过扩展实现相同能力。

| 项目 | 主要定位 | 渲染后端 | 路径 / Canvas 状态 | 图片 / 纹理 | 字体与文本 | 工程化与验证 |
| --- | --- | --- | --- | --- | --- | --- |
| WhatsCanvas | 轻量 C++ Canvas 风格 2D 渲染库，兼顾 UI、工具界面、2D 游戏和学习。 | OpenGL 主路径，OpenGLES 目标，保留多后端扩展空间。 | `save/restore`、矩阵、裁剪、路径、离屏层、解析抗锯齿、真高斯阴影、渐变、命中测试。 | 图片解码、raw RGBA、OpenGL 外部纹理、typed external image descriptor、局部更新、九宫格、圆角 / 圆形裁剪、平铺、render-target canvas。 | FreeType / stb rasterizer、HarfBuzz shaping、fallback chain、glyph atlas、COLR/CPAL v0、UAX #9、像素回归。 | CTest、跨平台 CI、OpenGLES smoke、像素 hash / PPM / fuzzy diff、benchmark smoke、专题文档。 |
| Skia | 完整工业级 2D 图形引擎，覆盖浏览器、应用框架和复杂排版场景。 | CPU、GPU、多平台后端生态成熟。 | 路径、滤镜、着色器、文本和图像能力覆盖面很广。 | 图像编解码、颜色管理、滤镜和 GPU 资源体系完整。 | 高级文本和字体能力完整，常与 HarfBuzz / ICU 等生态协作。 | 成熟工程生态，体量和接入复杂度也更高。 |
| Cairo | 稳定的 2D 矢量绘图库，偏文档、桌面和软件渲染场景。 | CPU surface、PDF / SVG / PS 等输出面强。 | 路径、stroke/fill、变换、裁剪成熟。 | 图像 surface 支持稳定，但不是游戏式纹理管线。 | 基本文字能力可用，复杂 shaping 通常依赖外部文本栈。 | 稳定、可移植，实时 GPU 特性不是重点。 |
| NanoVG | 小型即时模式矢量绘制库，适合嵌入式 UI 和调试面板。 | 典型为 OpenGL 类后端。 | API 简洁，路径、渐变、阴影等 UI 绘制常用能力轻量。 | 支持基础图片绘制和纹理使用。 | 基本文本绘制为主，复杂排版、fallback 和字体诊断不是重点。 | 接入轻，但验证、排版和资源治理通常需要应用侧补齐。 |
| HTML Canvas 2D | 浏览器内建 2D API，适合 Web 内容、图表、小游戏和工具界面。 | 由浏览器实现，后端对用户透明。 | Canvas 状态、路径、变换、裁剪、渐变、阴影能力完整且标准化。 | 图片、视频、ImageBitmap、像素读写等 Web 生态强。 | 文本绘制依赖浏览器字体栈，复杂排版能力受 API 边界影响。 | 跨平台由浏览器兜底，但原生 C++ 嵌入和渲染管线控制较弱。 |
| Qt QPainter | Qt 应用框架中的高层 2D 绘制 API，适合桌面应用 UI。 | Qt paint engine 抽象，随平台和 surface 变化。 | 路径、文本、图片、变换、裁剪等应用 UI 能力成熟。 | 与 Qt image / pixmap / resource 体系结合紧密。 | 与 Qt 字体和文本系统集成好。 | 依托 Qt 生态，功能强但框架依赖较重。 |
| LÖVE2D | 面向 Lua 的 2D 游戏框架，适合快速制作游戏和交互 demo。 | 封装底层图形后端，面向游戏循环。 | Canvas、变换、图像、粒子、shader 等游戏常用能力友好。 | 纹理、sprite、render target、资源加载体验好。 | 文本能力适合游戏 UI，复杂排版不是核心目标。 | 开发体验强，但不是 C++ 库式嵌入接口。 |

## 字体与文本

字体系统已经从早期演示型文字绘制推进到一条完整链路：字体注册、fallback、shaping、glyph rasterization、atlas upload、Canvas 绘制和诊断验证都已经打通。

![字体与文本渲染能力效果图](images/text-rendering-showcase.png)

上图由 `WhatsCanvasDemo` 的 `text-showcase` 场景通过 `savePixelsPPM` 捕获后转换生成，展示的是当前 Canvas 文本渲染路径的真实输出。

- `FontSystem` 会发现常见平台系统字体，并为 `createBasicTextBackend()` 提供默认 primary / fallback chain；需要完全手动控制时可使用 portable backend 或手动注册字体。
- `Paint::setFontWeight` 和 `Paint::setFontSlant` 会参与同 family 多 face 选择，优先匹配 slant，再选择最接近的 weight。
- 跨平台字体路径支持文件字体、内存字体、TrueType Collection 和 collection face index。
- `FontRasterizer` 的 loaded face 资源有默认 LRU 上限，可显式调整容量、清理缓存，并查询 face count、hit / miss 和 eviction 统计；共享缓存访问已加互斥保护，覆盖并发查询和容量调整场景。
- FreeType 可用时优先用于 glyph lookup、metrics、kerning 和 alpha glyph rasterization；不可用时自动回退到内置 `stb_truetype`。
- HarfBuzz 作为可选 OpenType shaping implementation 接入；未启用或不可用时保留 simple shaping 和 kerning fallback。
- `FontFace`、`FontDescriptor`、`FontFallbackChain`、`FontManager` 和后端契约已经成型，文本会按 resolved font face 分段后再 shaping/raster/render。
- `GlyphAtlas` 管理 glyph allocation、indexed lookup、dirty rect count/area collapse stats、context rebuild keys 和 atlas stats；Canvas 侧拥有持久 GPU atlas resource，并支持局部更新、resize-before-evict 和 atlas 文本阴影采样。
- 已支持 color font table detection，COLR/CPAL v0 layered glyph 可以解码、合成到 RGBA glyph bitmap，并通过 RGBA atlas 管线绘制。
- UTF-8-safe layout、CJK no-space wrapping、Unicode spaces、zero-width break、ellipsis、baseline、line height、text box layout、渐变文本、text-on-path、缺字诊断和 raster / shaper / atlas 回退诊断已经纳入统一文本路径。
- 内置 Unicode 17.0.0 `BidiTest.txt` / `BidiCharacterTest.txt` 数据和 conformance target；exhaustive conformance 已通过 861,948 cases、0 skips、0 failures。CI 矩阵包含 HarfBuzz + FreeType 联合字体栈 text tests 和字体 benchmark smoke。

更多细节见 [Text Feature Matrix](doc/TEXT_FEATURE_MATRIX.md) 和 [字体渲染专题](doc/Font%20Rendering%20Techniques/index.html)。

## 画质与渲染

填充与描边支持逐 `Paint` 的**解析抗锯齿**：沿轮廓生成一圈跨骑真实边缘的 ~1px 羽化带，由片元着色器按覆盖度调制 alpha。它是分辨率无关的，不依赖 MSAA，且对所有渲染目标含离屏 FBO 生效；细描边还会做亚像素淡化处理。默认关闭，按需逐 `Paint` 开启：

```cpp
Paint paint;
paint.setAntiAlias(true);
```

下图左侧关闭、右侧开启抗锯齿：

![抗锯齿对比：左关右开](images/aa/aa_comparison.png)

渐变为片元级求值，支持多 stop 的线性 / 径向渐变，避免顶点色在大三角上的分带：

![渐变画质对比：左分带右片元级](images/aa/gradient_comparison.png)

阴影为**真高斯模糊**：先把形状剪影渲到离屏目标，做可分离的横 / 纵两趟高斯模糊，再按阴影色合成，取代了早期的多趟偏移环形近似。填充、描边，以及几何文本与贴图（字形图集 / 位图）文本都走同一条路径；`setShadowLayer` 半径为 0 时回退到偏移近似。下图左半径 8、右半径 24：

![真高斯阴影对比：左半径 8 右半径 24](images/aa/shadow_comparison.png)

路径裁剪（`clipPath`）为**抗锯齿覆盖度掩码**：裁剪路径带与填充相同的解析式 AA 羽化，光栅化进 R8 掩码并按层相乘求交集，绘制时每个片元按覆盖度调制 alpha，取代了原来的 1-bit stencil 硬边（矩形裁剪仍走 scissor 快路径）。下图为星形 / 圆 / 椭圆裁剪的平滑边缘：

![抗锯齿路径裁剪](images/aa/clip_comparison.png)

对比图可用示例 `WhatsCanvasAAShowcase` 复现：`WhatsCanvasAAShowcase <输出路径>` 会在同目录生成 `aa_comparison.png`、`gradient_comparison.png`、`shadow_comparison.png` 与 `clip_comparison.png`。

## 快速开始

环境要求：

- CMake 3.16 或更新版本。
- 支持 C++17 的编译器。
- Windows：Visual Studio 2022 + 桌面 C++ 工作负载。
- macOS / Linux：OpenGL 开发环境，以及可编译 GLFW 示例的系统图形开发库。

Windows：

```bat
build.bat --no-run
build.bat
build.bat --package --no-run
```

macOS / Linux：

```bash
chmod +x build.sh
./build.sh --no-run
./build.sh
./build.sh --package --no-run
```

默认构建产物位于 `build/Debug/` 或 `build/Release/`。如果加上 `--package`，脚本会额外整理出一份更适合交付的目录：

- Windows：`out\package\Debug\` 或 `out\package\Release\`
- macOS / Linux：`out/package/Debug/` 或 `out/package/Release/`

其中库文件在 `lib/`，公共头入口在 `include/wsc/`。

## 作为库接入

如果你希望把 WhatsCanvas 当成库使用，推荐使用 `--package` 生成交付目录，再在你的项目中通过 CMake 包方式接入。

```bat
build.bat --release --package --no-run
```

```cmake
find_package(WhatsCanvas 0.1.11 CONFIG REQUIRED)

add_executable(MyApp main.cpp)
target_link_libraries(MyApp PRIVATE WhatsCanvas::OpenGL)
```

最常见的头文件入口：

```cpp
#include <wsc/wsc.h>
```

如果你只想按模块引入，也可以分别包含 `wsc/Canvas.h`、`wsc/Paint.h`、`wsc/Path.h`、`wsc/Image.h`、`wsc/Font.h` 和 `wsc/base.h`。

安装包的消费面默认暴露 `WhatsCanvas::OpenGL` 和 `include/wsc/`。如果构建时打开 `WHATSCANVAS_BUILD_OPENGLES`，也会额外导出 `WhatsCanvas::OpenGLES`。GLFW 只用于仓库内 examples 的窗口与事件循环，GLAD 被编进 GL-family 后端，GLM 只作为内部数学实现依赖；普通消费者不需要 include 或链接这三者。

如果你的应用自己创建 OpenGL 上下文，需要在使用 Canvas 前把平台的 proc-address 函数交给库：

```cpp
Canvas::loadOpenGL(reinterpret_cast<Canvas::OpenGLProcAddress>(glfwGetProcAddress));
```

OpenGLES 后端使用同一套 Canvas API 和 proc-address 加载入口，适合由宿主应用自行创建 EGL / 平台 OpenGLES 上下文后接入：

```cmake
cmake -S . -B build-gles -DWHATSCANVAS_BUILD_OPENGLES=ON
target_link_libraries(MyApp PRIVATE WhatsCanvas::OpenGLES)
```

当前 OpenGLES 目标复用 GL-family 渲染设备实现，会启用 GLES shader 版本并跳过桌面 OpenGL-only 状态，例如 `GL_FRAMEBUFFER_SRGB` 和 `GL_PROGRAM_POINT_SIZE`。

## 可选字体依赖

OpenType shaping implementation 可以通过 CMake option 打开。CMake 会优先使用 `third_party/harfbuzz`，如果子模块未初始化再查找系统 HarfBuzz；如果没有可用 adapter，会自动回退到 simple shaping，并在文本后端 diagnostics 中报告：

```cmake
cmake -S . -B build -DWHATSCANVAS_ENABLE_OPENTYPE_SHAPING=ON
```

字体栅格化默认会尝试启用 FreeType。CMake 会优先使用 `third_party/freetype`，如果子模块未初始化再查找系统 FreeType；找到 FreeType 时，注册字体的 glyph index、metrics、kerning 和 alpha glyph rasterization 会优先走 FreeType；找不到时自动回退到内置 `stb_truetype` 路径：

```cmake
cmake -S . -B build -DWHATSCANVAS_ENABLE_FREETYPE_RASTERIZER=ON
```

## 示例

### Tetris

位于 [examples/game/tetris](examples/game/tetris)。这是一个很适合学习布局、文本面板、方块绘制和游戏状态叠加的示例。

![Tetris example built with WhatsCanvas](images/tetris.jpg)

### Racer

位于 [examples/game/racer](examples/game/racer)。这个示例更强调滚动场景、裁剪区域、HUD，以及节奏明确的动画驱动。

![Racer example built with WhatsCanvas](images/racer.png)

示例单独构建：

```bat
cd examples\game\tetris
build.bat --no-run
```

```bat
cd examples\game\racer
build.bat --no-run
```

## 验证

常用验证入口：

```bat
ctest -C Debug -L unit --output-on-failure
cmd /c scripts\smoke_test.bat
cmd /c scripts\clip_path_smoke.bat
cmd /c scripts\regression_smoke.bat
cmd /c scripts\text_pixel_regression.bat
cmd /c scripts\examples_smoke.bat
cmd /c scripts\validation_scene_smoke.bat
cmd /c scripts\opengles_build_smoke.bat
ctest -C Debug -L smoke --output-on-failure
```

如果只想跑核心单元测试，优先使用 `ctest -C Debug -L unit --output-on-failure`。当前单元测试覆盖 GraphicsState / Path、文本布局、UTF-8 工具、FontManager、文本后端契约、文本回归、RenderStats、RenderTargetPool、CanvasAdapter、矩阵与裁剪、Paint 状态、Image 生命周期、Canvas 上下文生命周期、GlyphAtlas 和弃用提示。

字体像素回归只覆盖文本渲染路径，默认捕获 `font-regression` 和 `text-showcase` 两个场景后与 `tests/baselines/text/*.ppm` 做 fuzzy comparison。需要刷新本机字体基准时，先设置 `WHATSCANVAS_UPDATE_TEXT_BASELINES=1`，再运行 `scripts\text_pixel_regression.bat`；需要临时缩小范围时可设置 `WHATSCANVAS_TEXT_REGRESSION_SCENES=font-regression`。

根 demo 支持以下环境变量：

```bat
WHATSCANVAS_CAPTURE_PPM=build\capture.ppm .\build\Debug\WhatsCanvasDemo.exe
WHATSCANVAS_PRINT_PIXEL_HASH=1 .\build\Debug\WhatsCanvasDemo.exe
WHATSCANVAS_EXPECT_PIXEL_HASH=<uint64> .\build\Debug\WhatsCanvasDemo.exe
WHATSCANVAS_EXIT_AFTER_FIRST_FRAME=1 .\build\Debug\WhatsCanvasDemo.exe
WHATSCANVAS_FIXED_TIME_SECONDS=1.25 .\build\Debug\WhatsCanvasDemo.exe
WHATSCANVAS_DISABLE_MSAA=1 .\build\Debug\WhatsCanvasDemo.exe
WHATSCANVAS_EXERCISE_CLIP_PATH=1 .\build\Debug\WhatsCanvasDemo.exe
WHATSCANVAS_VALIDATION_SCENE=text-heavy .\build\Debug\WhatsCanvasDemo.exe
WHATSCANVAS_VALIDATION_SCENE=font-regression .\build\Debug\WhatsCanvasDemo.exe
```

Driver-sensitive 场景可以用 PPM 容差比较：

```powershell
python scripts\compare_ppm_fuzzy.py baseline.ppm candidate.ppm --max-channel-delta 3 --max-mean-delta 0.75 --max-changed-percent 5
```

## 文档入口

- [架构总览](doc/architecture/README.md)：适合先建立整体分层和模块边界认知。
- [Text Feature Matrix](doc/TEXT_FEATURE_MATRIX.md)：定义文本、字体、fallback、layout、diagnostics 和后续 atlas 后端的能力边界。
- [Shader Portability Notes](doc/SHADER_PORTABILITY.md)：记录桌面 OpenGL / OpenGLES shader 版本、precision、状态 guard 和 GLES-only build gate。
- [iOS Build Notes](doc/IOS_BUILD_NOTES.md)：记录当前 OpenGLES target 在 iOS 宿主中的构建、上下文生命周期和验证边界。
- [Shadow Model](doc/SHADOW_MODEL.md)：记录 `Paint::setShadowLayer` 的当前契约、shape/text shadow 边界和后续 box-shadow 方向。
- [Visual Regression Notes](doc/VISUAL_REGRESSION.md)：记录严格 hash 与 fuzzy PPM comparison 的适用场景和命令。
- [Blend Mode Audit](doc/BLEND_MODE_AUDIT.md)：记录 `Paint::BlendMode` 到 GL-family blend state 的映射和限制。
- [Performance Benchmarks](doc/PERFORMANCE_BENCHMARKS.md)：记录 core benchmark target、输出格式和当前覆盖范围。
- [Effect Regression Matrix](doc/EFFECT_REGRESSION_MATRIX.md)：记录 gradients、shadows、blend modes、strokes 和 dashes 的回归覆盖入口。
- [Polyline2D 互动教学](doc/polyline/polyline2d_interactive_tutorial.html)：适合理解路径描边、网格生成和相关几何细节。
- [抗锯齿原理与实现互动教学](doc/anti_aliasing/anti_aliasing_interactive_tutorial.html)：适合理解什么是抗锯齿、不同实现方法和 WhatsCanvas 当前做法。
- [字体渲染专题](doc/Font%20Rendering%20Techniques/index.html)：适合补字体渲染、排版和文本后端相关知识。
- [功能演进记录](doc/CanvasEvaluation.md)：适合回看功能推进、验证方式和阶段性成果。
- [测试说明](tests/README.md)：适合查看本地测试入口和 Unicode Bidi conformance 说明。

## 架构

下面的图基于当前源码和 CMake 目标整理。当前实际 GL-family 构建目标是 `WhatsCanvasOpenGL`，可选目标是 `WhatsCanvasOpenGLES`，它们把 `src/canvas`、`src/text`、`src/command`、`src/render` 和 `src/opengl` 编进库；对外消费面主要是 `include/wsc/`。

![WhatsCanvas 当前 Canvas 架构图](images/canvas-architecture.png)

关键代码事实：

- `cmake/WhatsCanvasOpenGL.cmake` 中的 `whatscanvas_add_opengl_library()` 和 `whatscanvas_add_opengles_library()` 明确把 canvas、text、command、render、opengl 源文件加入 GL-family 库目标。
- `include/wsc/Canvas.h` 是主要公共入口；`Canvas` 和 `Image` 都实现了 `ITextureSource`，所以普通图片和 render-target canvas 可以走同一套 `drawImage(const ITextureSource&)` 路径。
- `src/canvas/Canvas.cpp` 的 `Canvas::Impl` 持有 `std::unique_ptr<IRenderer>`、`std::unique_ptr<ITextBackend>`、`GraphicsStateStack`、`layerStack` 和 render-target image resource。
- `src/command/DrawCommand.*` 定义 Points、Lines、Path、Image、Text 五类命令；命令执行时先通过 `RenderContext` 应用 blend、scissor、clip mask，再进入对应 `Draw*Program`。
- `src/render/Renderer.*` 持有命令队列、`RenderContext` 和 `IRenderDevice`，并在 `flush()` 中执行命令，同时处理路径命令合批、像素回读、clip mask resource、image resource 和离屏渲染请求。
- `src/render/RenderDeviceFactory.cpp` 当前会在桌面 OpenGL 构建中默认选择 `OpenGL`，在 OpenGLES 构建中默认选择 `OpenGLES`；二者复用 `OpenGLRenderDevice`，Vulkan 和 Metal 分支存在但返回 `nullptr`。
- `src/render/OpenGLRenderDevice.cpp` 负责初始化 Draw*Program、GlobalIndexBuffers、PixelFormatCaps，并创建 texture、FBO/render target、clip mask resource 和 readback；OpenGLES 目标通过编译定义切换 shader 版本和桌面 GL-only 状态。

## 学习路线

如果你是第一次读这个仓库，建议按这个顺序：

1. 先跑根工程，确认你能看到 `WhatsCanvasDemo` 正常启动。
2. 阅读 [doc/architecture/README.md](doc/architecture/README.md)，先建立整体分层认识。
3. 打开 [doc/polyline/polyline2d_interactive_tutorial.html](doc/polyline/polyline2d_interactive_tutorial.html)，补一遍描边网格和 Path 相关原理。
4. 打开 [doc/anti_aliasing/anti_aliasing_interactive_tutorial.html](doc/anti_aliasing/anti_aliasing_interactive_tutorial.html)，理解抗锯齿的原理、实现方法和仓库内做法。
5. 阅读 [doc/Font Rendering Techniques/index.html](doc/Font%20Rendering%20Techniques/index.html)，把字体与文本渲染相关知识补完整。
6. 查看 [examples/showcase/main.cpp](examples/showcase/main.cpp)，理解演示程序是怎样驱动 `Canvas` 的。
7. 进入 `src/canvas`、`src/render`、`src/opengl`，顺着绘制请求一路往下读。
8. 结合 [tests/README.md](tests/README.md) 和 `scripts/` 目录，看这个仓库如何做本地验证。
9. 最后再读 [doc/CanvasEvaluation.md](doc/CanvasEvaluation.md)，回看功能演进和验证轨迹。

## 项目结构

- `src/`: 核心实现，包含 Canvas、命令、渲染器、OpenGL 后端和文本模块。
- `examples/game/`: 完整游戏示例工程。
- `examples/showcase/`: 根演示程序，适合快速浏览公共 Canvas API 的综合使用方式。
- `examples/snippets/`: 可复制的功能片段，覆盖 font fallback、multiline text、external texture 和 image pattern。
- `tests/`: 单元测试入口与测试说明。
- `benchmarks/`: core benchmark 入口，覆盖文本、图片、命令录制和 flush 等成本观察点。
- `scripts/`: 冒烟、clip-path、回归、示例构建、validation scene 和 OpenGLES 构建验证脚本。
- `doc/polyline/`: 偏原理和互动演示导向的教学材料。
- `doc/Font Rendering Techniques/`: 字体渲染与文本专题材料。
- `doc/architecture/`: ADR 和架构文档，适合系统性阅读。
- `doc/CanvasEvaluation.md`: 功能演进与验证记录。
- `third_party/`: 内部实现和示例构建使用的第三方源码。

## 后续方向

- 持续完善文档、ADR 和学习路径。
- 继续把 Canvas 核心抽成更清晰的可复用库目标。
- 继续推进 CBDT/CBLC / SBIX / SVG / COLR paint graph 等 color glyph 解码、更高质量的文本渲染策略，以及 DirectWrite/CoreText 等 native text adapter 实现。
- 增强自动化验证、渲染回归和性能基准能力。
- 为更多图形后端保留清晰的扩展边界。

# WhatsCanvas

WhatsCanvas 是一个用 C++17 编写的轻量级二维渲染引擎项目，以 Canvas 的使用方式对外呈现。

它不是要取代 Skia、Cocos2d 这类成熟的大型框架，也不是只停留在极简轻量绘制层。它更像是介于两者之间的一种选择：比大型框架更轻、更容易接入和阅读，比极简绘图库更完整，既能拿来做 UI、工具界面和 2D 游戏项目，也适合作为学习 Canvas 渲染原理的工程样本。
本项目在设计时参考了 love2d 的 Canvas 风格和 Skia 的渲染抽象。

## 项目定位

- 当前以 OpenGL 路线最完整，并已提供 OpenGLES 编译目标；Vulkan、Metal 等后端仍保留扩展空间，相关能力持续完善中。
- 对外提供的是 Canvas 风格 API，而不是底层图形接口的直接暴露。
- 定位偏轻量，强调易接入、易阅读、易验证，适合中小型项目和教学场景。
- 如果你需要的是一套更容易掌控、方便按需裁剪的 2D 渲染底座，WhatsCanvas 会比大型框架更灵活；如果你觉得极简绘图库过于轻薄，它又能提供更多工程能力。

## 为什么值得用

- 它由 C++17 编写，工程结构清晰，适合直接嵌入跨平台项目中继续演进。
- 它对外暴露的是熟悉的 Canvas 形式，能较自然地承接 UI、工具型界面、2D 游戏和可视化项目的绘制需求。
- 它不只提供基础图元，还覆盖路径、变换、裁剪、`saveLayer`、图像布局、文本绘制、像素回读和本地回归这些真正会落到项目里的能力。
- 它自带根工程演示和三个游戏示例，既能快速试用，也能拿来验证改动是否可靠。

## 为什么值得学

- 项目规模拿捏得比较合适，足够覆盖真实工程问题，又没有大到让人无从下手。
- 你可以顺着 `Canvas` API 一路读到命令录制、渲染器、设备层和 OpenGL 执行路径，看到一条完整的 2D 渲染链路。
- [doc/architecture/README.md](doc/architecture/README.md) 里有分层设计和 ADR，[doc/CanvasEvaluation.md](doc/CanvasEvaluation.md) 里有功能演进和验证记录，[doc/polyline/polyline2d_interactive_tutorial.html](doc/polyline/polyline2d_interactive_tutorial.html) 和 [doc/Font Rendering Techniques/index.html](doc/Font%20Rendering%20Techniques/index.html) 则补充了偏原理和偏专题的学习内容。
- Tetris、Racer、Bubble Shooter 三个示例不是单纯摆效果图，而是能帮助你理解这个引擎在真实场景里怎么组织绘制、状态和界面。

## 当前 Canvas 架构图

下面的图基于当前源码和 CMake 目标整理，而不是仅基于 `doc/architecture` 中的阶段性说明。当前实际 GL-family 构建目标是 `WhatsCanvasOpenGL`，可选目标是 `WhatsCanvasOpenGLES`，它们把 `src/canvas`、`src/text`、`src/command`、`src/render` 和 `src/opengl` 编进库；对外消费面主要是 `include/wsc/`。

![WhatsCanvas 当前 Canvas 架构图](images/canvas-architecture.png)

架构图验收标准：

- 必须基于当前源码和 CMake 事实，不能只按旧文档或目录名推断。
- 必须清楚表达 GL-family 库边界、公共 API、Canvas 模型、命令录制、渲染抽象和 OpenGL / OpenGLES 后端层次。
- 文字不得被裁剪、遮挡、压线或用省略号吞掉关键信息。
- 连线只保留主依赖方向，不允许穿过文字或把图变成流程图/调用链图。
- 生成脚本必须通过本地校验，并人工查看渲染后的 PNG；未达标不得标记完成。

代码事实依据：

- `cmake/WhatsCanvasOpenGL.cmake` 中的 `whatscanvas_add_opengl_library()` 和 `whatscanvas_add_opengles_library()` 明确把 canvas、text、command、render、opengl 源文件加入 GL-family 库目标。
- `include/wsc/Canvas.h` 是主要公共入口；`Canvas` 和 `Image` 都实现了 `ITextureSource`，所以普通图片和 render-target canvas 可以走同一套 `drawImage(const ITextureSource&)` 路径。
- `src/canvas/Canvas.cpp` 的 `Canvas::Impl` 持有 `std::unique_ptr<IRenderer>`、`std::unique_ptr<ITextBackend>`、`GraphicsStateStack`、`layerStack` 和 render-target image resource。
- `src/command/DrawCommand.*` 定义了 Points、Lines、Path、Image、Text 五类命令；命令执行时先通过 `RenderContext` 应用 blend、scissor、clip mask，再进入对应 `Draw*Program`。
- `src/render/Renderer.*` 持有命令队列、`RenderContext` 和 `IRenderDevice`，并在 `flush()` 中执行命令，同时处理路径命令合批、像素回读、clip mask resource、image resource 和离屏渲染请求。
- `src/render/RenderDeviceFactory.cpp` 当前会在桌面 OpenGL 构建中默认选择 `OpenGL`，在 OpenGLES 构建中默认选择 `OpenGLES`；二者复用 `OpenGLRenderDevice`，Vulkan 和 Metal 分支存在但返回 `nullptr`。
- `src/render/OpenGLRenderDevice.cpp` 负责初始化 Draw*Program、GlobalIndexBuffers、PixelFormatCaps，并创建 texture、FBO/render target、clip mask resource 和 readback；OpenGLES 目标通过编译定义切换 shader 版本和桌面 GL-only 状态。

## 能力概览

- 基础图元：点、线、折线、多边形、矩形、圆角矩形、圆、椭圆、圆弧、任意路径。
- 绘制样式：填充、描边、透明度、线性 / 径向 / 多 stop 渐变、混合模式、阴影、虚线、圆角路径效果、图像采样与贴图模式。
- 画布状态：`save` / `restore`、矩阵变换、矩形裁剪、`clipPath`、`saveLayer`、render-target canvas、命中测试和快速剔除。
- 图像能力：文件解码、encoded memory、raw RGBA、外部纹理包装、整图替换、局部更新、contain / cover / fill 布局、锚点、九宫格、圆角裁剪、圆形裁剪和平铺绘制。
- 文本能力：UTF-8 输入处理、字体描述与 fallback 契约、`drawText`、`drawTextBox`、`drawTextOnPath`、测量、文本框布局、行高、最大行、ellipsis、对齐、baseline、letter spacing、描边文本、文本阴影和缺字诊断。
- 渲染后端：桌面 OpenGL 为主路径，OpenGLES 目标共享同一套 Canvas API 和 GL-family 后端实现；shader portability、上下文生命周期和资源重建已有对应验证。
- 性能与资源：路径 / 点线 / 图像 / 文本绘制程序统一走流式顶点缓冲，图片命令支持同纹理合批，图片绘制使用全局 quad index buffer，离屏 render target 有复用池，桌面 GL 渐变 stop 支持 texel buffer，OpenGLES 保留兼容 fallback。
- 诊断与验证：`RenderStats`、同步 / 异步像素回读、PPM 截图、像素哈希、fuzzy PPM 对比、固定时间首帧冒烟、OpenGLES 构建冒烟、示例构建冒烟和本地严格回归检查。

## Canvas API

公开的 `Canvas` API 保持在一个熟悉、直接的二维绘制模型上，便于上层调用，也便于顺着接口往下理解内部实现。

```cpp
class Canvas {
	struct TextMetrics;
	enum class ImageFit { FILL, CONTAIN, COVER };
	enum class ImageAnchor { ... };

	static void initialize();
	static void finalize();
	static bool loadOpenGL(...);
	static void setGammaCorrect(bool enabled);

	void shutdown();
	bool initializeContext();
	void finalizeContext();
	void releaseResources();
	void setSize(int width, int height);
	int getWidth() const;
	int getHeight() const;
	void setColor(Color color);
	void setBlendMode(Paint::BlendMode blendMode);
	void setRenderTargetMode(bool enabled);

	void drawColor(const Color& color);
	void drawPaint(const Paint& paint);
	void drawPoint(...);
	void drawPoints(...);
	void drawLine(...);
	void drawLines(...);
	void drawPolyline(...);
	void drawPolygon(...);
	void drawRect(...);
	void drawRoundRect(...);
	void drawCircle(...);
	void drawOval(...);
	void drawArc(...);
	void drawPath(const Path& path, ...);
	RectF measureStrokeBounds(...);

	void drawImage(...);
	void drawImageFit(...);
	void drawImageNinePatch(...);
	void drawImageRounded(...);
	void drawImageCircle(...);
	void drawImageTiled(...);
	void drawImage(const ITextureSource& source, ...);
	bool loadImageFromEncodedMemory(...);
	bool loadImageFromRGBA(...);
	bool replaceImageRGBA(...);
	bool updateImageRGBA(...);
	bool wrapExternalTexture(...);

	void drawText(...);
	void drawTextBox(...);
	std::vector<TextLine> layoutTextBox(...);
	void drawTextOnPath(...);
	float measureText(...);
	RectF measureTextBounds(...);
	TextMetrics measureTextMetrics(...);

	int save();
	int saveLayer(...);
	void restore();
	int getSaveCount() const;
	void restoreToCount(int saveCount);

	Matrix4 getMatrix() const;
	PointF mapPoint(...);
	RectF mapRect(...);
	bool inverseMapPoint(...);
	bool inverseMapRect(...);
	void setMatrix(const Matrix4& matrix);
	void resetMatrix();
	void concat(const Matrix4& matrix);
	void translate(float dx, float dy);
	void scale(float sx, float sy);
	void rotate(float radians);

	void clipRect(...);
	void clipPath(const Path& path);
	bool hasClip() const;
	bool getClipBounds(RectF& bounds) const;
	bool isPointInClip(...);
	bool quickReject(...);
	bool hitTestPathFill(...);
	bool hitTestPathStroke(...);

	void beginFrame();
	void flush();
	void endFrame();
	RenderStats getRenderStats() const;
	bool readPixelsRGBA(...);
	bool readPixelsRGBAAsync(...);
	bool pollReadPixelsRGBAAsync();
	bool hasPendingReadPixelsRGBAAsync() const;
	std::vector<unsigned char> readPixelsRGBA() const;
	bool savePixelsPPM(const std::string& path) const;
	static std::uint64_t hashPixelsRGBA(...);
	std::uint64_t computePixelsHashRGBA() const;
};
```

图片 pattern 目前使用 `drawImageTiled` 表达：`Image` 是 pattern 的源，`Paint::setImageTileMode` 控制 `CLAMP` / `REPEAT` / `MIRROR` / `DECAL`，`Paint` 的 alpha、tint、color matrix 和 sampling 继续生效；如果需要移动、缩放、旋转 pattern 区域，可以在调用前使用 `save()` + `translate()` / `scale()` / `rotate()`，绘制后 `restore()`。

如果应用更偏好 immediate-mode 风格，可以使用 `wsc::CanvasAdapter`。它独立于核心 `Canvas` 模型，维护当前 fill/stroke paint、当前 path、字体状态、alpha/blend 状态和 image handle 表，并提供 `beginPath()`、`fill()`、`stroke()`、`fillRect()`、`strokeRect()`、`drawImage(handle, ...)`、`drawText()` 等便捷入口。

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
- [字体渲染专题](doc/Font%20Rendering%20Techniques/index.html)：适合补字体渲染、排版和文本后端相关知识。
- [功能演进记录](doc/CanvasEvaluation.md)：适合回看功能推进、验证方式和阶段性成果。

## 5 分钟上手

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

默认构建产物位于 `build/Debug/` 或 `build/Release/`。如果加上 `--package`，脚本还会额外整理出一份更适合交付的目录：

- Windows：`out\package\Debug\` 或 `out\package\Release\`
- macOS / Linux：`out/package/Debug/` 或 `out/package/Release/`

其中库文件在 `lib/`，公共头入口在 `include/wsc/`。

## 作为库接入

如果你希望把 WhatsCanvas 当成库使用，推荐使用 `--package` 生成交付目录，再在你的项目中通过 CMake 包方式接入。

当前对外发布版本建议继续使用 `0.1.x` 这条线，例如当前仓库使用的是 `0.1.10`。

生成发布目录：

```bat
build.bat --release --package --no-run
```

典型接入方式：

```cmake
find_package(WhatsCanvas 0.1.10 CONFIG REQUIRED)

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
```

```cmake
target_link_libraries(MyApp PRIVATE WhatsCanvas::OpenGLES)
```

当前 OpenGLES 目标复用 GL-family 渲染设备实现，会启用 GLES shader 版本并跳过桌面 OpenGL-only 状态，例如 `GL_FRAMEBUFFER_SRGB` 和 `GL_PROGRAM_POINT_SIZE`。

如果你走 GitHub Release 方式分发，可以直接复用仓库里的 Actions 打包流程，让每次 tag 发布都产出对应的 package zip。

常用验证入口：

```bat
ctest -C Debug -L unit --output-on-failure
cmd /c scripts\smoke_test.bat
cmd /c scripts\clip_path_smoke.bat
cmd /c scripts\regression_smoke.bat
cmd /c scripts\examples_smoke.bat
cmd /c scripts\validation_scene_smoke.bat
cmd /c scripts\opengles_build_smoke.bat
ctest -C Debug -L smoke --output-on-failure
```

如果只想跑核心单元测试，优先使用 `ctest -C Debug -L unit --output-on-failure`。当前单元测试覆盖 GraphicsState / Path、文本布局、UTF-8 工具、FontManager、文本后端契约、文本回归、RenderStats、RenderTargetPool、CanvasAdapter、矩阵与裁剪、Paint 状态、Image 生命周期、Canvas 上下文生命周期、GlyphAtlas 和弃用提示。渲染链路或平台相关改动再补跑 `ctest -C Debug -L smoke --output-on-failure`。

## 示例展示

### Tetris

位于 [examples/game/tetris](examples/game/tetris)。这是一个很适合学习布局、文本面板、方块绘制和游戏状态叠加的示例。

![Tetris example built with WhatsCanvas](images/tetris.jpg)

### Racer

位于 [examples/game/racer](examples/game/racer)。这个示例更强调滚动场景、裁剪区域、HUD，以及节奏明确的动画驱动。

![Racer example built with WhatsCanvas](images/racer.png)

### Bubble Shooter

位于 [examples/game/bubble_shooter](examples/game/bubble_shooter)。适合学习网格排布、瞄准辅助线和轻量 UI 绘制。

![Bubble Shooter example built with WhatsCanvas](images/bubble_shooter.jpg)

示例单独构建：

```bat
cd examples\game\tetris
build.bat --no-run
```

```bat
cd examples\game\racer
build.bat --no-run
```

```bat
cd examples\game\bubble_shooter
build.bat --no-run
```

## 学习路线

如果你是第一次读这个仓库，建议按这个顺序：

1. 先跑根工程，确认你能看到 `WhatsCanvasDemo` 正常启动。
2. 阅读 [doc/architecture/README.md](doc/architecture/README.md)，先建立整体分层认识。
3. 打开 [doc/polyline/polyline2d_interactive_tutorial.html](doc/polyline/polyline2d_interactive_tutorial.html)，补一遍描边网格和 Path 相关原理。
4. 阅读 [doc/Font Rendering Techniques/index.html](doc/Font%20Rendering%20Techniques/index.html)，把字体与文本渲染相关知识补完整。
5. 查看 [examples/showcase/main.cpp](examples/showcase/main.cpp)，理解演示程序是怎样驱动 `Canvas` 的。
6. 进入 `src/canvas`、`src/render`、`src/opengl`，顺着绘制请求一路往下读。
7. 结合 [tests/README.md](tests/README.md) 和 `scripts/` 目录，看这个仓库如何做本地验证。
8. 最后再读 [doc/CanvasEvaluation.md](doc/CanvasEvaluation.md)，回看功能演进和验证轨迹。

## 仓库导览

- `src/`: 核心实现，包含 Canvas、命令、渲染器、OpenGL 后端和文本模块。
- `examples/game/`: 三个完整示例工程。
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

## 本地回归钩子

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
```

Driver-sensitive 场景可以用 PPM 容差比较：

```powershell
python scripts\compare_ppm_fuzzy.py baseline.ppm candidate.ppm --max-channel-delta 3 --max-mean-delta 0.75 --max-changed-percent 5
```

这些钩子非常适合做三类事情：

- 本地快速回归，确认渲染改动没有明显破坏。
- 学习读回 framebuffer、固定时间驱动和像素基线这些工程写法。
- 为后续 CI 和自动化测试打基础。

## 依赖边界

- 对外消费面：C++17、CMake package、`WhatsCanvas::OpenGL`、可选 `WhatsCanvas::OpenGLES`、`include/wsc/`。
- GLFW：只用于 examples 的窗口、OpenGL 上下文和事件循环，不随主库安装，也不是 `WhatsCanvas::OpenGL` 的传递依赖。
- GLAD：作为 OpenGL loader 编进后端库，消费者通过 `Canvas::loadOpenGL` 传入 proc-address 函数，不直接 include 或链接 GLAD。
- GLM：仅用于内部矩阵和向量实现，公共头使用 `wsc::Matrix4`。
- STB / Polyline2D：内部图像加载、轻量文本和描边网格实现依赖，不作为公共 API 暴露。

## 接下来会继续变强

- 持续完善文档、ADR 和学习路径。
- 继续把 Canvas 核心抽成更清晰的可复用库目标。
- 继续推进跨平台字体栅格化、atlas-backed 文本渲染、emoji 彩色字形和更完整的 shaping/layout 后端。
- 增强自动化验证、渲染回归和性能基准能力。
- 为更多图形后端保留清晰的扩展边界。

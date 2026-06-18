# WhatsCanvas

WhatsCanvas 是一个用 C++17 编写的轻量级二维渲染引擎项目，以 Canvas 的使用方式对外呈现。

它不是要取代 Skia、Cocos2d 这类成熟的大型框架，也不是只停留在 NanoVG 式的轻量绘制层。它更像是介于两者之间的一种选择：比大型框架更轻、更容易接入和阅读，比极简绘图库更完整，既能拿来做 UI、工具界面和 2D 游戏项目，也适合作为学习 Canvas 渲染原理的工程样本。

## 项目定位

- 当前以 OpenGL 路线最完整，工程结构已经为 OpenGLES、Vulkan、Metal 等后端预留扩展空间，相关能力仍在持续完善中。
- 对外提供的是 Canvas 风格 API，而不是底层图形接口的直接暴露。
- 定位偏轻量，强调易接入、易阅读、易验证，适合中小型项目和教学场景。
- 如果你需要的是一套更容易掌控、方便按需裁剪的 2D 渲染底座，WhatsCanvas 会比大型框架更灵活；如果你觉得 NanoVG 这一类库过于轻薄，它又能提供更多工程能力。

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

## 能力概览

- 基础图元：点、线、折线、多边形、矩形、圆角矩形、圆、椭圆、圆弧、任意路径。
- 绘制样式：填充、描边、透明度、渐变、混合模式、虚线、圆角路径效果、图像采样与贴图模式。
- 画布状态：`save` / `restore`、矩阵变换、矩形裁剪、`clipPath`、`saveLayer`、命中测试。
- 图像能力：普通绘制、contain / cover / fill 布局、九宫格、平铺绘制。
- 文本能力：`drawText`、`drawTextBox`、`drawTextOnPath`、测量与基础布局。
- 验证能力：PPM 截图、像素哈希、固定时间首帧冒烟测试、严格本地回归检查。

## Canvas API

公开的 `Canvas` API 保持在一个熟悉、直接的二维绘制模型上，便于上层调用，也便于顺着接口往下理解内部实现。

```cpp
class Canvas {
	struct TextMetrics;
	enum class ImageFit { FILL, CONTAIN, COVER };
	enum class ImageAnchor { ... };

	static void initialize();
	static void finalize();

	void shutdown();
	void setSize(int width, int height);
	int getWidth() const;
	int getHeight() const;
	void setColor(Color color);

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
	void drawImageTiled(...);

	void drawText(...);
	void drawTextBox(...);
	void drawTextOnPath(...);
	float measureText(...);
	RectF measureTextBounds(...);
	TextMetrics measureTextMetrics(...);

	int save();
	int saveLayer(...);
	void restore();
	int getSaveCount() const;
	void restoreToCount(int saveCount);

	const glm::mat4& getMatrix() const;
	PointF mapPoint(...);
	RectF mapRect(...);
	bool inverseMapPoint(...);
	bool inverseMapRect(...);
	void setMatrix(const glm::mat4& matrix);
	void resetMatrix();
	void concat(const glm::mat4& matrix);
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
	bool readPixelsRGBA(...);
	std::vector<unsigned char> readPixelsRGBA() const;
	bool savePixelsPPM(const std::string& path) const;
	static std::uint64_t hashPixelsRGBA(...);
	std::uint64_t computePixelsHashRGBA() const;
};
```

## 文档入口

- [架构总览](doc/architecture/README.md)：适合先建立整体分层和模块边界认知。
- [Polyline2D 互动教学](doc/polyline/polyline2d_interactive_tutorial.html)：适合理解路径描边、网格生成和相关几何细节。
- [字体渲染专题](doc/Font%20Rendering%20Techniques/index.html)：适合补字体渲染、排版和文本后端相关知识。
- [功能演进记录](doc/CanvasEvaluation.md)：适合回看功能推进、验证方式和阶段性成果。

## 5 分钟上手

环境要求：

- CMake 3.16 或更新版本。
- 支持 C++17 的编译器。
- Windows：Visual Studio 2022 + 桌面 C++ 工作负载。
- macOS / Linux：OpenGL 开发环境和可用的 GLFW 工具链。

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

常用验证入口：

```bat
cmd /c scripts\smoke_test.bat
cmd /c scripts\clip_path_smoke.bat
cmd /c scripts\regression_smoke.bat
cmd /c scripts\examples_smoke.bat
ctest -C Debug -L smoke --output-on-failure
```

## 示例展示

### Tetris

位于 [example/game/tetris](example/game/tetris)。这是一个很适合学习布局、文本面板、方块绘制和游戏状态叠加的示例。

![Tetris example built with WhatsCanvas](images/tetris.jpg)

### Racer

位于 [example/game/racer](example/game/racer)。这个示例更强调滚动场景、裁剪区域、HUD，以及节奏明确的动画驱动。

![Racer example built with WhatsCanvas](images/racer.png)

### Bubble Shooter

位于 [example/game/bubble_shooter](example/game/bubble_shooter)。适合学习网格排布、瞄准辅助线和轻量 UI 绘制。

![Bubble Shooter example built with WhatsCanvas](images/bubble_shooter.jpg)

示例单独构建：

```bat
cd example\game\tetris
build.bat --no-run
```

```bat
cd example\game\racer
build.bat --no-run
```

```bat
cd example\game\bubble_shooter
build.bat --no-run
```

## 学习路线

如果你是第一次读这个仓库，建议按这个顺序：

1. 先跑根工程，确认你能看到 `WhatsCanvasDemo` 正常启动。
2. 阅读 [doc/architecture/README.md](doc/architecture/README.md)，先建立整体分层认识。
3. 打开 [doc/polyline/polyline2d_interactive_tutorial.html](doc/polyline/polyline2d_interactive_tutorial.html)，补一遍描边网格和 Path 相关原理。
4. 阅读 [doc/Font Rendering Techniques/index.html](doc/Font%20Rendering%20Techniques/index.html)，把字体与文本渲染相关知识补完整。
5. 查看 [src/main.cpp](src/main.cpp)，理解演示程序是怎样驱动 `Canvas` 的。
6. 进入 `src/canvas`、`src/render`、`src/opengl`，顺着绘制请求一路往下读。
7. 结合 [tests/README.md](tests/README.md) 和 `scripts/` 目录，看这个仓库如何做本地验证。
8. 最后再读 [doc/CanvasEvaluation.md](doc/CanvasEvaluation.md)，回看功能演进和验证轨迹。

## 仓库导览

- `src/`: 核心实现，包含 Canvas、命令、渲染器、OpenGL 后端和文本模块。
- `example/game/`: 三个完整示例工程。
- `tests/`: 单元测试入口与测试说明。
- `scripts/`: 冒烟、clip-path、回归、示例构建四类验证脚本。
- `doc/polyline/`: 偏原理和互动演示导向的教学材料。
- `doc/Font Rendering Techniques/`: 字体渲染与文本专题材料。
- `doc/architecture/`: ADR 和架构文档，适合系统性阅读。
- `doc/CanvasEvaluation.md`: 功能演进与验证记录。
- `third_party/`: GLFW、GLM、STB、Polyline2D 等依赖。

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
```

这些钩子非常适合做三类事情：

- 本地快速回归，确认渲染改动没有明显破坏。
- 学习读回 framebuffer、固定时间驱动和像素基线这些工程写法。
- 为后续 CI 和自动化测试打基础。

## 依赖组成

- GLFW：窗口与 OpenGL 上下文创建。
- GLM：矩阵和向量数学。
- STB：图像加载与轻量文本方案。
- Polyline2D：描边网格生成。
- GLAD：OpenGL 3.3 加载器，源码保存在 `third_party/glad`。

## 接下来会继续变强

- 持续完善文档、ADR 和学习路径。
- 继续把 Canvas 核心抽成更清晰的可复用库目标。
- 逐步增强文本、字体度量、字形整形和 Glyph Atlas。
- 增强自动化验证、渲染回归和性能基准能力。
- 为更多图形后端保留清晰的扩展边界。
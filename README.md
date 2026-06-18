# WhatsCanvas

WhatsCanvas 是一个适合直接上手，也适合拿来拆解学习的 C++17 二维画布项目。

它把熟悉的 `Canvas` 绘制模型放在前台，把 OpenGL、命令录制、状态栈、文本后端、验证脚本这些真正的引擎细节放在后台。你可以把它当成一个可运行的二维绘图试验场，也可以把它当成一套中小型图形引擎的学习范本。

## 为什么值得用

- 想快速得到一个 OpenGL 驱动的二维画布项目，而不是从窗口、着色器和状态管理开始重新搭脚手架。
- 想在一个工程里同时拥有路径绘制、变换、裁剪、`saveLayer`、图像布局、文本绘制、像素回读和本地回归能力。
- 想用真实例子验证引擎能力，而不是只看零散的 API 示例。
- 想把根工程和示例工程复用在同一套 OpenGL 目标和依赖配置上。

## 为什么值得学

- 项目规模适中，不会大到难以下手，也不会小到失去工程价值。
- 你可以顺着 `Canvas` API 一路读到命令提交、渲染器、设备层和 OpenGL 执行路径。
- 仓库里有 ADR、架构说明、测试入口和评估记录，适合学习“功能是怎么做出来的”，也适合学习“工程是怎么稳住的”。
- Tetris、Racer、Bubble Shooter 三个示例让它不只是图形 API 的展示，而是接近真实项目形态的可运行样本。

## 能力概览

- 基础图元：点、线、折线、多边形、矩形、圆角矩形、圆、椭圆、圆弧、任意路径。
- 绘制样式：填充、描边、透明度、渐变、混合模式、虚线、圆角路径效果、图像采样与贴图模式。
- 画布状态：`save` / `restore`、矩阵变换、矩形裁剪、`clipPath`、`saveLayer`、命中测试。
- 图像能力：普通绘制、contain / cover / fill 布局、九宫格、平铺绘制。
- 文本能力：`drawText`、`drawTextBox`、`drawTextOnPath`、测量与基础布局。
- 验证能力：PPM 截图、像素哈希、固定时间首帧冒烟测试、严格本地回归检查。

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
```

macOS / Linux：

```bash
chmod +x build.sh
./build.sh --no-run
./build.sh
```

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
2. 阅读 [doc/architecture/README.md](doc/architecture/README.md)，建立整体分层认识。
3. 查看 [src/main.cpp](src/main.cpp)，理解演示程序是怎样驱动 `Canvas` 的。
4. 进入 `src/canvas`、`src/render`、`src/opengl`，顺着绘制请求往下读。
5. 结合 [tests/README.md](tests/README.md) 和 `scripts/` 目录，看这个仓库如何做本地验证。
6. 最后再读 [doc/CanvasEvaluation.md](doc/CanvasEvaluation.md)，回看功能演进和验证轨迹。

## 仓库导览

- `src/`: 核心实现，包含 Canvas、命令、渲染器、OpenGL 后端和文本模块。
- `example/game/`: 三个完整示例工程。
- `tests/`: 单元测试入口与测试说明。
- `scripts/`: 冒烟、clip-path、regression、examples 四类验证脚本。
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
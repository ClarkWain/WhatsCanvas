# WhatsCanvas

[English](README.md) | 中文

[![CI](https://github.com/ClarkWain/WhatsCanvas/actions/workflows/cross-platform-validation.yml/badge.svg)](https://github.com/ClarkWain/WhatsCanvas/actions/workflows/cross-platform-validation.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Version](https://img.shields.io/badge/version-0.1.20-informational.svg)](CHANGELOG.md)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C.svg)](CMakeLists.txt)
[![Documentation](https://img.shields.io/badge/docs-online-success.svg)](https://clarkwain.github.io/WhatsCanvas/)

WhatsCanvas 是一款基于 C++17 编写、面向原生应用的可嵌入 2D 渲染库。它提供了类似 HTML Canvas 的 `Canvas` / `Paint` / `Path` API，覆盖多语言文本、图层滤镜、图片渲染、离屏渲染与像素回读等常用能力。不同于完整的 UI 框架，它聚焦于核心渲染逻辑，未内置控件、布局、输入事件与无障碍支持，也不是 HTML Canvas 的源码级兼容实现。

该项目旨在填补“极简基础绘制库（如 NanoVG）”与“重型图形引擎（如 Skia）”之间的空白，提供一个易于集成、理解和验证的轻量级方案。

> **需要完整 UI 框架？** [WhatsUI](https://github.com/ClarkWain/WhatsUI) 是基于 WhatsCanvas 构建的真实下游项目，提供 C++17 保留模式 UI、Fluent 2 设计系统、控件、布局、输入、焦点、浮层、确定性视觉测试和原生桌面窗口。如果你的需求不只是 2D 绘制，而是完整 UI 库，可以直接评估 WhatsUI。它面向可移植的原生桌面 UI，目前以 Windows 为主要交付和验证平台。

![WhatsCanvas 图层滤镜示例](images/image-filter-showcase.png)

> 上图由 WhatsCanvas 桌面 OpenGL 后端绘制、直接从 framebuffer 回读生成，分辨率 1920 × 1080，并非设计效果图或 UI 截图。

## 先判断它是否适合你的项目

| 你关心的事项 | 当前答案 |
| --- | --- |
| **适用场景** | 原生应用自定义 UI、工具与数据界面、HUD、2D 游戏渲染层、服务端或测试环境中的离屏图片生成。 |
| **API 与语言** | C++17；公开 API 位于 `include/wsc/`，入口是 `#include <wsc/wsc.h>`。 |
| **渲染后端** | OpenGL、纯 CPU Software；可选 OpenGL ES、Vulkan 以及 Metal（macOS/iOS）。WebGPU 尚未实现。 |
| **平台状态** | Windows、Linux、macOS 持续执行构建和单元测试；发布包覆盖 Windows x64、Linux x64 和 macOS universal。Metal 已在 macOS 验证；移动端仍不代表已完成完整设备矩阵验证。 |
| **文本能力** | 字体发现和 fallback、CJK/RTL、UAX #9、换行与省略号、glyph atlas、COLR/CPAL v0；OpenGL/OpenGL ES 默认启用 FreeType 与 HarfBuzz shaping。 |
| **接入方式** | vcpkg overlay port、CMake `find_package`、`add_subdirectory`，或从源码生成可搬运的安装目录。 |
| **体量** | 非 header-only。支持按后端仅链接 `WhatsCanvas::Software`、`::OpenGL`（也承载可选 Vulkan 与 Apple Metal）或 `::OpenGLES`；参考体量见[体量与依赖](#体量与依赖)。 |
| **成熟度** | 当前版本 `0.1.20`，仍处于 pre-1.0。公开 API 边界、跨平台 CI、像素回归、package consumer 集成测试与可审计的性能基线均已建立；升级与平台风险仍需结合下文的边界说明评估。 |
| **许可证** | MIT；`third_party/` 组件遵循各自许可证。 |

**何时推荐使用 WhatsCanvas？**
如果你希望用统一的 Canvas 风格 API 处理 CPU/GPU 渲染、多语言文本以及常见 UI 效果，同时看重截图确定性、像素级回归测试与源码可读性，WhatsCanvas 是一个合适的选择。

**何时需要另寻方案？**
如果你的项目强依赖现成的 UI 控件体系、需要在浏览器中运行、需要 WebGPU 原生支持、需要严格的色彩管理、要求生成文档/PDF、涉及复杂的富文本编辑，或需要依赖已进入长期稳定期（1.0+）、承诺 ABI 稳定的成熟渲染库，那么 WhatsCanvas 可能暂时不适合你。

## 60 秒画出第一帧

Software 后端无需绑定窗口、GL 上下文或 GPU 资源，适合用于初期 API 验证：

```cpp
#include <wsc/wsc.h>

int main()
{
    auto canvas = wsc::Canvas::create(wsc::Canvas::Backend::Software, 256, 256);
    if (!canvas || !canvas->initializeContext()) {
        return 1;
    }

    canvas->beginFrame();

    wsc::Paint fill;
    fill.setColor(wsc::Color(40, 120, 240, 255));
    fill.setAntiAlias(true);
    canvas->drawRoundRect(wsc::RectF(40, 40, 176, 176), 24.0f, fill);

    canvas->endFrame();
    return canvas->savePixelsPPM("first.ppm") ? 0 : 1;
}
```

上面这段代码作为一个独立可编译的 CMake 项目已收录在 [`examples/hello_world/`](examples/hello_world/)，启用 demo 构建时会随主工程一起编译。

你可以直接从 [Releases](https://github.com/ClarkWain/WhatsCanvas/releases) 获取预编译包，或使用以下命令在本地生成，随后在应用中链接相应的目标库：

```cmake
cmake_minimum_required(VERSION 3.16)
project(MyApp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
find_package(WhatsCanvas 0.1.20 CONFIG REQUIRED)

add_executable(MyApp main.cpp)
target_link_libraries(MyApp PRIVATE WhatsCanvas::Software)
```

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/path/to/WhatsCanvas/package
cmake --build build --config Release
./build/MyApp
```

Visual Studio 多配置生成器通常从 `build\Release\MyApp.exe` 运行。程序会在当前工作目录写出 `first.ppm`；可用支持 PPM 的图片查看器打开，或将其转换为 PNG。WhatsCanvas 提供 RGBA 回读和 PPM 调试输出，不内置 PNG/JPEG 编码器。

Windows 官方包是 shared 构建。运行前将包内 `bin` 加入 `PATH`，或把其中 DLL 复制到应用可执行文件旁：

```bat
set "PATH=C:\path\to\whatscanvas\bin;%PATH%"
build\Release\MyApp.exe
```

若需使用窗口内 OpenGL、OpenGL ES、Vulkan、字体注册或宿主 render target 等进阶功能，请直接查阅 **[Using WhatsCanvas as a Library](doc/GETTING_STARTED_AS_LIBRARY.md)**。该指南包含了完整的上下文生命周期介绍及可独立运行的 consumer 示例。

## 获取与构建

### 使用发布包

tagged release 的资产名为 `whatscanvas-<platform>-release-<version>.zip`，例如 `whatscanvas-win64-release-0.1.20.zip`。目录布局如下：

```text
include/wsc/                 公开头文件
lib/                         可用的渲染库
bin/                         shared 构建的运行时库（存在时）
lib/cmake/WhatsCanvas/       find_package 配置
```

各平台预编译包所包含的 target 可能有所差异。实际使用时，建议通过 CMake 显式校验所需 target 是否存在：

```cmake
find_package(WhatsCanvas 0.1.20 CONFIG REQUIRED)
if (NOT TARGET WhatsCanvas::Software)
    message(FATAL_ERROR "This package does not contain the Software backend")
endif()
```

请注意，目前发布的预编译包在三大平台上并未做到完全一致的分发：

| 发布资产 | 交付形式和 target | 字体/Vulkan 配置 |
| --- | --- | --- |
| Windows x64 | shared；OpenGL、OpenGL ES、Software | FreeType、HarfBuzz shaping 开启；Vulkan 选项开启，运行时仍需 loader/驱动/设备可用 |
| Linux x64 | static；OpenGL、Software | FreeType、HarfBuzz shaping 开启；Vulkan 关闭 |
| macOS universal | static；OpenGL、Software | FreeType、HarfBuzz shaping 开启；Vulkan 关闭 |

FreeType/HarfBuzz 配置作用于 GL 家族的 target；`WhatsCanvas::Software` 为保持独立的 CPU-only 交付，继续使用内置 `stb_truetype` 和 simple shaping。

Windows 包由 VS 2022 工具链生成。正式接入时应匹配平台、架构、配置与 C/C++ runtime；如需不同的 target 或依赖组合，请从源码构建。

官方包的具体构建参数记录在 [package-release workflow](.github/workflows/package-release.yml)；本地执行 `--package` 时会采用下文所述的默认设置，因此产物与 Windows 官方包的完整配置并不一致。

### vcpkg

仓库内提供了经过验证的 overlay port，但 vcpkg 工具本身需要单独安装，WhatsCanvas 不会内置它。在 WhatsCanvas 源码目录打开 Windows CMD，可执行：

```bat
git clone https://github.com/microsoft/vcpkg.git ..\vcpkg
..\vcpkg\bootstrap-vcpkg.bat
..\vcpkg\vcpkg.exe install whatscanvas --overlay-ports=.\ports
```

如果 `vcpkg` 已加入 `PATH`，可直接安装默认的 OpenGL、Software 与完整文本 feature 组合：

```sh
vcpkg install whatscanvas --overlay-ports=./ports
```

如果只需要 CPU 渲染，不引入 OpenGL、FreeType 或 HarfBuzz：

```sh
vcpkg install "whatscanvas[core,software]" --overlay-ports=./ports
```

随后使用 vcpkg toolchain 配置应用，并链接所需 renderer：

```cmake
find_package(WhatsCanvas CONFIG REQUIRED COMPONENTS OpenGL)
target_link_libraries(MyApp PRIVATE WhatsCanvas::OpenGL)
```

纯 CPU 渲染改用 `COMPONENTS Software` 与 `WhatsCanvas::Software`。当前 overlay port 可直接从本仓库使用；进入 vcpkg 中央注册表仍需单独完成上游审核。

### 从源码构建

源码构建需要 CMake 3.16+、C++17 编译器以及完整的 Git 子模块环境。根构建流程默认启用 OpenGL、Software、demo、测试及 benchmark，并默认输出静态库。此外，运行 OpenGL demo 另需宿主系统预装可用的 GLFW 系统图形开发库。

```sh
git clone --recursive https://github.com/ClarkWain/WhatsCanvas.git
cd WhatsCanvas
```

Windows（VS 2022）：

```bat
build.bat --no-run
```

macOS / Linux：

```bash
sh ./build.sh --no-run
```

生成适配 `find_package` 的 Release 目录结构：

```bat
build.bat --release --package --no-run
```

```bash
sh ./build.sh --release --package --no-run
```

安装目录位于 `out/package/Release/`。普通 Windows 多配置构建的 demo 通常位于 `build/Debug/` 或 `build/Release/`；默认 Unix 单配置构建通常位于 `build/`。

若已克隆仓库，可直接执行 `git submodule update --init --recursive` 更新子模块状态。构建脚本在运行时也会自动拉取所需子模块，因此初次构建请确保网络畅通；如需进行完全离线的构建，请务必提前备足各子模块的完整源码。在 Ubuntu 环境下编译 demo 时，常见的系统依赖为 `libgl1-mesa-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev`。

包管理器构建可以设置 `WHATSCANVAS_USE_SYSTEM_DEPENDENCIES=ON`，使用
registry 提供的 GLAD、GLM、stb、FreeType 和 HarfBuzz。Linux 下可通过
`WHATSCANVAS_X11` 明确控制窗口呈现支持：`AUTO` 保留源码构建时的自动
发现行为，`ON` 要求 X11 必须存在，`OFF` 则保证 headless 构建不会探测
宿主机上的 X11。

### 作为源码子目录

```cmake
set(WHATSCANVAS_BUILD_OPENGL ON CACHE BOOL "")
set(WHATSCANVAS_BUILD_SOFTWARE ON CACHE BOOL "")
set(WHATSCANVAS_BUILD_DEMO OFF CACHE BOOL "")
set(WHATSCANVAS_BUILD_BENCHMARKS OFF CACHE BOOL "")
# WhatsCanvas follows CMake's global BUILD_TESTING option. Set it OFF here only
# if the parent project does not need tests from any subproject.
# set(BUILD_TESTING OFF CACHE BOOL "")
add_subdirectory(third_party/WhatsCanvas)
target_link_libraries(MyApp PRIVATE WhatsCanvas::OpenGL)
```

只需要无 GPU 依赖的 CPU 渲染时，可以缩小构建面：

```sh
cmake -S . -B build \
  -DWHATSCANVAS_BUILD_OPENGL=OFF \
  -DWHATSCANVAS_BUILD_SOFTWARE=ON \
  -DWHATSCANVAS_BUILD_DEMO=OFF \
  -DWHATSCANVAS_BUILD_BENCHMARKS=OFF \
  -DBUILD_TESTING=OFF
```

## 后端与平台边界

| 后端 | CMake target | 默认状态 | 宿主要求 | 当前边界 |
| --- | --- | --- | --- | --- |
| **Software** | `WhatsCanvas::Software` | 开启 | 无 GPU 或图形 API | 确定性 CPU 参考实现，适合 headless、测试、截图和 fallback。 |
| **OpenGL 3.3 Core** | `WhatsCanvas::OpenGL` | 开启，主要跨平台 GL 路径 | 应用创建 GL 上下文并保持为当前上下文，提供 proc address | 桌面应用的主要 GL 渲染路径。 |
| **OpenGL ES 3.0** | `WhatsCanvas::OpenGLES` | 关闭 | 宿主 EGL/GLES context | 独立 target；Linux Mesa 执行构建和滤镜像素门禁，移动设备仍需宿主侧验证。 |
| **Vulkan** | 编入 `WhatsCanvas::OpenGL` | 关闭 | 源码构建需 Vulkan SDK；运行需 loader、驱动和可用设备 | 默认离屏；Win32 支持 Canvas 窗口呈现，其他平台的窗口 surface 仍在完善。 |
| **Metal** | Apple 平台编入 `WhatsCanvas::OpenGL` | Apple 平台默认开启 | 支持 Metal 的 macOS/iOS/tvOS 设备 | 支持离屏渲染、外部 `MTLTexture` 互操作和 `CAMetalLayer` 窗口呈现；iOS 真机/模拟器验证仍由宿主负责。 |

Vulkan 用 `-DWHATSCANVAS_ENABLE_VULKAN=ON` 启用。Metal 在 Apple 平台默认开启，可通过 `-DWHATSCANVAS_ENABLE_METAL=OFF` 关闭。两者目前都没有独立 package target：代码编入 `WhatsCanvas::OpenGL`，运行时分别用 `Backend::Vulkan` 或 `Backend::Metal` 选择设备。

OpenGL / OpenGL ES 由应用拥有窗口和上下文；Software、Vulkan 和 Metal 不需要外部 GL 上下文。所有后端都通过 `Canvas::create(Backend, width, height)` 创建，失败时返回 `nullptr`，因此可显式提供 fallback：

```cpp
using Backend = wsc::Canvas::Backend;
auto canvas = wsc::Canvas::create(
    {Backend::Vulkan, Backend::Metal, Backend::OpenGL, Backend::Software}, width, height);
if (!canvas) {
    return 1;
}
```

平台验证现状：

**测试约定**：下表中的“单元测试”主要覆盖 headless 环境下的逻辑与契约校验；“像素门禁”会实际启动对应的图形后端，并与参考输出逐像素比对；“发布包”仅表示编译、打包与 package consumer 集成流程已通过，并不等同于已在真机上完成窗口渲染的完整验证。

| 平台 | 自动化覆盖 | 备注 |
| --- | --- | --- |
| Windows x64 | MSVC 单元测试、包消费、OpenGL/Software；发布矩阵可启用 GLES、Vulkan、FreeType、HarfBuzz | DirectWrite 文本后端可选；Vulkan 窗口呈现支持 Win32。 |
| Linux x64 | GCC 构建、单元测试、OpenGL/GLES 滤镜像素门禁、包消费 | 自动化 GL 场景使用 Mesa/Xvfb；GLX 窗口呈现源码仍缺少持续验证。 |
| macOS x86_64/arm64 | 单元测试、Metal 像素/契约门禁与 universal 发布包 | Metal 默认开启，支持离屏渲染和 `CAMetalLayer` 呈现；系统 OpenGL 仍可用。 |
| iOS / Android | OpenGL ES target 与 iOS 接入说明 | 尚无常态化的真机 CI，集成前请在目标设备上自行验证。 |
| Web | 未支持 | WebAssembly / WebGL 2 桥接仍在规划。 |

详细状态见 [Cross-Platform Validation Matrix](doc/CROSS_PLATFORM_VALIDATION_MATRIX.md)、[iOS Build Notes](doc/IOS_BUILD_NOTES.md) 和 [Vulkan Backend Status](doc/vulkan-backend-status.md)。

## 能力概览

| 领域 | 主要能力 | 代表 API |
| --- | --- | --- |
| 几何与路径 | 点、线、矩形、圆角矩形、圆/椭圆/圆弧、曲线路径、fill/stroke hit-test、虚线、路径效果 | `drawPath`、`measureStrokeBounds`、`hitTestPathFill` |
| Paint | 填充/描边、解析式抗锯齿、线性/径向多 stop 渐变、14 种混合模式、真高斯阴影、采样质量、颜色矩阵 | `Paint`、`setBlendMode`、`setShadowLayer` |
| Canvas 状态 | save/restore、矩阵变换、矩形/抗锯齿路径裁剪、离屏层、quick reject | `clipPath`、`saveLayer`、`quickReject` |
| 图片 | PNG/JPEG 解码、raw RGBA、外部纹理、局部更新、contain/cover、九宫格、圆角/圆形裁剪、平铺 | `Image`、`drawImageFit`、`wrapExternalTexture` |
| 图层滤镜 | content/backdrop blur、内阴影、毛玻璃、饱和度/亮度/对比度/颗粒、颜色矩阵和 offset chain | `ImageFilter`、`ImageFilterChain`、`LayerOptions` |
| 文字 | 系统字体、fallback、weight/slant、CJK/RTL、换行/省略号、letter spacing、描边/阴影/渐变文本、text-on-path | `FontManager`、`drawTextBox`、`drawTextOnPath` |
| 输出与互操作 | 离屏图片、render-target canvas、GL framebuffer、外部 Vulkan image/Metal texture、同步/异步 RGBA 回读、窗口 present | `OutputTarget`、`wrapExternalTexture`、`readPixelsRGBAAsync`、`present` |
| 诊断 | 像素 hash/PPM、后端与字体 diagnostics、render stats、资源与 atlas 统计 | `computePixelsHashRGBA`、`RenderStats` |

### 文本实现说明

默认的跨平台文本处理体系涵盖了 UTF-8 布局、字体 fallback、CJK 无空格换行、Unicode 17.0.0 双向文本支持以及 glyph atlas 构建。已归档的 UAX #9 一致性测试覆盖 **861,948 例，全部通过，无跳过、无失败**。

需要注意的是，双向文本处理并不等同于简单的 script shaping。阿拉伯文、印度语系等语言涉及复杂的字形替换与重排，完整支持依赖 HarfBuzz。相关功能在所有 GL 家族的 target 上默认启用；上线前建议先检查 package diagnostics，并结合真实业务字体与文案做回归测试。若关闭该选项，或因依赖缺失而退化到 simple shaping，排版结果与完整 shaping 并不等价，不应作为等效替代。

- `WHATSCANVAS_ENABLE_FREETYPE_RASTERIZER=ON`（默认）：优先使用 FreeType 处理 glyph lookup、metrics、kerning 和栅格化；不可用时回退 `stb_truetype`。
- `WHATSCANVAS_ENABLE_OPENTYPE_SHAPING=ON`（默认）：启用 HarfBuzz OpenType shaping；不可用或关闭时使用 simple shaping + kerning。
- Windows 可选 DirectWrite adapter；CoreText adapter 尚未实现。
- 已支持 COLR/CPAL v0；CBDT/CBLC、SBIX、SVG 和 COLR v1 paint graph 仍是后续工作。

![WhatsCanvas 字体 fallback、CJK、双向文本与 text-on-path](images/text-rendering-showcase.png)

完整能力矩阵见 [Text Feature Matrix](doc/TEXT_FEATURE_MATRIX.md) 与 [Text Sharpness & HiDPI](doc/TEXT_SHARPNESS_AND_HIDPI.md)。

## 性能：有证据，也有适用范围

<!-- PERFORMANCE_CLAIM baseline=benchmarks/baselines/nanovg-win-i7-8700-gtx1060/matrix-summary.json wins=26 losses=0 inconclusive=1 quality=27/27 -->

在仓库当前归档的 **Windows、Core i7-8700、GTX 1060、1920 × 1080、Release、OpenGL** 画质对齐基准矩阵中，WhatsCanvas 对比 NanoVG GL3 为 **26 项领先、0 项落后、1 项持平**，并有 **27 项像素质量验证通过**。

审计元数据：Windows 10、NVIDIA 560.94、MSVC 19.43、OpenGL 3.3；每进程预热 5 帧并测量 30 帧，每个 cell 使用 2 个 ABBA block、每端 4 个新进程和 10,000 次 bootstrap；NanoVG commit 为 `ce3bf745eb2d2dbc14a50bf2446783f691ac4353`。矩阵于 2026-07-29 归档在 WhatsCanvas commit `0358151`，质量阈值与一键复现命令见基线 README。

| 场景 | 矩阵范围 | 归档结果 |
| --- | --- | --- |
| 抗锯齿几何 | 256–4,096 个图形；静态场景、动态数据、动态结构 | 8 项领先、1 项持平；最大帧时间下降 26.7% |
| 图片 | 64–1,024 张；最多 32 张纹理；圆角与状态变化 | 9/9 领先；最大帧时间下降 58.5% |
| 动态文字 | 64–1,024 次绘制；文本、字号和状态变化 | 9/9 领先；最大帧时间下降 32.0% |

以上数据仅反映特定硬件、驱动、后端与工作负载下的表现，不宜外推到其他 GPU、Software 后端、Vulkan 后端、移动设备或你的生产环境。仓库内保留了逐帧的 JSONL 明细、像素残差、ABBA 进程配对以及 95% 置信区间等原始数据，方便审计与复现。选型前，建议使用与业务贴近的 workload 自行复测。

- [完整方法与结果](doc/PERFORMANCE_BENCHMARKS.md)
- [NanoVG 参数矩阵与原始基线](benchmarks/baselines/nanovg-win-i7-8700-gtx1060/README.md)
- [跨库 benchmark 规范](doc/CROSS_LIBRARY_BENCHMARKS.md)

## 体量与依赖

本文中所说的“轻量”，指的是后端可按需分离链接、公开 API 表面较小、库不干预宿主应用的窗口与事件循环，并不意味着 header-only。

当前仓库 `0.1.20` 的一个 **VS 2022 x64、静态 Release、默认 FreeType/HarfBuzz 开启**的干净构建快照可作为量级参考：

| 内容 | 文件体量 |
| --- | ---: |
| 16 个公开头文件 | 约 74 KiB |
| `WhatsCanvasSoftware.lib` | 约 4.67 MiB |
| `WhatsCanvasOpenGL.lib` | 约 7.59 MiB |
| 随包安装的 `freetype.lib` | 约 1.78 MiB |
| 随包安装的 `harfbuzz.lib` | 约 4.49 MiB |

以上仅为静态库文件本身的大小，不能直接累加作为可执行文件的最终体积增量。链接器 dead-code 裁剪、是否嵌入调试信息、LTO 级别、C/C++ 运行时选择、字体实现（FreeType 或 `stb_truetype`）、Vulkan 模块开关、静态/动态链接选择等因素都会显著影响最终交付的体积。如需精确评估，请在实际工具链下裁掉不需要的 target，再直接测量产物。

依赖模型：

- Software target 不链接 OpenGL/Vulkan；核心图片解码和 portable font fallback 来自仓库内组件。这里的“确定性”指仓库固定实现和输入可作为回归基线，不承诺不同 OS、编译器或版本之间永远逐像素一致。
- OpenGL / OpenGL ES target 需要平台图形库；WhatsCanvas 不要求应用使用 GLFW，GLFW 只用于仓库 demo 和部分测试。
- FreeType、HarfBuzz、Vulkan 均可在构建时裁剪。根 CMake 与 `--package` 默认 FreeType `ON`、HarfBuzz shaping `ON`、Vulkan `OFF`。需要最小文本依赖时，可设置 `WHATSCANVAS_PACKAGE_ENABLE_FREETYPE=0`，并通过 `WHATSCANVAS_CMAKE_EXTRA_ARGS=-DWHATSCANVAS_ENABLE_OPENTYPE_SHAPING=OFF` 关闭 HarfBuzz。正式发布前应检查 CMake cache 或 package diagnostics。

## 成熟度与工程质量

WhatsCanvas 不只是“能画出图形”，仓库配套的工程与自动化验证包括：

- Windows、Linux、macOS 跨平台 CI；OpenGL ES、Vulkan 和 Metal 有独立构建/像素门禁。
- Software golden image 基线、OpenGL/OpenGL ES/Vulkan/Metal 滤镜结果对齐、严格 hash 回归与模糊 PPM 回归。
- 公开 API 参考文档时效性、版本一致性、package consumer 与示例构建检查。
- 同步/异步像素回读、确定性首帧时序、render stats、资源统计和可复现 benchmark。
- 公开头文件与 CMake target 的支持边界记录在 [API Stability](doc/API_STABILITY.md)，发布记录见 [CHANGELOG](CHANGELOG.md)。

仍需明确的风险：

- 版本仍是 `0.1.x`，升级前应阅读 CHANGELOG 并执行 package consumer 测试。
- README 的能力表不是所有 backend × platform 组合的完全 parity 承诺；滤镜、文字和输出目标应查对应的 feature matrix，并验证项目的实际组合。
- Vulkan 不是默认后端，跨平台窗口呈现和更大场景的像素覆盖仍在扩展。
- Metal 已在 Apple 平台可用，但 iOS 模拟器/真机 CI 与仓库内 iOS 示例仍待补齐；WebGPU、WebAssembly 和 CoreText native text adapter 尚未实现。
- 跨 GPU 的实时渲染结果可能受驱动影响；确定性基线应优先使用 Software，GPU 回归使用容差比较。
- `Canvas` 应在其渲染 / 上下文线程内使用；当前公开文档不承诺同一实例的并发访问，也未定义跨 Canvas 共享图片、字体或外部纹理的跨线程约定。

## 示例

仓库包含根 demo、API snippets、package consumer、Software/OpenGL/Vulkan/Metal present，以及两个游戏示例：

<table>
<tr>
<td width="50%" align="center"><a href="examples/game/tetris"><img src="images/tetris.jpg" alt="WhatsCanvas Tetris example" width="100%"></a><br><b>Tetris</b> — 布局、文本面板、方块与状态叠加</td>
<td width="50%" align="center"><a href="examples/game/racer"><img src="images/racer.png" alt="WhatsCanvas Racer example" width="100%"></a><br><b>Racer</b> — 滚动场景、裁剪、HUD 与动画</td>
</tr>
</table>

Windows 单独构建 Tetris：

```bat
cd examples\game\tetris
build.bat --no-run
```

## 验证你的集成

从仓库根目录运行核心单元测试：

```bat
ctest --test-dir build -C Debug -L unit --output-on-failure
```

```bash
ctest --test-dir build -C Debug -L unit --output-on-failure
```

上述命令会构建并运行带 `unit` 标签的测试。更高层的常用验证：

```bat
cmd /c scripts\smoke_test.bat
cmd /c scripts\text_pixel_regression.bat
cmd /c scripts\opengles_build_smoke.bat
cmd /c scripts\package_consumer_smoke.bat
cmd /c scripts\release_preflight.bat
```

```bash
sh ./scripts/smoke_test.sh
sh ./scripts/text_pixel_regression.sh
sh ./scripts/opengles_build_smoke.sh
sh ./scripts/package_consumer_smoke.sh
sh ./scripts/release_preflight.sh
```

发版预检覆盖 API reference、版本、单元测试和 package consumer，但不替代完整 GPU/视觉回归。基线更新规则见 [Regression Baseline Policy](doc/REGRESSION_BASELINES.md)。

## 文档导航

优先从 **[在线文档](https://clarkwain.github.io/WhatsCanvas/)** 或以下入口继续：

| 目的 | 文档 |
| --- | --- |
| 首次接入 | [Using WhatsCanvas as a Library](doc/GETTING_STARTED_AS_LIBRARY.md) |
| 查找 API | [Public API Reference](doc/API_REFERENCE.md) · [Visual API Gallery](doc/visual-api-gallery.md) |
| 评估 API 稳定性 | [API Stability](doc/API_STABILITY.md) · [CHANGELOG](CHANGELOG.md) |
| 文本和字体 | [Text Feature Matrix](doc/TEXT_FEATURE_MATRIX.md) · [DirectWrite](doc/DIRECTWRITE_TEXT_BACKEND.md) |
| 图层效果 | [Image Filters](doc/IMAGE_FILTERS.md) · [Shadow Model](doc/SHADOW_MODEL.md) · [Blend Modes](doc/BLEND_MODE_AUDIT.md) |
| 后端与平台 | [Vulkan Status](doc/vulkan-backend-status.md) · [Shader Portability](doc/SHADER_PORTABILITY.md) · [Troubleshooting](doc/TROUBLESHOOTING.md) |
| 性能和验证 | [Performance Benchmarks](doc/PERFORMANCE_BENCHMARKS.md) · [Visual Regression](doc/VISUAL_REGRESSION.md) |
| 架构与贡献 | [Architecture](doc/architecture/README.md) · [Contributing](CONTRIBUTING.md) |

## 路线与边界

WhatsCanvas 当前的主要方向是跨后端像素一致性、文本排版质量、更广的 Vulkan 与 Apple 设备覆盖，以及更可复现的性能基准。较远期方向包括 WebAssembly / WebGL 2、WebGPU，以及更完整的 color glyph 格式（CBDT/CBLC、SBIX、SVG、COLR v1）。这些方向仍在规划中，请勿视为已可直接使用的能力。

## 许可证

WhatsCanvas 以 [MIT License](LICENSE) 发布。FreeType、HarfBuzz、GLFW、stb 等第三方组件遵循各自许可证。

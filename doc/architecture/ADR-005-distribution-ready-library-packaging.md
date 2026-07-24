# ADR-005: Distribution-Ready Library Packaging

## Status

Accepted. The first distribution-ready packaging slice is implemented; deeper target splitting remains future work.

## Context

WhatsCanvas 已经从“只有 demo 的源码工程”推进到可被外部 CMake 工程消费的库形态。当前已具备：

- 稳定公共头目录 `include/wsc/`。
- `WhatsCanvas::OpenGL`、`WhatsCanvas::OpenGLES`（可选）和
  `WhatsCanvas::Software` package targets。
- Vulkan 没有独立 package target；启用 `WHATSCANVAS_ENABLE_VULKAN=ON` 且找到
  Vulkan SDK 时，它编译进 `WhatsCanvas::OpenGL`，再由 `Canvas::Backend::Vulkan`
  在运行时选择。
- `install()`、export、`WhatsCanvasConfig.cmake` 和 `WhatsCanvasConfigVersion.cmake`。
- `build.bat --package` / `build.sh --package` 生成 `out/package/<config>/`。
- 外部 consumer smoke，验证 `find_package(WhatsCanvas CONFIG REQUIRED)` 和导出 target。
- API reference、版本一致性、package consumer 和 release preflight gate。

剩余挑战不再是“能不能作为库分发”，而是继续把内部模块拆得更清楚，并让 release artifact 在真实 GitHub Actions 矩阵中持续通过。

## Decision

WhatsCanvas 后续应按“核心库 + 后端库 + 可选平台胶水 + 示例与工具分离”的方式组织，并把“可安装、可打包、可发布”作为一级目标设计。

### 1. Target 拆分

建议将当前结构逐步整理为以下 target：

- `WhatsCanvas::Core`
  - 只承载公开 Canvas API、绘制模型、命令录制、状态栈、后端抽象、与后端无关的文本与图像接口。
  - 不直接创建窗口，不直接依赖 GLFW，不直接暴露 OpenGL 细节。

- `WhatsCanvas::TextBasic`
  - 承载当前基础文本后端实现。
  - 后续如果引入 FreeType、HarfBuzz 或平台原生文本栈，可以并列扩展新的文本 target。

- `WhatsCanvas::BackendOpenGL`
  - 承载 `OpenGLRenderDevice`、GL program、纹理工具、顶点数组等 OpenGL 专属实现。
  - 只依赖后端抽象层，不反向污染 Core 的公开 API。

- `WhatsCanvas::PlatformGLFW`（可选）
  - 作为“快速接入层”存在，负责 GLFW 窗口、上下文初始化、默认事件循环辅助。
  - 这不是必须 target，但如果希望别人“很轻松就可以使用”，它会显著降低第一接入成本。

- `WhatsCanvas::OpenGL`
  - 作为面向使用者的便捷聚合 target。
  - 可以内部链接 `Core + TextBasic + BackendOpenGL + 可选 PlatformGLFW`，让大部分使用者只需要链接一个目标。

- `WhatsCanvasDemo`
  - 保持仓库内部演示程序身份，不参与安装，不作为对外库的一部分发布。

### 2. 公开头文件布局

当前公共 API 已经放在 `include/wsc/`，并通过安装包发布：

```text
include/
    wsc/
    wsc.h
    base.h
    Canvas.h
    CanvasAdapter.h
    Paint.h
    Path.h
    Image.h
    Color.h
    Export.h
    Font.h
    Log.h
    Matrix.h
    Surface.h
    TextureSource.h
    Version.h
```

对应原则：

- 使用者只需要看 `include/wsc/` 就能开始接入。
- `src/` 下的头文件默认视为内部实现细节，不直接安装。
- 任何未来希望稳定发布的类型，都必须先进入 `include/wsc/`，再谈 ABI/API 兼容。

### 3. 目录组织建议

长期建议把源码进一步整理为更清晰的模块边界，例如：

```text
src/
  core/
  recording/
  render/
  text/
  backend/
    opengl/
  platform/
    glfw/

include/
  wsc/
  wsc/backend/
  wsc/platform/

examples/
  game/

tests/
scripts/
```

当前仓库已经完成了“把验证脚本从根目录下沉到 `scripts/`”这一步，后续可以沿着同样的思路继续把“公开面”和“内部实现”分层清晰化。

### 4. CMake 选项与安装导出

当前已经具备这些核心开关和能力：

- `WHATSCANVAS_BUILD_DEMO`
- `WHATSCANVAS_INSTALL`
- `WHATSCANVAS_BUILD_OPENGL`
- `WHATSCANVAS_BUILD_OPENGLES`
- `WHATSCANVAS_BUILD_SOFTWARE`
- `WHATSCANVAS_BUILD_BENCHMARKS`
- `WHATSCANVAS_ENABLE_OPENTYPE_SHAPING`
- `WHATSCANVAS_ENABLE_FREETYPE_RASTERIZER`
- `WHATSCANVAS_ENABLE_VULKAN`

当前已经补齐：

- `install(TARGETS ...)` 安装动态库、静态库和导入库。
- `install(DIRECTORY include/ DESTINATION include)` 安装公共头文件。
- `install(EXPORT WhatsCanvasTargets ...)` 导出 target。
- 生成并安装 `WhatsCanvasConfig.cmake`。
- 生成并安装 `WhatsCanvasConfigVersion.cmake`。
- 为 Windows、Linux、macOS package workflow 整理 `lib/`、`include/`、`lib/cmake/WhatsCanvas/` 布局。

最终使用者应该可以直接这样接：

```cmake
find_package(WhatsCanvas CONFIG REQUIRED)

target_link_libraries(MyApp PRIVATE WhatsCanvas::OpenGL)
```

### 5. GitHub 发布产物

如果目标是“在 GitHub 中直接发布给别人使用”，建议每个 release 至少产出：

- Windows MSVC x64 静态库包。
- Windows MSVC x64 动态库包。
- Linux 静态/动态库包。
- macOS 静态/动态库包。
- 对应版本的头文件包。
- 对应版本的 `WhatsCanvasConfig.cmake` 包。

推荐发布结构：

```text
WhatsCanvas-<version>-win64-shared.zip
  bin/
  lib/
  include/
  cmake/

WhatsCanvas-<version>-win64-static.zip
  lib/
  include/
  cmake/
```

这样使用者不需要翻仓库，只要下载 release 包并 `find_package` 即可。

### 6. 兼容性与定位原则

库化之后也应维持当前定位：

- 不以“替代 Skia、Cocos2d”作为目标。
- 不把工程做成沉重的大一统框架。
- 保持“轻量、可裁剪、易理解”的气质。
- 对外优先提供好用的 Canvas API，而不是要求使用者理解过多底层渲染细节。

## Consequences

### Positive

- 使用者可以直接通过头文件 + 静态库/动态库接入，而不是复制源码工程。
- 多后端演进会更自然，OpenGL 不再等同于整个库本身。
- demo、example、test、script 与正式发布库的边界会更清楚。
- GitHub Release 可以成为真正可消费的分发渠道，而不仅是源码快照。

### Negative

- 需要较大规模梳理公开 API 与内部实现边界。
- 一旦开始安装与导出，就要更认真地对待 API 稳定性、命名和版本兼容。
- 构建矩阵、发布脚本和 CI 成本会上升。

## Follow-up

1. 把当前 `WhatsCanvasOpenGL` 继续拆成更清晰的 `Core` 与 `BackendOpenGL` 两层。
2. 为 examples 继续保持独立构建 smoke，避免示例依赖未公开的内部路径。
3. 视需要补 `WHATSCANVAS_BUILD_SHARED` / `WHATSCANVAS_BUILD_STATIC` 这类更明确的产物形态选项。
4. 再决定是否提供 `WhatsCanvas::PlatformGLFW` 作为“快速接入层”。
5. 在 GitHub Actions 实跑 package workflow 后，根据 artifact 结果调整 release 包布局。
6. 长期评估 CPack 或自定义 release packager，减少平台包脚本分叉。

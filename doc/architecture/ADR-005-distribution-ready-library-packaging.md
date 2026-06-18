# ADR-005: Distribution-Ready Library Packaging

## Status

Proposed.

## Context

WhatsCanvas 已经走出了“只有 demo、没有库”的第一步：当前根工程可以先构建 `WhatsCanvasOpenGL`，再让 demo 和 example 复用它。这说明工程已经具备“被当作库组织”的雏形。

但如果目标是后续在 GitHub 上直接发布动态库、静态库、头文件，并让使用者低成本接入，当前结构还存在几个明显障碍：

- 公开 API 头文件并没有真正独立成稳定的安装面，`Canvas.h`、`Paint.h`、`Image.h`、`Path.h` 等仍主要放在 `src/` 下。
- 当前主库名 `WhatsCanvasOpenGL` 仍然把“引擎核心”和“OpenGL 后端”绑在一起，不利于多后端演进。
- demo、example、tests、validation scripts 都围绕同一套源码组织，但还没有形成明确的“可安装库”与“仓库内部工具/样例”边界。
- 没有 `install()`、`export()`、`WhatsCanvasConfig.cmake`、版本文件、发布产物布局等分发能力。

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

需要把真正的公共 API 从 `src/` 中抽离到明确的安装头目录，例如：

```text
include/
  whatscanvas/
    Canvas.h
    Paint.h
    Path.h
    Image.h
    Color.h
    Geometry.h
    TextTypes.h
    Version.h
  whatscanvas/backend/
    OpenGLRenderDevice.h        # 若确实需要对外暴露
  whatscanvas/platform/
    GLFWCanvasApp.h             # 若提供快速接入层
```

对应原则：

- 使用者只需要看 `include/whatscanvas/` 就能开始接入。
- `src/` 下的头文件默认视为内部实现细节，不直接安装。
- 任何未来希望稳定发布的类型，都必须先进入 `include/whatscanvas/`，再谈 ABI/API 兼容。

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
  whatscanvas/
  whatscanvas/backend/
  whatscanvas/platform/

examples/
  game/

tests/
scripts/
```

当前仓库已经完成了“把验证脚本从根目录下沉到 `scripts/`”这一步，后续可以沿着同样的思路继续把“公开面”和“内部实现”分层清晰化。

### 4. CMake 选项与安装导出

要让别人“直接拿来用”，CMake 至少要补齐这些开关与能力：

- `WHATSCANVAS_BUILD_SHARED`
- `WHATSCANVAS_BUILD_STATIC`
- `WHATSCANVAS_BUILD_DEMO`
- `WHATSCANVAS_BUILD_EXAMPLES`
- `WHATSCANVAS_BUILD_TESTS`
- `WHATSCANVAS_BUILD_TOOLS`
- `WHATSCANVAS_INSTALL`
- `WHATSCANVAS_USE_BUNDLED_DEPS`

同时补齐以下安装导出动作：

- `install(TARGETS ...)` 安装动态库、静态库和导入库。
- `install(DIRECTORY include/ DESTINATION include)` 安装公共头文件。
- `install(EXPORT WhatsCanvasTargets ...)` 导出 target。
- 生成并安装 `WhatsCanvasConfig.cmake`。
- 生成并安装 `WhatsCanvasConfigVersion.cmake`。
- 为 Windows、Linux、macOS 分别整理 `bin/`、`lib/`、`include/`、`cmake/` 布局。

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

1. 先把 `Canvas.h`、`Paint.h`、`Path.h`、`Image.h` 等公共头迁入 `include/whatscanvas/`，建立最小公共头集合。
2. 把当前 `WhatsCanvasOpenGL` 拆成 `WhatsCanvas::Core` 与 `WhatsCanvas::BackendOpenGL` 两层。
3. 为 demo 和 examples 改成链接公开 target，而不是直接假定源码布局。
4. 补 `install()`、`export()`、`WhatsCanvasConfig.cmake` 与版本文件。
5. 再决定是否提供 `WhatsCanvas::PlatformGLFW` 作为“快速接入层”。
6. 最后接入 GitHub Actions / CPack / 自定义脚本，产出 release 二进制包。
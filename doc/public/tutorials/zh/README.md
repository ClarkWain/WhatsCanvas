# WhatsCanvas 使用教程

> 本系列教程由浅入深地介绍 WhatsCanvas 2D 渲染库的使用方法。每章独立成文，建议按顺序阅读。

## 目录

| 章节 | 标题 | 难度 | 文件 |
|:----:|------|:----:|------|
| 00 | [项目概览与能力边界](./00-whatscanvas-intro.md) | 概览 | `00-whatscanvas-intro.md` |
| 01 | [环境搭建与第一帧](./01-environment-setup.md) | 入门 | `01-environment-setup.md` |
| 02 | [基础图形绘制](./02-basic-shapes.md) | 入门 | `02-basic-shapes.md` |
| 03 | [画笔 Paint 详解](./03-paint-bindepth.md) | 初级 | `03-paint-bindepth.md` |
| 04 | [路径 Path 与曲线](./04-path-bindcurves.md) | 初级 | `04-path-bindcurves.md` |
| 05 | [状态栈与变换](./05-state-bindtransforms.md) | 初级 | `05-state-bindtransforms.md` |
| 06 | [图片绘制](./06-image-bindrawing.md) | 中级 | `06-image-bindrawing.md` |
| 07 | [文本排版](./07-text-bindlayout.md) | 中级 | `07-text-bindlayout.md` |
| 08 | [图层滤镜与特效](./08-layer-filters.md) | 中级 | `08-layer-filters.md` |
| 09 | [窗口呈现与交互](./09-windowed-presentation.md) | 中高级 | `09-windowed-presentation.md` |
| 10 | [多后端与 Fallback](./10-multi-backend.md) | 中高级 | `10-multi-backend.md` |
| 11 | [性能优化](./11-performance.md) | 高级 | `11-performance.md` |
| 12 | [跨平台实战](./12-cross-platform.md) | 高级 | `12-cross-platform.md` |

## 前置要求

- C++17 编译器 (MSVC 2019+, GCC 9+, Clang 10+)
- CMake 3.16+
- WhatsCanvas 1.1.0+（[获取与构建](https://github.com/ClarkWain/WhatsCanvas/blob/main/README_zh.md)）

## 约定

- 教程中的代码默认使用 **Software 后端**，除非章节明确说明
- `Canvas::create` / `setSize` 接收物理像素尺寸；设置 DPR 后，绘制 API 使用逻辑坐标
- `Paint::setTextSize` 接收逻辑单位，不会自动识别 Android `sp`
- 章节内的短代码用于解释单个 API；带效果图的综合示例在 `examples/tutorials/` 中提供可编译源码
- 效果图由 Software 后端示例实际输出，并以 PNG 保存，便于检查文字和图形边缘
- 综合示例代码块由对应的 `.cpp` 文件生成；修改源码后运行 `pwsh examples/tutorials/sync_docs.ps1`
- `wsc::` 为 WhatsCanvas 的命名空间前缀

## 尺寸、DPR 与文字单位（必读）

WhatsCanvas 不替宿主平台决定布局单位。Canvas 的宽高是物理像素；`setDevicePixelRatio` 建立从逻辑坐标到物理像素的映射：

```text
逻辑宽度 = 物理宽度 / DPR
逻辑高度 = 物理高度 / DPR
物理输出尺寸 = 逻辑尺寸 × DPR
```

例如，720 × 820 的离屏缓冲区使用 DPR 2 时，绘制区域是 360 × 410 个逻辑单位：

```cpp
constexpr int physicalWidth = 720;
constexpr int physicalHeight = 820;
constexpr float dpr = 2.0f;

auto canvas = wsc::Canvas::create(
    wsc::Canvas::Backend::Software, physicalWidth, physicalHeight);
canvas->setDevicePixelRatio(dpr);

const float logicalWidth = physicalWidth / dpr;    // 360
const float logicalHeight = physicalHeight / dpr;  // 410
canvas->drawRect(wsc::RectF(0, 0, logicalWidth, logicalHeight), paint);
```

此时坐标、圆角、描边和 `setTextSize` 都使用逻辑单位。不要再调用 `canvas->scale(dpr, dpr)`，否则内容会被放大两次。`getWidth()` 和 `getHeight()` 返回的仍是物理尺寸，布局时应使用自行计算并保存的逻辑宽高。

在 Android 中，通常把 `DisplayMetrics.density` 作为 DPR。这样一个 Canvas 逻辑单位可按 1 dp 使用。文字还要响应用户的字体设置，宿主应先把 sp 转成物理像素，再除以 density 后传给 `setTextSize`：

```kotlin
val metrics = resources.displayMetrics
val density = metrics.density
val bodyTextPx = TypedValue.applyDimension(
    TypedValue.COMPLEX_UNIT_SP,
    16f,
    metrics,
)
val bodyTextLogical = bodyTextPx / density

nativeResize(surfaceWidthPx, surfaceHeightPx, density)
nativeSetBodyTextSize(bodyTextLogical)
```

Android 14 及以上可能使用[非线性字体缩放](https://developer.android.com/about/versions/14/features#non-linear-font-scaling)，不要用 `16 * fontScale` 或 `scaledDensity` 手算。使用 [`TypedValue.applyDimension`](https://developer.android.com/reference/android/util/TypedValue#applyDimension(int,%20float,%20android.util.DisplayMetrics))，并在字体设置变化后重新计算文字尺寸。第 5 章进一步说明 DPR 与状态矩阵，第 7 章说明字号与行高，第 9、12 章分别给出桌面和 Web 宿主的尺寸处理方式。

## 运行综合示例

```bash
cmake -S . -B build -DWHATSCANVAS_BUILD_OPENGL=OFF -DWHATSCANVAS_BUILD_SOFTWARE=ON
cmake --build build --config Release --target chapter02_cards
./build/Release/chapter02_cards  # Windows
```

其他综合示例使用相同方式运行；将最后一个 target 替换为对应源码文件名即可。

提交前可检查教程代码块是否仍与源码一致：

```powershell
pwsh examples/tutorials/sync_docs.ps1 -Check
```

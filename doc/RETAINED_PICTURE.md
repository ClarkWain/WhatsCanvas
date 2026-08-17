# Retained Picture / Display List

如果希望先理解 Picture 要解决的性能问题、渲染 pipeline、迁移步骤和 Android
工程复盘，请阅读 [从每帧重画到可复用渲染](deep-dives/RECORD_PICTURE_DEEP_DIVE.md)。本页侧重
API 范围和约束速查。

WhatsCanvas 的 `Picture` 是一段不可变、可跨帧回放的 Canvas 操作流。它借鉴
Flutter DisplayList/SkPicture 的职责边界：核心对象只保留 CPU 侧绘制语义，不持有
OpenGL、Vulkan 或 Metal 上下文资源。

## 基本用法

```cpp
std::shared_ptr<const wsc::Picture> background =
    canvas.recordPicture([](wsc::Canvas& recordingCanvas) {
        wsc::Paint paint;
        paint.setStyle(wsc::Paint::Style::FILL);
        paint.setFillColor(wsc::Color(20, 28, 52));
        recordingCanvas.drawRoundRect(
            wsc::RectF(12.0f, 12.0f, 240.0f, 120.0f), 16.0f, paint);
    });

canvas.beginFrame();
canvas.drawPicture(*background);
drawChangingContent(canvas);
canvas.endFrame();
```

录制期间不会提交 GPU 工作，并且录制前后的矩阵、裁剪和 save 栈保持一致。
回放同样有隐式状态隔离，Picture 内部未配对的状态操作不会污染调用方。

## 当前支持范围

当前第一阶段支持：

- 点、线、矩形、圆角矩形、圆、椭圆、路径和弧；
- 文本、文本框和沿路径文本；
- Paint、渐变、阴影、混合模式；
- save/restore、矩阵变换、矩形/路径裁剪；
- 在 Context 销毁并重建后继续回放。

以下操作目前会令 `recordPicture()` 返回空指针：

- `Image` / `ITextureSource` 绘制；
- `saveLayer()`；
- `setDevicePixelRatio()`；

`restoreToCount()` 在录制时会转换为相对 pop 数量，因此 Picture 可以在任意调用方
save 栈深度下安全回放。

这些限制是有意的。当前 `Image` 是 Canvas/GPU 上下文拥有的可变资源；直接把它或
离屏 layer 纹理塞进 Picture，会在 Android 切后台、Surface 重建或跨后端回放时留下
失效资源。

## 两级派生缓存

`Picture` 本体始终只是不可变的 CPU 绘制语义。Canvas 可以从它生成两级、可丢弃的
派生缓存：

1. `drawPicture()` 缓存已编译命令。缓存键包含所属 Canvas、Context generation、
   内容 generation 和调用点的矩阵/裁剪/颜色/混合状态。首次回放会预热字体图集和
   AA 几何，后续回放不再重建高层场景。
2. `drawPictureRasterized()` 把一个明确隔离的静态 Picture 渲染为纹理，稳态只提交
   一个 textured quad。这相当于 Flutter 中由 `RepaintBoundary` 划定边界后进入
   RasterCache，而不是给所有 Picture 自动截图。

```cpp
canvas.beginFrame();
canvas.drawPictureRasterized(*background); // 仅当整层合成语义正确
drawChangingContent(canvas);
canvas.endFrame();
```

栅格缓存按 Canvas 管理，默认使用 32 MB 软预算和 LRU 驱逐。单个大于预算的最新层
仍会保留，防止每帧反复重建。可以通过 `RenderStats` 观察：

- `retainedPictureCacheHits/Misses`；
- `retainedPictureRasterCacheHits/Misses`；
- `retainedPictureRasterCacheSize/Bytes/Evictions`。

不要把 `drawPictureRasterized()` 用在依赖已有目标内容的混合、每帧变化、需要与外部
绘制交错，或不应作为一个独立合成层的 Picture 上。此 API 是显式语义边界，不是通用
“性能开关”。

## Context 生命周期

编译命令与栅格纹理都不是 `Picture` 的可移植内容。Canvas 在 `finalizeContext()`、
`releaseResources()`、尺寸/DPR/字体/后端变化时先清除这些派生项；CPU Picture 仍可在
新 Context 上重新编译和栅格化。跨 Canvas 回放只使用高层操作，不借用原 Canvas 的
派生资源。

Android 示例在普通暂停时保留健康的 EGL Context，避免无意义地丢弃约 9.5 MB 的
全屏静态层；真正 Context 丢失仍走完整重建路径。

## 性能边界和后续工作

Picture 与可选栅格层已经解决静态场景的 CPU 重建和大量静态 DrawCall，但不会自动
优化仍在变化的路径。Android 示例对一次性 fill 几何采用“两次观察后才准入”AA
缓存，防止连续变化的进度条宽度驱逐稳定几何。

后续仍按 Flutter/Impeller 的分层推进：

1. 把文本操作升级为不可变 GlyphRun/TextBlob，避免每帧重新 shaping；
2. 使用瞬态 ring/arena 或驻留几何上传已编译包，不让 Picture 本体持有 GPU buffer；
3. 增加不可变、CPU-backed Image，使图像能安全进入 Picture；
4. 为真正 EGL Context 丢失增加核心库的 abandon-context 路径；
5. 为冷启动/Context 丢失后的首次栅格化减少同步预热延迟。

Android Demo 已按“静态 Picture + 动态覆盖层”拆分。静态卡片、文字和固定几何只
录制一次并显式栅格缓存；虚线相位、进度弧、旋转元素和进度条仍逐帧生成。图片暂留
动态层，直到不可变 Image 完成。

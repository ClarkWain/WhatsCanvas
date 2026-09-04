# 从每帧重画到可复用渲染：理解 WhatsCanvas 的 `recordPicture`

假设你正在使用一个即时模式 2D 渲染库开发仪表盘。界面里有背景、八张卡片、几十段文字、图标和路径，其中只有两条曲线、一个进度环和一块进度条会动。

代码通常从一个简单的渲染循环开始：

```cpp
void renderFrame(float elapsedSeconds)
{
    canvas.beginFrame();
    drawBackground(canvas);
    drawCards(canvas);
    drawLabels(canvas);
    drawIcons(canvas);
    drawAnimations(canvas, elapsedSeconds);
    canvas.endFrame();
}
```

它很直观，也很容易维护。但当界面逐渐复杂，性能分析可能出现下面这些结果：

- `drawCards()` 和 `drawLabels()` 每帧重复创建 `Path`、`Paint`、字符串布局和临时容器；
- 相同文字每帧重新进入 shaping、字体 fallback 和 glyph atlas 查询；
- 相同路径反复 tessellate、生成抗锯齿几何并上传顶点；
- CPU 录制时间已经下降，`endFrame()` 或后端 `flush` 仍然很慢；
- 画面里只有少量动画，DrawCall 数量却与整个页面的元素数量接近。

这时，继续优化某一个圆角矩形函数通常收效有限。需要改变的是“哪些绘制工作必须每帧重做”。`recordPicture()` 就是为这个问题准备的。

## Why：即时模式为什么会重复工作

即时模式 API 描述的是“这一帧画什么”。应用每次调用 `drawPath()` 或 `drawText()`，渲染库都要把高级语义逐步转换成后端可以执行的工作。一个典型流程是：

```text
应用绘制代码
  -> Canvas 状态与绘制操作
  -> 文本 shaping / 路径 tessellation / 抗锯齿展开
  -> 后端命令与批次
  -> 顶点、索引、纹理上传
  -> DrawCall
  -> GPU 执行与显示
```

即使应用传入的数据与上一帧完全相同，这条 pipeline 也可能再次走一遍。不同渲染库会缓存字体、路径或 pipeline object，但局部缓存无法自动知道“这八张卡片作为一个整体已经三百帧没有变化”。这个判断通常只有上层应用或 UI 框架知道。

因此，使用 Picture 之前要先区分两类成本：

| 主要成本 | 常见现象 | 首选手段 |
| --- | --- | --- |
| 场景构建和高级语义转换 | record CPU 高，大量对象和临时内存；DrawCall 尚可接受 | 录制 Picture，使用 `drawPicture()` |
| 后端提交和静态 DrawCall | record CPU 已低，但 flush、上传和 DrawCall 仍高 | 在明确的隔离边界使用 `drawPictureRasterized()` |

Picture 首先解决“重复描述同一幅内容”，栅格层进一步解决“重复提交同一批静态绘制”。两者不是同一个优化。

## What：Picture 是什么

Picture 是一段不可变、可重复回放的 Canvas 操作流。它记录的是“画一个圆角矩形、设置某种 Paint、绘制一段文字、保存并恢复矩阵”之类的绘制语义，而不是最终屏幕像素。

可以把它理解成一份只读的绘制记录：

```text
recordPicture(callback)
  -> [save, clipRect, drawRoundRect, drawText, drawPath, restore, ...]
  -> immutable Picture
```

Picture 不是以下对象：

- 不是截图。它本身没有固定分辨率，也不是一张 RGBA 位图；
- 不是 GPU command buffer。它不应该直接持有某个 OpenGL、Vulkan 或 Metal Context 的资源；
- 不是完整 scene graph。它不负责节点更新、布局、命中测试或局部脏区传播；
- 不是自动性能开关。把每帧变化的内容录进去，仍然需要频繁重录或产生缓存抖动。

这种边界很重要。CPU 侧 Picture 可以在 Context 重建后重新回放，也可以在另一个 Canvas 上回放。后端命令、纹理和 buffer 则只能作为可丢弃的派生结果存在。

Picture 的不可变性不代表 Canvas 可以跨线程并发使用。应用仍应遵守宿主渲染线程约束，在拥有目标 Canvas 和当前图形 Context 的线程执行回放与提交；`shared_ptr<const Picture>` 解决的是内容生命周期，不是 Canvas 的线程调度。

类似设计可以在 Skia 的 Picture、Flutter 的 DisplayList、浏览器显示列表以及其他 retained rendering 系统中看到。名称和内部表示不同，但共同目标都是把“绘制语义”与“这一代 GPU Context 生成的资源”分开。

## Picture 加入后，渲染 pipeline 如何工作

WhatsCanvas 当前有两条 Picture 回放路径。

### 路径一：保留矢量语义的 `drawPicture()`

```text
应用首次录制
  -> backend-neutral Picture

每帧 drawPicture
  -> 查找当前 Canvas / Context / 内容 / 绘制状态对应的编译缓存
     -> 命中：复制已编译命令并加入当前帧
     -> 未命中：回放高级操作，预热字体和 AA 缓存，生成并保存命令
  -> Renderer 合批、上传和提交
```

这条路径避免每帧重新运行上层场景构建代码，并减少文字、路径和命令生成成本。它仍然保留每个绘制操作，因此 clip、矢量边缘和独立 DrawCall 语义不变。

它不保证大幅减少 DrawCall。如果一个 Picture 内有一百个无法合并的绘制操作，编译后的 Picture 仍可能提交接近一百次 draw。

### 路径二：隔离静态层的 `drawPictureRasterized()`

```text
首次 drawPictureRasterized
  -> 编译 Picture
  -> 在离屏目标执行命令
  -> 得到 Context 相关的静态纹理
  -> 本帧绘制一个 textured quad

后续帧
  -> 命中 raster cache
  -> 绘制同一个 textured quad
```

它适合语义上本来就能作为一个独立合成层的内容，例如：

- 不透明的静态页面背景；
- 长时间不变化的卡片组；
- 编辑器里的静态网格或底图；
- UI 框架中由 RepaintBoundary 一类边界隔离的子树。

栅格化会把多个矢量 draw 压缩成一个纹理 draw，但也改变了成本结构：它消耗额外显存，首次生成需要离屏渲染，而且缩放后的清晰度取决于缓存生成时的尺寸和状态。因此它只应该显式使用，不能替代普通 `drawPicture()`。WhatsCanvas 会分析操作流的保守局部 bounds，只为实际覆盖区域分配离屏纹理；阴影等无法可靠界定的操作会安全回退到全画布 bounds。

## How：把现有渲染代码迁移到 Picture

### 第一步：找到稳定边界

先不要改代码，连续采样几十或几百帧，回答下面几个问题：

- 哪些元素的几何、文字、Paint 和资源长期不变？
- 哪些元素只是整体平移或被外部裁剪？
- 哪些内容依赖当前 framebuffer 中已经存在的像素？
- 某个区域变化时，能否整体重录，而不必维护大量细粒度失效规则？

用于普通 `drawPicture()` 的边界宁可小而明确。把静态背景和动态进度条录进同一个 Picture，会迫使应用每帧重录整个 Picture，收益通常会消失。即使局部 bounds 已减少小控件的纹理占用，仍要把离屏面积、失效频率和预算压力纳入边界判断。

### 第二步：在渲染循环之外录制

```cpp
std::shared_ptr<const wsc::Picture> staticPicture;

void rebuildStaticPicture(wsc::Canvas& canvas)
{
    staticPicture = canvas.recordPicture(
        [](wsc::Canvas& recordingCanvas) {
            drawBackground(recordingCanvas);
            drawCards(recordingCanvas);
            drawLabels(recordingCanvas);
            drawStaticVectorIcons(recordingCanvas);
        });

    if (!staticPicture) {
        // 当前录制包含不支持的操作，保留即时绘制 fallback。
        reportPictureRecordingFailure();
    }
}
```

Picture 是不可变对象。当语言、主题、窗口布局或数据改变时，应用应重新录制并替换 `shared_ptr`，而不是修改旧 Picture。

实际项目应先完成 Canvas 尺寸、DPR、字体 provider、fallback 和文本后端配置，再录制依赖这些条件的 Picture。录制回调会同步执行，WhatsCanvas 把支持的绘制输入按值保存；后续业务数据变化仍需由应用显式重录。

### 第三步：先使用 `drawPicture()`

```cpp
void renderFrame(wsc::Canvas& canvas, float elapsedSeconds)
{
    canvas.beginFrame();

    if (staticPicture) {
        canvas.drawPicture(*staticPicture);
    } else {
        drawStaticScene(canvas);
    }

    drawAnimations(canvas, elapsedSeconds);
    canvas.endFrame();
}
```

先验证画面、状态隔离和 Context 生命周期，再观察 `RenderStats`。如果 record CPU 明显下降，flush 和 DrawCall 已满足预算，到这里就可以停止。保留矢量回放通常比增加整层纹理更节省内存，也更适合缩放和复杂合成。

### 第四步：确认合成语义后再启用栅格层

如果性能数据仍显示静态 DrawCall 和上传占据主要成本，并且这段内容可以整体合成，再改为：

```cpp
canvas.beginFrame();
canvas.drawPictureRasterized(*staticPicture);
drawAnimations(canvas, elapsedSeconds);
canvas.endFrame();
```

不要对下面这些内容使用栅格 Picture：

- 每帧改变几何、文字或 Paint 的内容；
- 必须与外部绘制逐项交错的内容；
- 使用 destination-dependent blend、backdrop 或其他依赖已有目标像素的操作；
- 在连续变化的 scale、clip 或变换状态下回放的内容；
- bounds 无法可靠收紧、会回退到大面积 raster target 的内容；
- 面积很大、只绘制一两次，生成成本无法摊销的内容。

### 第五步：用统计数据验证缓存是否按预期工作

```cpp
const wsc::Canvas::RenderStats stats = canvas.getRenderStats();

log("picture command cache: hit=%zu miss=%zu",
    stats.retainedPictureCacheHits,
    stats.retainedPictureCacheMisses);

log("picture raster cache: hit=%zu miss=%zu layers=%zu bytes=%zu evictions=%zu",
    stats.retainedPictureRasterCacheHits,
    stats.retainedPictureRasterCacheMisses,
    stats.retainedPictureRasterCacheSize,
    stats.retainedPictureRasterCacheBytes,
    stats.retainedPictureRasterCacheEvictions);

log("cold raster cpu(ns): prepare=%llu bounds=%llu render=%llu path=%llu text=%llu backend=%llu atlas=%llu",
    static_cast<unsigned long long>(stats.retainedPictureRasterPrepareCpuTimeNs),
    static_cast<unsigned long long>(stats.retainedPictureRasterBoundsCpuTimeNs),
    static_cast<unsigned long long>(stats.retainedPictureRasterRenderCpuTimeNs),
    static_cast<unsigned long long>(stats.retainedPictureRasterPathCpuTimeNs),
    static_cast<unsigned long long>(stats.retainedPictureRasterTextCpuTimeNs),
    static_cast<unsigned long long>(stats.retainedPictureRasterTextBackendCpuTimeNs),
    static_cast<unsigned long long>(stats.retainedPictureRasterTextAtlasCpuTimeNs));
```

稳定场景进入预热后，预期结果是 raster hit 持续出现，miss 和 eviction 很少。如果每帧都 miss，通常不是“缓存不够快”，而是调用点的矩阵、clip、DPR、尺寸或内容 generation 一直在变化。

## WhatsCanvas 如何实现 Picture

### 录制层只保存 CPU 绘制语义

`Canvas::recordPicture()` 会暂时进入录制状态。当前实现把支持的 Canvas 操作转换成只捕获值类型的回放操作；录制流不能包含 `RenderDevice`、`ImageResource` 或其他 Context 对象。

录制开始前，Canvas 会保存 graphics state、layer stack 和颜色。回调结束后，这些状态会恢复，因此录制代码里的 `save()`、`clipPath()` 或矩阵变换不会泄漏到调用方。Picture 回放也有同样的隐式状态边界。

嵌套 `recordPicture()` 会被拒绝。`restoreToCount()` 会被转换成相对 pop 数量，使 Picture 能在不同的调用方 save 深度下安全回放。

当前支持点、线、矩形、圆角矩形、圆、椭圆、路径、弧、文本、文本框、路径文字、渐变、阴影、混合、矩阵、裁剪，以及拥有 CPU RGBA 快照的 Image。Image 的 Fit、NinePatch、圆角和圆形辅助绘制会展开为同样可移植的操作。以下操作会使录制失败并返回空指针：

- `ITextureSource`、外部纹理或没有 CPU 快照的 Image 绘制；
- `saveLayer()`；
- `setDevicePixelRatio()`。

这些限制来自资源所有权，而不是 API 遗漏。录制 CPU-backed Image 时，Picture 捕获不可变的 RGBA 快照；源 Image 后续更新采用 copy-on-write，因此不会改写已录制画面。外部纹理仍只属于创建它的 GPU Context，把它直接捕获到可跨 Context 的 Picture 中会产生悬空纹理或在错误 Context 删除资源。

页面中的静态、CPU-backed 图片可以和背景、文字、矢量图标一起录制。相机、视频、SurfaceTexture、render target 或应用自行管理的 GL 纹理应继续放在动态覆盖层。

### 编译命令缓存不是 Picture 本体

同一个 Picture 在不同 Canvas、DPR、矩阵或 clip 下，最终命令可能不同。WhatsCanvas 因此使用以下信息匹配派生缓存：

- 录制它的 Canvas；
- `contextGeneration`；
- `retainedContentGeneration`；
- 当前矩阵、DPR、颜色、blend、clip、画布尺寸和输出目标组成的状态指纹。

Context 初始化会推进 `contextGeneration`。尺寸、DPR、字体集合、字体 fallback、文本后端或输出目标变化会推进内容 generation。缓存不匹配时重新编译；跨 Canvas 回放则直接使用高级操作，不借用原 Canvas 的 Context 资源。

首次编译会执行一次预热回放并丢弃它的命令，然后再生成最终命令。这样做是为了解决一个不明显的问题：第一次文字回放可能扩展 glyph atlas，早期命令仍指向已经退休的小图集；路径 AA 缓存也可能在第一次观察后才具备稳定复用条件。把冷命令直接缓存，会保留旧纹理或固化大量临时顶点。

### 栅格缓存有显存预算

`drawPictureRasterized()` 首次执行时，会计算操作流的保守局部 bounds，把一次性生成的命令以 viewport/scissor 偏移渲染到匹配该区域的离屏图像，再在原位置合成。后续帧只提交一个纹理矩形。Raster-only miss 不会额外建立 compiled-command cache：这些命令马上就会被栅格化和释放，因此无需普通 `drawPicture()` 为长期反复回放准备的预热、第二次回放和深拷贝。如果调用方之前已经建立了 compiled cache，栅格路径仍会直接复用它。当前实现处理 render target 的上下原点差异，并使用 nearest sampling，避免一比一回放时因为半像素采样引入模糊或边缘缝隙。

栅格缓存由 Canvas 管理，默认使用 32 MB 软预算和 LRU 驱逐。应用可以通过 `setRetainedPictureRasterCacheBudgetBytes()` 调整；设为 0 会完全绕过纹理缓存并回退到普通 Picture 回放。单个大于非零预算的最新层仍会保留；否则它会在每一帧被驱逐、重新生成，内存没有下降，性能反而更差。

Picture 与 Canvas 之间使用弱引用登记派生项。`finalizeContext()`、`abandonContext()`、`releaseResources()` 和 Canvas 销毁会清除相关编译命令与纹理；内容 generation 改变会使旧条目失效，并在下一次编译时移除。CPU Picture 不受影响，可以在新 Context 中重新生成派生缓存。前者用于旧 Context 仍 current 的有序释放；`abandonContext()` 用于 Context 已经丢失的情况，它只忘记 GL 名称而不调用删除 API。

## 实现过程中遇到的坑

### 只缓存高级命令，FPS 提升可能很小

Android feature-matrix demo 最初把静态场景录成 Picture 后，场景构建已经变快，但每帧仍有约 122 条命令、112 个 DrawCall 和 35 次路径上传。`pictureCpu` 只有约 0.27–0.36 ms，`flush` 却仍需约 14–21.5 ms，帧率停留在约 38–40 FPS。

这个结果说明瓶颈已经从场景构建转移到 GPU 提交。继续优化 Picture 回调不会解决一百多个静态 draw，必须为静态卡片建立明确的栅格边界。

### GPU 缓存不能成为可移植对象的事实来源

早期实验把 GPU 资源与缓存对象绑定得过紧。Android Surface 或 EGL 生命周期变化后，旧纹理可能在新 Context 中被误用或释放；切后台前后甚至可能看到画面变化。

现在的原则是：Picture 的 CPU 操作流和 Image 的 CPU 快照是事实来源，GPU 命令和 raster image 都是可丢弃的派生数据。缓存键包含 Context generation；有序 teardown 时删除资源，意外 Context loss 时只 abandon 旧名称。这个所有权关系比“是否使用纹理缓存”更重要。

### 动态几何会污染静态缓存

进度条宽度、弧线 sweep 和虚线 phase 每帧变化。如果每个新键都立即进入最终 AA 缓存，缓存会不断插入、驱逐和重建，动态 CPU 一度达到 10–28 ms。

WhatsCanvas 后来改为“两次观察后才准入”最终 fill/stroke AA 几何。一次性几何仍然正常绘制，但不会挤掉稳定缓存。调整后动态 record CPU 通常约 3.3–8.2 ms，fill AA 驻留量稳定在约 764 KB，stroke AA 稳定在约 4.3 MB。

### 普通暂停不等于 Context 丢失

Android demo 曾经在每次 `Activity.onPause()` 时销毁 NativeRenderer。恢复后虽然能够正确重建，但首个 raster miss 需要同步预热，实测约 1.25 秒。

当前示例为 `GLSurfaceView` 设置 `preserveEGLContextOnPause = true`。普通后台切换只停止 VSYNC 调度，Activity 真正结束或配置重建时才在 GL 线程释放资源。恢复前后的静态区域截图 MD5 一致，并且恢复后直接命中原 raster layer。

非自愿 EGL Context 丢失使用独立的 `Canvas::abandonContext()` 语义：旧 Context 已经消失时，只遗忘旧 GPU 名称，不能在新 Context 中调用删除 API。核心路径和单元测试已经具备；剩余工作是 managed-emulator/instrumentation 强制丢失门禁，普通 pause/resume 测试不能替代它。

### FPS 必须结合显示模式解释

同一版本在 Redmi K30 的 60 Hz 模式达到 59.0–59.6 FPS；MIUI 把屏幕切到 50 Hz mode 后，回调稳定在 49.7–50.0 FPS。后者不是渲染退化。采样时的动态 record 与 flush 合计仍低于 60 Hz 的 16.67 ms 预算。

评估 Picture 时至少同时记录 record CPU、flush CPU、DrawCall、上传量和当前显示刷新率。单独看 FPS 容易把 SurfaceFlinger 或 OEM 刷新率策略误判为渲染器瓶颈。

## 当前优化结果

在 1080 × 2305、DPR 2.75 的 Android demo 上，静态卡片、文字和固定路径进入一个不透明栅格层，动态弧线、虚线、旋转元素和进度条继续逐帧绘制：

| 指标 | 仅使用编译 Picture | 静态 raster Picture + 动态覆盖层 |
| --- | ---: | ---: |
| Commands/frame | 约 122 | 10 |
| DrawCall/frame | 约 112 | 9 |
| 路径上传/frame | 约 35 | 6 |
| 路径上传量/frame | 约 190 KB | 约 46–57 KB |
| 静态 Picture CPU/frame | 约 0.27–0.36 ms | 约 0.03–0.13 ms |
| Flush CPU/frame | 约 14–21.5 ms | 通常约 2.1–4.9 ms |
| 静态 raster 驻留 | 无 | 1 层，约 9.5 MB |

这组数据只说明当前 demo 的边界划分有效，不能推导出所有页面都应缓存整屏。一个只有少量矢量元素、经常缩放或内容频繁变化的页面，很可能更适合普通 `drawPicture()`。

## 接入时的检查清单

在其他渲染库或已有引擎中引入 Picture 思路时，可以按下面的顺序执行：

1. 先测量 record、submit/flush、DrawCall、上传和显示刷新率；
2. 找到跨帧稳定、失效条件明确的内容边界；
3. 录制 CPU 绘制语义，不把 Context 资源放进可移植 Picture；
4. 先使用矢量 Picture 回放，确认状态隔离和画面一致；
5. 只有静态 DrawCall 仍是瓶颈时，才增加显式 raster boundary；
6. 为派生缓存定义 Context、内容、状态和尺寸失效键；
7. 给纹理缓存设置字节预算、LRU 和可观测统计；
8. 分别测试冷启动、稳定帧、内容更新、resize/DPR、后台恢复和真实 Context 重建；
9. 用像素对比验证优化前后，而不是只观察“看起来差不多”；
10. 保留即时绘制 fallback，录制失败时不要提交半份 Picture。

WhatsCanvas 的简明 API 说明见 [Retained Picture / Display List](../guides/rendering/RETAINED_PICTURE.md)，Android Context 和真机数据见 [Android Integration](../platforms/ANDROID_INTEGRATION.md)。

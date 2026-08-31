# 第十一章：性能优化

> 本章目标：掌握 WhatsCanvas 的性能优化策略，包括 Picture 录制与光栅化缓存、quickReject、渲染统计、资源管理和性能测试方法。

---

## 11.1 性能优化的层次

```
应用层   → 减少不必要的绘制（quickReject、脏区域）
缓存层   → Picture 录制 + 光栅化缓存（避免重复计算）
资源层   → 纹理管理、Mipmap、Atlas
后端层   → 选择合适后端、减少状态切换
```

---

## 11.2 Picture 录制

`Picture` 将一系列绘制命令录制成一个不可变的"录像"，可以多次重放：

```cpp
// 录制绘制命令（不立即执行）
auto picture = canvas->recordPicture([](wsc::Canvas& c) {
    wsc::Paint bg;
    bg.setLinearGradient(0, 0, 400, 300,
        wsc::Color(30, 60, 120, 255), wsc::Color(20, 20, 40, 255));
    c.drawRect(wsc::RectF(0, 0, 400, 300), bg);

    wsc::Paint text;
    text.setColor(wsc::Color(255, 255, 255, 255));
    text.setTextSize(24.0f);
    c.drawText("Static Content", 50, 150, text);

    // ... 大量静态绘制操作 ...
});

// 多次重放——比重新执行所有绘制命令更快
canvas->drawPicture(*picture);
```

### 适用场景

- UI 中不随帧变化的静态部分（背景、工具栏、侧边栏）
- 复杂的图标或装饰元素
- 缓存复杂路径的绘制结果

### Picture 的属性

```cpp
picture->operationCount();  // 录制了多少个绘制命令
picture->empty();           // 是否为空
```

---

## 11.3 光栅化缓存 (Rasterized Picture)

`drawPictureRasterized` 更进一步——将 Picture 的绘制结果缓存为 GPU 纹理，后续帧直接贴图：

```cpp
// 第一次调用：执行 Picture 并将结果缓存到 GPU 纹理
// 后续调用：直接使用缓存的纹理，跳过所有绘制命令
canvas->drawPictureRasterized(*picture);
```

### 缓存预算

```cpp
// 设置缓存总预算（字节）
canvas->setRetainedPictureRasterCacheBudgetBytes(64 * 1024 * 1024);  // 64MB

// 查询当前预算
size_t budget = canvas->retainedPictureRasterCacheBudgetBytes();
```

### drawPicture vs drawPictureRasterized

| 方面 | `drawPicture` | `drawPictureRasterized` |
|------|:------------:|:---------------------:|
| 首次开销 | 低（仅重放命令） | 高（渲染 + 上传纹理） |
| 后续开销 | 中（每次重放） | 极低（纹理贴图） |
| 内存占用 | 极低 | 高（缓存纹理） |
| 变换支持 | 完美（矢量重放） | 缩放会模糊 |
| 适用场景 | 命令不多或需要缩放 | 命令量大且尺寸固定 |

---

## 11.4 Quick Reject

在大量绘制操作中，`quickReject` 可以在不做实际渲染的情况下快速判断一个区域是否完全不可见：

```cpp
// 适用于滚动列表、大型画布等场景
void drawItems(wsc::Canvas& canvas, const std::vector<Item>& items) {
    for (const auto& item : items) {
        wsc::RectF bounds = item.getBounds();

        // 如果完全在裁剪区域之外，跳过
        if (canvas.quickReject(bounds)) {
            continue;
        }

        item.draw(canvas);
    }
}
```

### 配合裁剪使用

```cpp
// 设置可见区域裁剪
canvas->clipRect(viewportRect);

// 只绘制与视口有交集的内容
for (const auto& widget : widgets) {
    if (!canvas->quickReject(widget.bounds())) {
        widget.render(*canvas);
    }
}
```

---

## 11.5 减少状态切换

### Paint 复用

```cpp
// 不好：每个元素创建新 Paint
for (auto& item : items) {
    wsc::Paint p;              // 每次构造 + 析构
    p.setColor(item.color);
    p.setTextSize(14.0f);
    p.setAntiAlias(true);
    canvas->drawText(item.text, item.x, item.y, p);
}

// 好：复用 Paint 对象，只修改变化的属性
wsc::Paint p;
p.setTextSize(14.0f);
p.setAntiAlias(true);
for (auto& item : items) {
    p.setColor(item.color);    // 只改变颜色
    canvas->drawText(item.text, item.x, item.y, p);
}
```

### 批量绘制同类型图形

```cpp
// 尽量将相同 Paint 属性的绘制操作放在一起
// GPU 后端会减少着色器/状态切换

// 先画所有背景
wsc::Paint bgPaint;
bgPaint.setColor(wsc::Color(240, 240, 240, 255));
for (auto& card : cards) {
    canvas->drawRoundRect(card.bgRect, 12, bgPaint);
}

// 再画所有文字
wsc::Paint textPaint;
textPaint.setTextSize(14.0f);
textPaint.setColor(wsc::Color(33, 33, 33, 255));
for (auto& card : cards) {
    canvas->drawText(card.title, card.titleX, card.titleY, textPaint);
}
```

---

## 11.6 图片资源优化

### Mipmap

缩小绘制图片时，Mipmap 能显著减少锯齿并提高性能：

```cpp
// 加载时生成 Mipmap（默认开启）
canvas->loadImageFromEncodedMemory(image, data, size, true /*generateMipmaps*/);

// 绘制时使用 Mipmap 采样
wsc::Paint imgPaint;
imgPaint.setImageSampling(wsc::Paint::ImageSampling::MIPMAP_LINEAR);
canvas->drawImageFit(image, dst, wsc::Canvas::ImageFit::COVER, imgPaint);
```

### 避免每帧加载

```cpp
// 不好：每帧创建纹理
void onFrame(Canvas& canvas) {
    Image img;
    canvas.loadImage(img, "icon.png");  // 每帧上传！
    canvas.drawImage(img, 10, 10, paint);
}

// 好：初始化时加载，持有引用
class MyScene {
    Image icon_;

    void onInit(Canvas& canvas) {
        canvas.loadImage(icon_, "icon.png");
    }

    void onFrame(Canvas& canvas) {
        canvas.drawImage(icon_, 10, 10, paint);
    }
};
```

### 局部更新

如果只有图片的一小部分变化，使用局部更新而非整体替换：

```cpp
// 只更新变化的区域
canvas->updateImageRGBA(image, dirtyPixels, dirtyX, dirtyY, dirtyW, dirtyH);
```

---

## 11.7 渲染统计

WhatsCanvas 提供了内置的性能统计：

```cpp
// 开启 GPU 计时
canvas->setGpuTimingEnabled(true);

// 帧结束后获取统计
canvas->endFrame();
auto stats = canvas->getRenderStats();
```

`RenderStats` 包含的信息（参考 `CanvasStats.h`）可以帮助定位性能瓶颈。

---

## 11.8 异步像素回读

同步回读 (`readPixelsRGBA`) 会导致 GPU 管线停顿。对于非即时需要的回读，使用异步版本：

```cpp
// 发起异步读取请求
canvas->readPixelsRGBAAsync([](std::vector<unsigned char> pixels, int w, int h) {
    // 回调在数据就绪时触发
    // 可以在后台线程处理像素（保存文件、上传等）
    savePNG(pixels, w, h, "screenshot.png");
});

// 在后续帧中轮询完成状态
if (canvas->hasPendingReadPixelsRGBAAsync()) {
    canvas->pollReadPixelsRGBAAsync();
}
```

---

## 11.9 帧循环优化模式

### 脏区域追踪

如果只有部分 UI 变化，可以只重绘变化部分：

```cpp
void renderFrame(Canvas& canvas, const DirtyRegion& dirty) {
    if (dirty.isEmpty()) {
        // 无需重绘
        return;
    }

    canvas.beginFrame();

    // 裁剪到脏区域
    canvas.save();
    canvas.clipRect(dirty.bounds());

    // 重绘所有与脏区域相交的内容
    for (auto& widget : widgets_) {
        if (!canvas.quickReject(widget.bounds())) {
            widget.render(canvas);
        }
    }

    canvas.restore();
    canvas.endFrame();
}
```

### 条件帧更新

对于非动画场景，只在有变化时才绘制新帧：

```cpp
bool needsRedraw = false;

void onUserInput() {
    needsRedraw = true;
}

void mainLoop() {
    while (!shouldQuit) {
        pollEvents();

        if (needsRedraw || hasAnimation) {
            canvas->beginFrame();
            draw(*canvas);
            canvas->endFrame();
            canvas->present();
            needsRedraw = false;
        } else {
            // 无变化，等待事件（省电）
            waitEvents();
        }
    }
}
```

---

## 11.10 性能测试方法

### 使用桌面平台的 Benchmark 模式

WhatsCanvas 仓库内置了 benchmark 工具：

```bash
# 运行 benchmark（warmup 30帧 + 测量 300帧）
./build/whatscanvas_desktop --scene=feature_showcase --benchmark

# 指定帧数
./build/whatscanvas_desktop --scene=geometry_stress \
    --benchmark --warmup=60 --measured=600
```

### 自定义 Benchmark

```cpp
#include <chrono>

void benchmark(Canvas& canvas, int warmupFrames, int measuredFrames) {
    // 预热
    for (int i = 0; i < warmupFrames; ++i) {
        canvas.beginFrame();
        drawScene(canvas);
        canvas.endFrame();
    }

    // 测量
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < measuredFrames; ++i) {
        canvas.beginFrame();
        drawScene(canvas);
        canvas.endFrame();
    }
    auto end = std::chrono::high_resolution_clock::now();

    float totalMs = std::chrono::duration<float, std::milli>(end - start).count();
    float avgMs = totalMs / measuredFrames;
    float fps = 1000.0f / avgMs;

    printf("Average frame time: %.2f ms (%.0f FPS)\n", avgMs, fps);
}
```

### 像素 Hash 回归

确保优化不改变输出结果：

```cpp
canvas->endFrame();
uint64_t hash = canvas->computePixelsHashRGBA();
assert(hash == expectedHash && "Pixel regression detected!");
```

---

## 11.11 交互式场景：避免大缓存随手势失效

“界面只有几十个对象”不代表每帧工作量小。拖拽时突然从 60 FPS 降到 1 FPS，常见原因不是 `drawImage` 本身，而是一次 pointer move 让整张桌面、白板或编辑器图层失效，并在当前帧重新栅格化和上传大纹理。

把场景拆成稳定层与动态层：

```text
稳定层：背景、固定工具栏、未变化的内容
动态层：拖拽对象、吸附预览、按压反馈、正在播放的动画
```

重复的小图元优先合并为一张共享 Atlas。对象只保存逻辑状态和 Atlas 源区域，不要为每个实例创建纹理：

```cpp
wsc::Paint imagePaint;
imagePaint.setColor(wsc::Color::WHITE);

for (const Card& card : visibleCards) {
    const wsc::RectF src = atlasCell(card.rank, card.suit, card.faceUp);
    canvas.drawImage(cardAtlas, src, card.bounds, imagePaint);
}

// 动态对象最后画；移动时无需重建静态桌面缓存。
canvas.drawImage(cardAtlas, dragged.src, dragged.bounds, imagePaint);
```

选择缓存方式时，先看失效模式，而不是只看静止帧的命令数：

| 情况 | 推荐策略 |
|---|---|
| 内容复杂、尺寸固定且长期不变 | `drawPictureRasterized` |
| 内容经常改变或缓存只能命中几帧 | `drawPicture` 或直接绘制 |
| 大量重复图片或 UI 零件 | 单张 Image Atlas + 源矩形 |
| 正在拖拽、缩放、旋转的对象 | 独立动态层直接绘制 |
| 只有局部变化 | 以组件或最小脏区域为缓存边界 |

尤其不要在变化的 transform 下光栅化同一个 Picture。即使 Picture 对象未变，目标尺寸、缩放或缓存签名变化也可能触发新纹理生成。首次渲染、缓存 miss 和纹理上传必须计入最慢帧，而不能只测预热后的平均 FPS。

动画观感也要单独检查。60 FPS 下，50 ms 动画理论上只有 3 帧；过强的 ease-out 或反弹会进一步压缩可见位移。拖拽吸附通常使用 120–180 ms、无反弹的 ease-out；发牌等需要看清轨迹的动作可以使用 180–260 ms，并通过错峰而不是同时启动所有对象来控制负载。

Android 蜘蛛纸牌的真实案例、原生 Canvas 对照方法和测量数据见[Android 交互式 Canvas 性能实战](../ANDROID_INTERACTIVE_PERFORMANCE.md)。

---

## 11.12 优化清单

| 优化 | 适用场景 | 效果 |
|------|---------|------|
| `drawPictureRasterized` | 复杂静态 UI 部件 | 避免每帧重算，纹理直贴 |
| `quickReject` | 长列表、大画布 | 跳过不可见元素 |
| Paint 复用 | 大量同类元素 | 减少对象构造和状态切换 |
| Mipmap + LINEAR 采样 | 图片缩小显示 | 减少锯齿，GPU 友好 |
| 局部 `updateImageRGBA` | 动态纹理 | 避免整体重传 |
| 异步像素回读 | 截图、录屏 | 不阻塞渲染管线 |
| 脏区域 + clipRect | 部分更新 UI | 减少绘制面积 |
| 条件帧更新 | 静态场景 | 省电、减少 GPU 负载 |
| 批量同属性绘制 | 列表渲染 | 减少着色器切换 |

---

## 11.13 小结

本章学习了：

- [x] Picture 录制与重放
- [x] 光栅化缓存（drawPictureRasterized）
- [x] quickReject 跳过不可见内容
- [x] 减少状态切换的技巧
- [x] 图片资源优化（Mipmap、复用、局部更新）
- [x] 渲染统计与 GPU 计时
- [x] 异步像素回读
- [x] 脏区域追踪和条件帧更新
- [x] 性能测试方法
- [x] 静态层与动态层拆分
- [x] Image Atlas 与缓存失效边界
- [x] 交互动画的帧预算与观感验证

**下一章**：[跨平台实战](./12-cross-platform.md) —— 学习 Android、iOS 和 Web 平台的集成实践。

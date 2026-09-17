# 第五章：状态栈与变换

> 本章目标：理解 Canvas 的状态管理机制（save/restore），掌握平移、缩放、旋转等坐标变换，以及矩形和路径裁剪。

---

## 5.1 Canvas 状态栈

Canvas 内部维护一个状态栈。每次调用 `save()` 会将当前状态（变换矩阵、裁剪区域）压入栈中；调用 `restore()` 会弹出并恢复之前的状态。

```
save()    → 保存当前状态
  ↓  修改变换/裁剪
  ↓  绘制内容
restore() → 恢复到 save 之前的状态
```

这是避免绘制操作相互干扰的核心机制。

```cpp
canvas->save();

// 在保存的状态下做变换和绘制
canvas->translate(100, 100);
canvas->drawRect(wsc::RectF(0, 0, 50, 50), paint);

canvas->restore();  // 变换被撤销

// 此处坐标系恢复原样
canvas->drawRect(wsc::RectF(0, 0, 50, 50), paint);  // 左上角
```

### 嵌套 save

```cpp
canvas->save();            // saveCount = 1
    canvas->translate(50, 0);
    canvas->save();        // saveCount = 2
        canvas->translate(0, 50);
        // 此时偏移 (50, 50)
    canvas->restore();     // 恢复到偏移 (50, 0)
canvas->restore();         // 恢复到无偏移
```

### restoreToCount

可以一次恢复到指定的保存层级：

```cpp
int count = canvas->save();     // 记录保存点
canvas->save();
canvas->save();
// ... 多层操作 ...
canvas->restoreToCount(count);  // 一次回到最初
```

---

## 5.2 平移 (translate)

将坐标原点移动到新位置：

```cpp
canvas->save();
canvas->translate(150, 100);

// 以 (150, 100) 为原点绘制
wsc::Paint paint;
paint.setColor(wsc::Color(66, 133, 244, 255));
canvas->drawRect(wsc::RectF(0, 0, 100, 80), paint);  // 实际位置 (150, 100)

canvas->restore();
```

---

## 5.3 缩放 (scale)

以当前原点为中心进行缩放：

```cpp
canvas->save();
canvas->translate(200, 200);  // 先移动到画布中心
canvas->scale(2.0f, 2.0f);   // 放大 2 倍

// 绘制一个"逻辑上"50x50 的矩形，实际渲染为 100x100
wsc::Paint paint;
paint.setColor(wsc::Color(76, 175, 80, 255));
canvas->drawRect(wsc::RectF(-25, -25, 50, 50), paint);

canvas->restore();
```

### 镜像翻转

```cpp
// 水平翻转
canvas->scale(-1.0f, 1.0f);

// 垂直翻转
canvas->scale(1.0f, -1.0f);
```

---

## 5.4 旋转 (rotate)

以当前原点为中心旋转，角度单位为**弧度**：

```cpp
float pi = 3.14159265f;

canvas->save();
canvas->translate(200, 200);       // 移到旋转中心
canvas->rotate(pi / 4.0f);        // 旋转 45°

wsc::Paint paint;
paint.setColor(wsc::Color(255, 152, 0, 255));
paint.setAntiAlias(true);
canvas->drawRect(wsc::RectF(-60, -60, 120, 120), paint);

canvas->restore();
```

> **关键技巧**：旋转总是绕当前原点。如果要绕某个点旋转：
> 1. `translate(cx, cy)` — 移到旋转中心
> 2. `rotate(angle)` — 旋转
> 3. `translate(-cx, -cy)` — 移回（或直接以中心为原点绘制）

---

## 5.5 变换组合

变换是**按调用顺序累积**的。顺序不同，结果不同：

```cpp
// 先平移再旋转
canvas->save();
canvas->translate(200, 200);
canvas->rotate(pi / 6);
canvas->drawRect(wsc::RectF(-40, -40, 80, 80), paint);
canvas->restore();

// 先旋转再平移（结果不同！）
canvas->save();
canvas->rotate(pi / 6);
canvas->translate(200, 200);
canvas->drawRect(wsc::RectF(-40, -40, 80, 80), paint);
canvas->restore();
```

### 矩阵操作

对于更复杂的变换，可以直接操作矩阵：

```cpp
// 获取当前矩阵
wsc::Matrix4 mat = canvas->getMatrix();

// 设置自定义矩阵
canvas->setMatrix(customMatrix);

// 重置为单位矩阵
canvas->resetMatrix();

// 连接（右乘）矩阵
canvas->concat(additionalMatrix);
```

---

## 5.6 坐标映射

将点从局部坐标映射到设备坐标（或反向）：

```cpp
canvas->save();
canvas->translate(100, 50);
canvas->scale(2.0f, 2.0f);

// 正向映射：局部坐标 → 设备坐标
wsc::PointF devicePt = canvas->mapPoint(wsc::PointF(10, 20));
// devicePt = (100 + 10*2, 50 + 20*2) = (120, 90)

// 反向映射：设备坐标 → 局部坐标（用于处理点击事件）
wsc::PointF localPt;
canvas->inverseMapPoint(wsc::PointF(120, 90), localPt);
// localPt = (10, 20)

canvas->restore();
```

---

## 5.7 裁剪 (Clip)

裁剪限制了后续绘制的可见区域。超出裁剪区域的内容不会被渲染。

### 矩形裁剪

```cpp
canvas->save();

// 设置裁剪区域
canvas->clipRect(wsc::RectF(50, 50, 200, 200));

// 这个圆会被裁剪为矩形内的部分
wsc::Paint paint;
paint.setColor(wsc::Color(244, 67, 54, 255));
paint.setAntiAlias(true);
canvas->drawCircle(150, 150, 120, paint);

canvas->restore();  // 裁剪区域也被恢复
```

### 路径裁剪

使用任意路径作为裁剪区域：

```cpp
canvas->save();

// 圆形裁剪区域
wsc::Path clipCircle;
clipCircle.addCircle(200, 200, 100);
canvas->clipPath(clipCircle);

// 在圆形区域内绘制图片或复杂内容
canvas->drawRect(wsc::RectF(0, 0, 400, 400), gradientPaint);

canvas->restore();
```

### 裁剪的累积特性

多次裁剪会取**交集**（区域越来越小）：

```cpp
canvas->save();
canvas->clipRect(wsc::RectF(50, 50, 200, 200));
canvas->clipRect(wsc::RectF(100, 100, 200, 200));
// 最终裁剪区域 = 两个矩形的交集 = (100, 100, 150, 150)
canvas->restore();
```

---

## 5.8 Quick Reject

在大量绘制操作中，`quickReject` 可以快速判断某个区域是否完全在裁剪区域之外，从而跳过不必要的绘制：

```cpp
canvas->clipRect(wsc::RectF(0, 0, 200, 200));

// 快速判断：如果完全在裁剪区外就跳过
if (!canvas->quickReject(wsc::RectF(300, 300, 100, 100))) {
    canvas->drawRect(wsc::RectF(300, 300, 100, 100), paint);
}
```

---

## 5.9 设备像素比（DPR）

DPR 不是普通的内容缩放。Canvas 仍以物理像素创建，DPR 作为根变换把逻辑坐标映射到 framebuffer：

```cpp
int framebufferWidth = 720;
int framebufferHeight = 820;
float dpr = 2.0f;

auto canvas = wsc::Canvas::create(
    wsc::Canvas::Backend::Software,
    framebufferWidth,
    framebufferHeight);
canvas->setDevicePixelRatio(dpr);

float logicalWidth = framebufferWidth / dpr;    // 360
float logicalHeight = framebufferHeight / dpr;  // 410
```

此后绘制 `RectF(0, 0, 100, 40)` 会占用 100 × 40 个逻辑单位，并输出为 200 × 80 个物理像素。文字也按当前 DPR 直接在目标分辨率栅格化，不需要先画一张低分辨率位图再放大。

需要注意以下行为：

- `Canvas::create`、`setSize`、`getWidth` 和 `getHeight` 使用物理像素。
- 绘制坐标、描边宽度和 `Paint::setTextSize` 使用逻辑单位。
- `setDevicePixelRatio` 会把当前矩阵重置为新的 DPR 根变换，应在业务 `translate`、`scale`、`rotate` 之前调用。
- `resetMatrix()` 只清除业务变换，不会丢失 DPR。
- 不要再执行 `canvas->scale(dpr, dpr)`，否则几何和文字都会重复缩放。

窗口尺寸或显示器缩放发生变化时，应一起更新物理尺寸、DPR 和逻辑布局尺寸。点击或触摸坐标也要转换到相同的逻辑坐标系后再做 hit-testing。

---

## 5.10 综合示例：旋转的花朵

![使用旋转变换组合的十瓣花朵](../images/chapter05_flower.png)

效果重点：每片花瓣都在局部坐标系中绘制，通过 `save`、`translate`、`rotate` 和 `restore` 围绕同一中心排列。辅助圆保留在效果图中，用来提示旋转半径。下方代码与生成图片的[可编译源码](https://github.com/ClarkWain/WhatsCanvas/blob/main/examples/tutorials/chapter05_flower.cpp)相同。

<!-- BEGIN GENERATED EXAMPLE: examples/tutorials/chapter05_flower.cpp -->
```cpp
// Chapter 05 comprehensive example: a flower composed with save/restore transforms.
#include <wsc/wsc.h>

#include <cmath>

int main()
{
    // 1. 创建离屏画布并定义花朵的共享几何数据。
    auto canvas = wsc::Canvas::create(wsc::Canvas::Backend::Software, 720, 720);
    if (!canvas || !canvas->initializeContext()) return 1;

    constexpr float pi = 3.14159265f, cx = 360.0f, cy = 350.0f;
    constexpr int petalCount = 10;
    const wsc::Color colors[] = {
        wsc::Color(255, 112, 132, 238), wsc::Color(255, 139, 113, 238),
        wsc::Color(250, 169, 92, 238), wsc::Color(241, 114, 151, 238),
        wsc::Color(216, 103, 169, 238), wsc::Color(180, 104, 190, 238),
        wsc::Color(139, 111, 204, 238), wsc::Color(111, 130, 215, 238),
        wsc::Color(91, 151, 217, 238), wsc::Color(91, 177, 197, 238),
    };

    // 2. 绘制背景和两条辅助圆，辅助圆用于说明旋转半径。
    canvas->beginFrame();
    wsc::Paint background;
    background.setLinearGradient(0, 0, 720, 720,
        wsc::Color(252, 248, 246, 255), wsc::Color(238, 243, 251, 255));
    canvas->drawRect(wsc::RectF(0, 0, 720, 720), background);

    wsc::Paint guide;
    guide.setStyle(wsc::Paint::Style::STROKE);
    guide.setStrokeWidth(2.0f);
    guide.setColor(wsc::Color(142, 157, 184, 34));
    guide.setAntiAlias(true);
    canvas->drawCircle(cx, cy, 205, guide);
    canvas->drawCircle(cx, cy, 92, guide);

    // 3. 先绘制位于花瓣下方的茎和叶子。
    wsc::Paint stem;
    stem.setStyle(wsc::Paint::Style::STROKE);
    stem.setStrokeWidth(18.0f);
    stem.setStrokeCap(wsc::Paint::StrokeCap::ROUND);
    stem.setColor(wsc::Color(72, 153, 116, 255));
    stem.setAntiAlias(true);
    canvas->drawLine(cx, cy + 48.0f, cx, 650.0f, stem);

    wsc::Paint leaf;
    leaf.setColor(wsc::Color(86, 177, 132, 235));
    leaf.setAntiAlias(true);
    canvas->save();
    canvas->translate(cx - 8, 535);
    canvas->rotate(-0.78f);
    canvas->drawOval(wsc::RectF(-20, -12, 110, 48), leaf);
    canvas->restore();

    canvas->save();
    canvas->translate(cx + 6, 585);
    canvas->rotate(0.72f);
    leaf.setColor(wsc::Color(58, 149, 107, 230));
    canvas->drawOval(wsc::RectF(-85, -12, 110, 48), leaf);
    canvas->restore();

    // 4. 每片花瓣都在相同局部坐标中绘制，只改变画布旋转角度。
    for (int i = 0; i < petalCount; ++i) {
        canvas->save();
        canvas->translate(cx, cy);
        canvas->rotate(i * 2.0f * pi / petalCount);
        wsc::Paint petal;
        petal.setColor(colors[i]);
        petal.setAntiAlias(true);
        petal.setShadowLayer(12, 0, 8, wsc::Color(79, 58, 92, 34));
        canvas->drawOval(wsc::RectF(-40, -226, 80, 172), petal);
        canvas->restore();
    }

    // 5. 中心圆最后绘制，用来遮住花瓣交叠处。
    wsc::Paint center;
    center.setRadialGradient(cx - 20, cy - 20, 78,
        wsc::Color(255, 232, 104, 255), wsc::Color(240, 176, 52, 255));
    center.setAntiAlias(true);
    center.setShadowLayer(14, 0, 6, wsc::Color(120, 78, 20, 50));
    canvas->drawCircle(cx, cy, 66, center);
    wsc::Paint seed;
    seed.setColor(wsc::Color(196, 125, 42, 115));
    for (int i = 0; i < 12; ++i) {
        const float a = i * 2.0f * pi / 12.0f;
        canvas->drawCircle(cx + std::cos(a) * 34, cy + std::sin(a) * 34, 4, seed);
    }

    // 6. 提交并保存结果图。
    canvas->endFrame();
    return canvas->savePixelsPPM("chapter05_flower.ppm") ? 0 : 2;
}
```
<!-- END GENERATED EXAMPLE: examples/tutorials/chapter05_flower.cpp -->

---

## 5.11 小结

本章学习了：

- [x] save/restore 状态栈机制
- [x] 平移 (translate)、缩放 (scale)、旋转 (rotate)
- [x] 变换的组合顺序与效果
- [x] 矩阵直接操作
- [x] 坐标映射（正向和反向）
- [x] 矩形裁剪和路径裁剪
- [x] Quick Reject 优化
- [x] 设备像素比

**下一章**：[图片绘制](./06-image-bindrawing.md) —— 学习加载、绘制和变换图片。

# 第二章：基础图形绘制

> 本章目标：掌握 WhatsCanvas 提供的所有基础图形绘制 API，包括矩形、圆角矩形、圆、椭圆、弧形、线段、多边形和阴影。

---

## 2.1 准备工作

本章所有示例基于以下模板：

```cpp
#include <wsc/wsc.h>

int main()
{
    auto canvas = wsc::Canvas::create(wsc::Canvas::Backend::Software, 400, 400);
    if (!canvas || !canvas->initializeContext()) return 1;

    canvas->beginFrame();

    // === 绘制代码写在这里 ===

    canvas->endFrame();
    canvas->savePixelsPPM("output.ppm");
    return 0;
}
```

后续示例仅展示"绘制代码"部分。

---

## 2.2 坐标系

WhatsCanvas 使用 **左上角为原点** 的坐标系：

```
(0,0) ────────────→ X+
  │
  │
  │
  ↓
  Y+
```

`RectF` 的构造方式为 `RectF(x, y, width, height)`，表示左上角坐标 + 宽高。

---

## 2.3 矩形

### 填充矩形

```cpp
wsc::Paint fill;
fill.setColor(wsc::Color(66, 133, 244, 255));  // Google Blue
fill.setStyle(wsc::Paint::Style::FILL);          // 默认就是 FILL

canvas->drawRect(wsc::RectF(50, 50, 120, 80), fill);
```

### 描边矩形

```cpp
wsc::Paint stroke;
stroke.setColor(wsc::Color(219, 68, 55, 255));  // Google Red
stroke.setStyle(wsc::Paint::Style::STROKE);
stroke.setStrokeWidth(3.0f);

canvas->drawRect(wsc::RectF(50, 50, 120, 80), stroke);
```

### 填充 + 描边

```cpp
wsc::Paint both;
both.setColor(wsc::Color(244, 180, 0, 255));     // 填充色：金色
both.setStyle(wsc::Paint::Style::FILL_AND_STROKE);
both.setStrokeWidth(2.0f);
both.setStrokeColor(wsc::Color(100, 100, 100, 255)); // 描边色：灰色

canvas->drawRect(wsc::RectF(50, 50, 120, 80), both);
```

---

## 2.4 圆角矩形

```cpp
wsc::Paint paint;
paint.setColor(wsc::Color(15, 157, 88, 255));
paint.setAntiAlias(true);

// 统一圆角半径
canvas->drawRoundRect(wsc::RectF(50, 50, 200, 120), 16.0f, paint);
```

圆角半径的取值：
- `radius = 0` 等价于普通矩形
- `radius = min(width, height) / 2` 等价于胶囊形

---

## 2.5 圆形

```cpp
wsc::Paint paint;
paint.setColor(wsc::Color(255, 87, 34, 255));
paint.setAntiAlias(true);

// drawCircle(centerX, centerY, radius, paint)
canvas->drawCircle(200.0f, 200.0f, 80.0f, paint);
```

---

## 2.6 椭圆

```cpp
wsc::Paint paint;
paint.setColor(wsc::Color(156, 39, 176, 255));
paint.setAntiAlias(true);

// drawOval(bounds, paint) —— bounds 是椭圆的外接矩形
canvas->drawOval(wsc::RectF(80, 120, 240, 160), paint);
```

---

## 2.7 弧形

```cpp
wsc::Paint paint;
paint.setColor(wsc::Color(0, 150, 136, 255));
paint.setAntiAlias(true);
paint.setStyle(wsc::Paint::Style::STROKE);
paint.setStrokeWidth(4.0f);

// drawArc(bounds, startAngleRad, sweepAngleRad, useCenter, paint)
// bounds: 椭圆外接矩形
// startAngleRad: 起始角度（弧度，0 = 3 点钟方向）
// sweepAngleRad: 扫过的角度
// useCenter: true 时连接圆心（扇形），false 时只画弧线

float pi = 3.14159265f;
canvas->drawArc(wsc::RectF(50, 50, 200, 200), 0, pi * 1.5f, false, paint);
```

### 填充扇形

```cpp
wsc::Paint piePaint;
piePaint.setColor(wsc::Color(255, 193, 7, 255));
piePaint.setAntiAlias(true);

canvas->drawArc(wsc::RectF(50, 50, 200, 200), 0, pi * 0.75f, true, piePaint);
```

---

## 2.8 线段

```cpp
wsc::Paint linePaint;
linePaint.setColor(wsc::Color(33, 33, 33, 255));
linePaint.setStrokeWidth(2.0f);
linePaint.setAntiAlias(true);

// drawLine(x1, y1, x2, y2, paint)
canvas->drawLine(50, 50, 350, 200, linePaint);
```

### 线帽样式

```cpp
linePaint.setStrokeCap(wsc::Paint::StrokeCap::BUTT);   // 平头（默认）
linePaint.setStrokeCap(wsc::Paint::StrokeCap::ROUND);  // 圆头
linePaint.setStrokeCap(wsc::Paint::StrokeCap::SQUARE); // 方头（超出端点）
```

---

## 2.9 多边形

```cpp
wsc::Paint polyPaint;
polyPaint.setColor(wsc::Color(63, 81, 181, 200));
polyPaint.setAntiAlias(true);

// 定义顶点
std::vector<wsc::PointF> points = {
    {200, 50},
    {350, 150},
    {300, 320},
    {100, 320},
    {50, 150}
};

canvas->drawPolygon(points.data(), points.size(), polyPaint);
```

---

## 2.10 盒阴影（Box Shadow）

类似 CSS 的 `box-shadow`，为矩形区域绘制阴影：

```cpp
// drawBoxShadow(rect, cornerRadius, spread, blur, offsetX, offsetY, color)
canvas->drawBoxShadow(
    wsc::RectF(100, 100, 200, 150),  // 矩形区域
    12.0f,                            // 圆角
    0.0f,                             // spread（扩展）
    20.0f,                            // blur（模糊半径）
    4.0f,                             // X 偏移
    8.0f,                             // Y 偏移
    wsc::Color(0, 0, 0, 80)          // 阴影颜色（半透明黑）
);

// 在阴影之上绘制卡片本体
wsc::Paint cardPaint;
cardPaint.setColor(wsc::Color(255, 255, 255, 255));
canvas->drawRoundRect(wsc::RectF(100, 100, 200, 150), 12.0f, cardPaint);
```

---

## 2.11 综合示例：绘制一组卡片

![三张由基础图形组合而成的仪表卡片](../images/chapter02_cards.png)

效果重点：卡片只使用圆角矩形、圆形、线段和盒阴影组合，没有依赖图片素材。下方代码与生成图片的[可编译源码](https://github.com/ClarkWain/WhatsCanvas/blob/main/examples/tutorials/chapter02_cards.cpp)相同。

<!-- BEGIN GENERATED EXAMPLE: examples/tutorials/chapter02_cards.cpp -->
```cpp
// Chapter 02 comprehensive example: a small dashboard made from basic shapes.
#include <wsc/wsc.h>

int main()
{
    // 1. 创建离屏画布。示例图片由 Software 后端直接输出。
    auto canvas = wsc::Canvas::create(wsc::Canvas::Backend::Software, 960, 540);
    if (!canvas || !canvas->initializeContext()) return 1;
    canvas->beginFrame();

    // 2. 绘制统一的浅色背景。
    wsc::Paint background;
    background.setColor(wsc::Color(244, 247, 252, 255));
    canvas->drawRect(wsc::RectF(0, 0, 960, 540), background);

    // 3. 每张卡片只改变强调色和进度值，布局保持一致。
    struct Card {
        float x;
        wsc::Color accent;
        float progress;
    };
    const Card cards[] = {
        {60.0f, wsc::Color(73, 109, 232, 255), 0.78f},
        {350.0f, wsc::Color(128, 86, 218, 255), 0.56f},
        {640.0f, wsc::Color(28, 167, 132, 255), 0.88f},
    };

    // 4. 组合圆角矩形、圆形和阴影，构成完整卡片。
    for (const auto &card : cards) {
        const wsc::RectF bounds(card.x, 90, 260, 360);

        // 卡片表面与顶部强调色区域。
        canvas->drawBoxShadow(bounds, 22, 0, 24, 0, 10, wsc::Color(30, 45, 80, 34));

        wsc::Paint surface;
        surface.setColor(wsc::Color(255, 255, 255, 255));
        surface.setAntiAlias(true);
        canvas->drawRoundRect(bounds, 22.0f, surface);

        wsc::Paint header;
        header.setColor(card.accent);
        header.setAntiAlias(true);
        canvas->drawRoundRect(wsc::RectF(card.x, 90, 260, 96), 22.0f, header);
        canvas->drawRect(wsc::RectF(card.x, 150, 260, 36), header);

        // 头像占位和两行标题占位。
        wsc::Paint icon;
        icon.setColor(wsc::Color(255, 255, 255, 52));
        icon.setAntiAlias(true);
        canvas->drawCircle(card.x + 46, 138, 24, icon);
        icon.setStyle(wsc::Paint::Style::STROKE);
        icon.setStrokeWidth(5.0f);
        icon.setColor(wsc::Color(255, 255, 255, 230));
        canvas->drawCircle(card.x + 46, 138, 10, icon);

        wsc::Paint highlight;
        highlight.setColor(wsc::Color(255, 255, 255, 210));
        canvas->drawRoundRect(wsc::RectF(card.x + 84, 122, 116, 12), 6, highlight);
        highlight.setColor(wsc::Color(255, 255, 255, 110));
        canvas->drawRoundRect(wsc::RectF(card.x + 84, 145, 82, 8), 4, highlight);

        // 正文骨架线。
        wsc::Paint skeleton;
        skeleton.setColor(wsc::Color(220, 226, 238, 255));
        canvas->drawRoundRect(wsc::RectF(card.x + 28, 222, 178, 12), 6, skeleton);
        canvas->drawRoundRect(wsc::RectF(card.x + 28, 250, 136, 10), 5, skeleton);
        canvas->drawRoundRect(wsc::RectF(card.x + 28, 278, 194, 10), 5, skeleton);

        // 进度条。
        wsc::Paint track;
        track.setColor(wsc::Color(231, 235, 244, 255));
        canvas->drawRoundRect(wsc::RectF(card.x + 28, 332, 204, 12), 6, track);
        track.setColor(card.accent);
        canvas->drawRoundRect(wsc::RectF(card.x + 28, 332, 204 * card.progress, 12), 6, track);

        // 底部状态标签。
        wsc::Paint badge;
        badge.setColor(wsc::Color(
            card.accent.getR(), card.accent.getG(), card.accent.getB(), 28));
        canvas->drawRoundRect(wsc::RectF(card.x + 28, 382, 86, 30), 15, badge);
        badge.setColor(card.accent);
        canvas->drawCircle(card.x + 46, 397, 5, badge);
        badge.setColor(wsc::Color(190, 198, 214, 255));
        canvas->drawRoundRect(wsc::RectF(card.x + 60, 393, 36, 8), 4, badge);
    }

    // 5. 提交绘制命令并保存与教程对应的结果图。
    canvas->endFrame();
    return canvas->savePixelsPPM("chapter02_cards.ppm") ? 0 : 2;
}
```
<!-- END GENERATED EXAMPLE: examples/tutorials/chapter02_cards.cpp -->

---

## 2.12 API 速查表

| 方法 | 参数 | 说明 |
|------|------|------|
| `drawRect` | `(RectF, Paint)` | 矩形 |
| `drawRoundRect` | `(RectF, radius, Paint)` | 圆角矩形 |
| `drawCircle` | `(cx, cy, r, Paint)` | 圆 |
| `drawOval` | `(RectF, Paint)` | 椭圆（外接矩形） |
| `drawArc` | `(RectF, start, sweep, center, Paint)` | 弧/扇 |
| `drawLine` | `(x1, y1, x2, y2, Paint)` | 线段 |
| `drawPolygon` | `(points, count, Paint)` | 多边形 |
| `drawBoxShadow` | `(RectF, radius, spread, blur, dx, dy, Color)` | 盒阴影 |

---

## 2.13 小结

本章学习了：

- [x] WhatsCanvas 的坐标系（左上角原点，X 向右，Y 向下）
- [x] 绘制矩形（填充/描边/填充+描边）
- [x] 绘制圆角矩形
- [x] 绘制圆、椭圆、弧形/扇形
- [x] 绘制线段及线帽样式
- [x] 绘制多边形
- [x] 使用盒阴影模拟卡片效果

**下一章**：[画笔 Paint 详解](./03-paint-bindepth.md) —— 深入学习颜色、渐变、阴影、混合模式等 Paint 属性。

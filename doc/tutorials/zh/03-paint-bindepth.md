# 第三章：画笔 Paint 详解

> 本章目标：深入理解 `wsc::Paint` 的所有属性，掌握颜色、渐变、阴影、透明度、混合模式、采样质量等绘制状态的配置方法。

---

## 3.1 Paint 的设计哲学

`Paint` 是一个值类型对象，描述"如何绘制"：

```cpp
wsc::Paint paint;  // 默认状态：黑色、填充、无抗锯齿
```

它不绑定任何 Canvas 实例，可以创建后多次复用。Paint 包含以下几类属性：

| 类别 | 属性 |
|------|------|
| 颜色 | 纯色、线性渐变、径向渐变 |
| 样式 | 填充、描边、填充+描边 |
| 描边 | 宽度、线帽、线连接 |
| 效果 | 阴影、混合模式、颜色矩阵 |
| 文本 | 字号、字族、粗细、对齐等 |
| 路径效果 | 虚线、圆角路径 |
| 图片 | 采样质量、平铺模式 |

---

## 3.2 颜色

### 基础颜色

```cpp
wsc::Paint paint;

// 方式一：RGBA 构造（0~255）
paint.setColor(wsc::Color(255, 100, 50, 255));

// 方式二：使用预定义常量
paint.setColor(wsc::Color::WHITE);
paint.setColor(wsc::Color::BLACK);
paint.setColor(wsc::Color::RED);
paint.setColor(wsc::Color::GREEN);
paint.setColor(wsc::Color::BLUE);
```

### 透明度

```cpp
// 独立设置 alpha（0~255）
paint.setAlpha(128);  // 50% 透明

// 也可以在 Color 构造时指定
paint.setColor(wsc::Color(66, 133, 244, 180));  // A=180
```

---

## 3.3 渐变

### 线性渐变（两色）

```cpp
wsc::Paint paint;
paint.setAntiAlias(true);

// setLinearGradient(x1, y1, x2, y2, startColor, endColor)
paint.setLinearGradient(
    0, 0,           // 起点
    200, 200,       // 终点
    wsc::Color(66, 133, 244, 255),   // 起始色：蓝
    wsc::Color(15, 157, 88, 255)     // 结束色：绿
);

canvas->drawRoundRect(wsc::RectF(50, 50, 200, 200), 20, paint);
```

### 线性渐变（多色停靠点）

```cpp
wsc::Paint paint;
paint.setAntiAlias(true);

// 使用 ColorStop 数组定义多个颜色位置
paint.setLinearGradient(0, 0, 300, 0, {
    {0.0f, wsc::Color(255, 0, 0, 255)},     // 红
    {0.3f, wsc::Color(255, 165, 0, 255)},   // 橙
    {0.5f, wsc::Color(255, 255, 0, 255)},   // 黄
    {0.7f, wsc::Color(0, 128, 0, 255)},     // 绿
    {1.0f, wsc::Color(0, 0, 255, 255)},     // 蓝
});

canvas->drawRect(wsc::RectF(20, 150, 360, 60), paint);
```

### 径向渐变

```cpp
wsc::Paint paint;
paint.setAntiAlias(true);

// setRadialGradient(centerX, centerY, radius, innerColor, outerColor)
paint.setRadialGradient(
    200, 200,       // 圆心
    120,            // 半径
    wsc::Color(255, 255, 255, 255),  // 中心：白色
    wsc::Color(33, 150, 243, 255)    // 边缘：蓝色
);

canvas->drawCircle(200, 200, 120, paint);
```

---

## 3.4 描边属性

### 描边宽度

```cpp
wsc::Paint paint;
paint.setStyle(wsc::Paint::Style::STROKE);
paint.setStrokeWidth(5.0f);  // 5 像素宽
paint.setColor(wsc::Color(33, 33, 33, 255));
```

### 线帽 (StrokeCap)

控制线段端点的形状：

```cpp
// BUTT   ——  平头，不超出端点（默认）
paint.setStrokeCap(wsc::Paint::StrokeCap::BUTT);

// ROUND  ——  圆头，超出半径等于线宽的一半
paint.setStrokeCap(wsc::Paint::StrokeCap::ROUND);

// SQUARE ——  方头，超出长度等于线宽的一半
paint.setStrokeCap(wsc::Paint::StrokeCap::SQUARE);
```

### 线连接 (StrokeJoin)

控制路径拐角处的形状：

```cpp
// MITER ——  尖角（默认）
paint.setStrokeJoin(wsc::Paint::StrokeJoin::MITER);

// ROUND ——  圆角
paint.setStrokeJoin(wsc::Paint::StrokeJoin::ROUND);

// BEVEL ——  斜切
paint.setStrokeJoin(wsc::Paint::StrokeJoin::BEVEL);
```

---

## 3.5 阴影

Paint 级别的阴影会附着在绘制的每个图形上：

```cpp
wsc::Paint paint;
paint.setColor(wsc::Color(66, 133, 244, 255));
paint.setAntiAlias(true);

// setShadowLayer(blurRadius, offsetX, offsetY, shadowColor)
paint.setShadowLayer(
    12.0f,                            // 模糊半径
    4.0f,                             // X 偏移
    6.0f,                             // Y 偏移
    wsc::Color(0, 0, 0, 100)         // 阴影颜色
);

canvas->drawRoundRect(wsc::RectF(80, 80, 200, 150), 16, paint);
```

> **注意**：Paint 阴影 vs `drawBoxShadow`
> - `setShadowLayer`：对任何形状（圆、路径等）都生效
> - `drawBoxShadow`：仅绘制矩形/圆角矩形阴影，但可单独使用

---

## 3.6 混合模式（Blend Mode）

混合模式控制新绘制内容如何与已有内容叠合：

```cpp
paint.setBlendMode(wsc::Paint::BlendMode::SRC_OVER);  // 默认：标准覆盖
```

WhatsCanvas 支持 14 种混合模式：

| 模式 | 效果说明 |
|------|---------|
| `SRC_OVER` | 标准 Alpha 合成（默认） |
| `SRC` | 完全替换目标 |
| `DST_OVER` | 在目标之下绘制 |
| `SRC_IN` | 只保留与目标重叠部分 |
| `DST_IN` | 目标只保留与源重叠部分 |
| `SRC_OUT` | 只保留不与目标重叠的部分 |
| `DST_OUT` | 目标去除与源重叠的部分 |
| `SRC_ATOP` | 在目标上方绘制，但不超出目标 |
| `DST_ATOP` | 目标显示在源区域内 |
| `XOR` | 只保留不重叠的部分 |
| `MULTIPLY` | 颜色相乘（变暗） |
| `SCREEN` | 颜色反相相乘再反相（变亮） |
| `OVERLAY` | Multiply/Screen 组合 |
| `DARKEN` | 取较暗值 |

### 示例：遮罩文字效果

```cpp
// 先画一个渐变背景
wsc::Paint gradientBg;
gradientBg.setLinearGradient(0, 0, 400, 0,
    wsc::Color(255, 0, 128, 255), wsc::Color(0, 128, 255, 255));
canvas->drawRect(wsc::RectF(0, 0, 400, 200), gradientBg);

// 使用 DST_IN 模式让后续绘制作为遮罩
wsc::Paint maskPaint;
maskPaint.setColor(wsc::Color(255, 255, 255, 255));
maskPaint.setBlendMode(wsc::Paint::BlendMode::DST_IN);
maskPaint.setTextSize(72.0f);
maskPaint.setFontFamily("Arial");
canvas->drawText("HELLO", 40, 130, maskPaint);
```

---

## 3.7 抗锯齿

```cpp
wsc::Paint paint;

// 开启：边缘平滑，适用于斜线、曲线、圆形
paint.setAntiAlias(true);

// 关闭：像素级精确，适用于对齐像素的矩形
paint.setAntiAlias(false);
```

**最佳实践**：
- 绘制曲线、斜线、圆形时始终开启
- 对齐像素边界的矩形可以关闭以获得更锐利的边缘

---

## 3.8 路径效果

### 虚线

```cpp
wsc::Paint dashPaint;
dashPaint.setStyle(wsc::Paint::Style::STROKE);
dashPaint.setStrokeWidth(3.0f);
dashPaint.setColor(wsc::Color(33, 33, 33, 255));

// setDashPathEffect({线段长度, 间隔长度, ...}, phase)
dashPaint.setDashPathEffect({10.0f, 5.0f}, 0.0f);

canvas->drawLine(50, 100, 350, 100, dashPaint);
```

复杂虚线模式：

```cpp
// 长-短-长-短 模式
dashPaint.setDashPathEffect({20.0f, 5.0f, 5.0f, 5.0f}, 0.0f);
```

### 圆角路径效果

将路径的尖角自动变为圆角：

```cpp
wsc::Paint cornerPaint;
cornerPaint.setStyle(wsc::Paint::Style::STROKE);
cornerPaint.setStrokeWidth(3.0f);
cornerPaint.setCornerPathEffect(12.0f);  // 圆角半径 12px
```

---

## 3.9 图片采样质量

绘制缩放图片时，采样质量决定了视觉效果和性能的平衡：

```cpp
wsc::Paint imgPaint;

// NEAREST: 最近邻，像素风格，最快
imgPaint.setImageSampling(wsc::Paint::ImageSampling::NEAREST);

// LINEAR: 双线性插值，平滑（默认）
imgPaint.setImageSampling(wsc::Paint::ImageSampling::LINEAR);

// MIPMAP_LINEAR: 带 Mipmap 的线性插值，缩小时最佳质量
imgPaint.setImageSampling(wsc::Paint::ImageSampling::MIPMAP_LINEAR);
```

---

## 3.10 图片平铺模式

```cpp
wsc::Paint tilePaint;

// CLAMP: 边缘像素延伸（默认）
tilePaint.setImageTileMode(wsc::Paint::ImageTileMode::CLAMP);

// REPEAT: 重复平铺
tilePaint.setImageTileMode(wsc::Paint::ImageTileMode::REPEAT);

// MIRROR: 镜像重复
tilePaint.setImageTileMode(wsc::Paint::ImageTileMode::MIRROR);
```

---

## 3.11 颜色矩阵

颜色矩阵是一个 4x5 的浮点数组，可以对像素进行线性变换：

```cpp
// 灰度化矩阵
float grayscale[20] = {
    0.2126f, 0.7152f, 0.0722f, 0, 0,  // R
    0.2126f, 0.7152f, 0.0722f, 0, 0,  // G
    0.2126f, 0.7152f, 0.0722f, 0, 0,  // B
    0,       0,       0,       1, 0,  // A
};

wsc::Paint paint;
paint.setColorMatrix(grayscale);
```

---

## 3.12 综合示例：渐变按钮组

![三种渐变按钮及其阴影和描边效果](./images/chapter03_buttons.png){ width="480" }

效果重点：示例输出 960 × 480 个物理像素，设置 DPR 2 后使用 480 × 240 的逻辑坐标，并在文档中按 480 像素宽展示。同一个按钮同时使用线性渐变、彩色阴影和一个物理像素宽的高光描边。下方代码与生成图片的[可编译源码](https://github.com/ClarkWain/WhatsCanvas/blob/main/examples/tutorials/chapter03_buttons.cpp)相同。

<!-- BEGIN GENERATED EXAMPLE: examples/tutorials/chapter03_buttons.cpp -->
```cpp
// Chapter 03 comprehensive example: gradient buttons and Paint states.
#include <wsc/FontSystem.h>
#include <wsc/wsc.h>

int main()
{
    // 1. 输出 960x480 物理像素，使用 480x240 的逻辑坐标布局。
    // 文档按 480 像素宽展示图片，因此 DPR=2 可以保留清晰边缘。
    constexpr int kPhysicalWidth = 960;
    constexpr int kPhysicalHeight = 480;
    constexpr float kDpr = 2.0f;
    constexpr float kLogicalWidth = kPhysicalWidth / kDpr;
    constexpr float kLogicalHeight = kPhysicalHeight / kDpr;

    auto canvas = wsc::Canvas::create(
        wsc::Canvas::Backend::Software, kPhysicalWidth, kPhysicalHeight);
    if (!canvas) return 1;
    canvas->setDevicePixelRatio(kDpr);
    if (!canvas->initializeContext()) return 1;
    for (const auto &face : wsc::FontSystem::defaultSystemFontFaces()) canvas->registerFontFace(face);
    canvas->setFontFallbackChain(wsc::FontSystem::defaultFallbackChain());

    // 2. 绘制页面背景和标题。
    canvas->beginFrame();
    wsc::Paint background;
    background.setLinearGradient(0, 0, kLogicalWidth, kLogicalHeight,
        wsc::Color(247, 249, 253, 255), wsc::Color(236, 241, 250, 255));
    canvas->drawRect(wsc::RectF(0, 0, kLogicalWidth, kLogicalHeight), background);

    wsc::Paint title;
    title.setColor(wsc::Color(28, 37, 58, 255));
    title.setTextSize(22.0f);
    title.setFontWeight(650);
    title.setTextAlign(wsc::Paint::TextAlign::CENTER);
    title.setTextBaseline(wsc::Paint::TextBaseline::MIDDLE);
    canvas->drawText("Paint states", kLogicalWidth / 2, 35, title);

    wsc::Paint subtitle = title;
    subtitle.setColor(wsc::Color(105, 116, 139, 255));
    subtitle.setTextSize(11.5f);
    subtitle.setFontWeight(400);
    canvas->drawText("Gradient, shadow and stroke in one compact example",
                     kLogicalWidth / 2, 57, subtitle);

    // 3. 用数据描述三个按钮，绘制逻辑只保留一份。
    struct Button {
        float x;
        wsc::Color top;
        wsc::Color bottom;
        const char *label;
        const char *caption;
    };
    const Button buttons[] = {
        {35, wsc::Color(91, 126, 246, 255), wsc::Color(62, 91, 214, 255), "Primary", "Gradient + shadow"},
        {175, wsc::Color(37, 190, 151, 255), wsc::Color(18, 145, 116, 255), "Success", "Soft highlight"},
        {315, wsc::Color(255, 112, 126, 255), wsc::Color(224, 70, 91, 255), "Danger", "High contrast"},
    };

    // 4. 逐个绘制卡片、按钮和说明文字。
    for (const auto &button : buttons) {
        // 白色承载卡片。
        const wsc::RectF card(button.x, 80, 130, 110);
        canvas->drawBoxShadow(card, 12, 0, 11, 0, 4, wsc::Color(35, 49, 83, 24));
        wsc::Paint cardPaint;
        cardPaint.setColor(wsc::Color(255, 255, 255, 245));
        cardPaint.setAntiAlias(true);
        canvas->drawRoundRect(card, 12, cardPaint);

        // 渐变按钮及同色系阴影。
        const wsc::RectF buttonRect(button.x + 15, 105, 100, 38);
        wsc::Paint fill;
        fill.setLinearGradient(button.x, 105, button.x, 143, button.top, button.bottom);
        fill.setAntiAlias(true);
        fill.setShadowLayer(7, 0, 3,
                            wsc::Color(button.bottom.getR(), button.bottom.getG(),
                                       button.bottom.getB(), 78));
        canvas->drawRoundRect(buttonRect, 9, fill);

        // 0.5 个逻辑单位在 DPR=2 时对应一个物理像素。
        wsc::Paint shine;
        shine.setStyle(wsc::Paint::Style::STROKE);
        shine.setStrokeWidth(0.5f);
        shine.setColor(wsc::Color(255, 255, 255, 92));
        shine.setAntiAlias(true);
        canvas->drawRoundRect(buttonRect, 9, shine);

        // 按钮标题和 Paint 特性说明。
        wsc::Paint label;
        label.setColor(wsc::Color(255, 255, 255, 255));
        label.setTextSize(16.0f);
        label.setFontWeight(600);
        label.setTextAlign(wsc::Paint::TextAlign::CENTER);
        label.setTextBaseline(wsc::Paint::TextBaseline::MIDDLE);
        canvas->drawText(button.label, button.x + 65, 124, label);

        wsc::Paint caption = label;
        caption.setColor(wsc::Color(108, 119, 142, 255));
        caption.setTextSize(11.5f);
        caption.setFontWeight(400);
        canvas->drawText(button.caption, button.x + 65, 166, caption);
    }

    // 5. 先结束当前帧，再保存像素；所有绘制必须发生在 endFrame 之前。
    canvas->endFrame();
    return canvas->savePixelsPPM("chapter03_buttons.ppm") ? 0 : 2;
}
```
<!-- END GENERATED EXAMPLE: examples/tutorials/chapter03_buttons.cpp -->

---

## 3.13 小结

本章学习了 Paint 的全部属性类别：

- [x] 颜色设置与透明度
- [x] 线性渐变与径向渐变（支持多色停靠点）
- [x] 描边属性：宽度、线帽、线连接
- [x] 阴影层 (`setShadowLayer`)
- [x] 14 种混合模式
- [x] 抗锯齿开关
- [x] 路径效果：虚线、圆角
- [x] 图片采样与平铺模式
- [x] 颜色矩阵变换

**下一章**：[路径 Path 与曲线](./04-path-bindcurves.md) —— 学习使用 Path 构建复杂几何形状和贝塞尔曲线。

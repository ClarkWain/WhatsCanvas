# 第七章：文本排版

> 本章目标：掌握 WhatsCanvas 的文本系统，包括字体注册与 fallback、单行/多行文本绘制、文本测量、CJK/RTL 排版，以及 text-on-path。

---

## 7.1 文本系统概述

WhatsCanvas 的文本系统远不止"画字"那么简单，它包含：

- **字体发现**：自动发现系统字体（Windows/Linux/macOS）
- **Fallback 链**：主字体找不到字形时自动回退到备选字体
- **HarfBuzz shaping**：复杂文字（阿拉伯文、天城文）正确成形
- **UAX #9 双向文本**：RTL 与 LTR 混排
- **CJK 排版**：无空格换行、标点避头尾
- **颜色 Emoji**：COLR/CPAL v0 和常见 COLRv1

---

## 7.2 字体注册

### 使用内置默认字体

WhatsCanvas 提供了内置的 fallback 字体集（用于无外部字体的场景）：

```cpp
#include <wsc/FontSystem.h>

// 注册默认系统字体
for (const auto& face : wsc::FontSystem::defaultSystemFontFaces()) {
    canvas->registerFontFace(face);
}

// 设置 fallback 链
canvas->setFontFallbackChain(wsc::FontSystem::defaultFallbackChain());
```

### 从文件注册自定义字体

```cpp
wsc::FontDescriptor desc("MyFont", 400, wsc::FontSlant::NORMAL);
wsc::FontFace face = wsc::FontFace::fromFile(desc, "/path/to/MyFont-Regular.ttf");
canvas->registerFontFace(face);

// 注册粗体变体
wsc::FontDescriptor boldDesc("MyFont", 700, wsc::FontSlant::NORMAL);
wsc::FontFace boldFace = wsc::FontFace::fromFile(boldDesc, "/path/to/MyFont-Bold.ttf");
canvas->registerFontFace(boldFace);
```

### 从内存注册

```cpp
std::vector<uint8_t> fontData = loadFontFromResource();
wsc::FontDescriptor desc("EmbeddedFont");
wsc::FontFace face = wsc::FontFace::fromMemory(desc, std::move(fontData));
canvas->registerFontFace(face);
```

### 带 Codepoint Range 的字体

当你只希望某个字体处理特定 Unicode 范围时：

```cpp
wsc::FontFace emojiFace = wsc::FontFace::fromFile(
    wsc::FontDescriptor("Emoji"), "/path/to/NotoColorEmoji.ttf");
emojiFace.addCodepointRange(0x1F600, 0x1F64F);  // Emoticons
emojiFace.addCodepointRange(0x1F300, 0x1F5FF);  // Symbols
canvas->registerFontFace(emojiFace);
```

---

## 7.3 平台原生文本后端

WhatsCanvas 支持使用平台原生文本引擎获得更好的系统一致性：

```cpp
// Windows: 使用 DirectWrite
canvas->setTextBackend(wsc::Canvas::TextBackend::DirectWrite);

// macOS / iOS: 使用 CoreText
canvas->setTextBackend(wsc::Canvas::TextBackend::CoreText);

// 跨平台默认：FreeType + HarfBuzz
canvas->setTextBackend(wsc::Canvas::TextBackend::Portable);
```

---

## 7.4 单行文本

### 字号使用逻辑单位，不是平台的 sp

`Paint::setTextSize` 接收 Canvas 逻辑单位。它不会读取 Android `density`、用户字体大小或 Web CSS，因此不能脱离 DPR 直接把参数称为 px、dp 或 sp。

如果一个 720 像素宽的 Canvas 使用 DPR 2，逻辑宽度就是 360。此时 `setTextSize(16)` 会按 16 个逻辑单位排版，并以约 32 个物理像素栅格化；在默认字体设置下，这与 Android 16sp 的视觉基准接近。若没有设置 DPR，同样的 `16` 只会输出约 16 个物理像素。

Android 还需要响应用户字体大小。宿主应使用 `TypedValue.applyDimension(COMPLEX_UNIT_SP, value, metrics)` 得到物理像素字号，再除以传给 Canvas 的 density：

```text
Canvas 逻辑字号 = Android 换算后的文字像素 / Canvas DPR
```

不要用加粗来补偿错误的字号或 DPR。正文通常从 400 字重开始，只有设计确实需要强调时才使用 500 或 600。

### 基础绘制

```cpp
wsc::Paint textPaint;
textPaint.setColor(wsc::Color(33, 33, 33, 255));
textPaint.setTextSize(24.0f);
textPaint.setFontFamily("Arial");

// drawText(text, x, y, paint)
// (x, y) 是文本基线的起始位置
canvas->drawText("Hello, WhatsCanvas!", 50, 100, textPaint);
```

### 文本对齐

```cpp
float centerX = 200;

wsc::Paint left;
left.setTextSize(20.0f);
left.setTextAlign(wsc::Paint::TextAlign::LEFT);    // 默认
canvas->drawText("Left aligned", centerX, 60, left);

wsc::Paint center;
center.setTextSize(20.0f);
center.setTextAlign(wsc::Paint::TextAlign::CENTER);
canvas->drawText("Center aligned", centerX, 100, center);

wsc::Paint right;
right.setTextSize(20.0f);
right.setTextAlign(wsc::Paint::TextAlign::RIGHT);
canvas->drawText("Right aligned", centerX, 140, right);
```

### 字体样式

```cpp
wsc::Paint paint;
paint.setTextSize(20.0f);

// 字族
paint.setFontFamily("Roboto");

// 字重 (100~1000, 400=normal, 700=bold)
paint.setFontWeight(700);

// 斜体
paint.setFontSlant(wsc::FontSlant::ITALIC);

// 字间距
paint.setLetterSpacing(2.0f);

// 下划线
paint.setUnderline(true);

// 删除线
paint.setStrikethrough(true);
```

### OpenType 特性

```cpp
// 启用连字
paint.setFontFeature("liga", 1);

// 禁用连字
paint.setFontFeature("liga", 0);

// 表格数字（等宽数字）
paint.setFontFeature("tnum", 1);

// 小写数字
paint.setFontFeature("onum", 1);
```

---

## 7.5 文本测量

### 测量宽度

```cpp
float width = canvas->measureText("Hello World", textPaint);
```

### 测量完整指标

```cpp
wsc::Canvas::TextMetrics metrics = canvas->measureTextMetrics("Hello", textPaint);

// metrics.width      — 文本宽度
// metrics.height     — 行高
// metrics.ascent     — 基线以上的高度（负值）
// metrics.descent    — 基线以下的深度
// metrics.lineGap    — 推荐行间距
// metrics.lineHeight — 推荐行高 (ascent + descent + lineGap)
// metrics.bounds     — 墨水包围盒 (RectF)
```

### 测量包围盒

```cpp
wsc::RectF bounds = canvas->measureTextBounds("Hello", textPaint);
// bounds 包含了文本的精确像素包围盒
```

---

## 7.6 多行文本

### 基础多行

```cpp
wsc::Paint paint;
paint.setTextSize(16.0f);
paint.setColor(wsc::Color(33, 33, 33, 255));

// drawTextBox(text, bounds, paint)
// 文本会在 bounds 内自动换行
canvas->drawTextBox(
    u8"WhatsCanvas 支持多行文本绘制。文本会自动在指定矩形区域内换行显示，"
    u8"支持中文无空格换行、标点避头尾等排版规则。",
    wsc::RectF(50, 50, 300, 200),
    paint
);
```

### 指定行高

```cpp
// drawTextBox(text, bounds, lineHeight, paint)
canvas->drawTextBox(
    u8"这是一段行间距为 1.8 倍的文本。",
    wsc::RectF(50, 50, 300, 200),
    1.8f,   // 行高倍数
    paint
);
```

### 最大行数 + 省略号

```cpp
// drawTextBox(text, bounds, lineHeight, maxLines, ellipsize, paint)
canvas->drawTextBox(
    u8"这是一段很长的文本，超过最大行数后会以省略号截断。"
    u8"WhatsCanvas 会自动计算断行位置，确保省略号出现在正确的位置。",
    wsc::RectF(50, 50, 250, 100),
    1.5f,    // 行高
    3,       // 最多 3 行
    true,    // 启用省略号
    paint
);
```

### 获取排版结果

可以获取排版后每一行的详细信息，用于自定义渲染或命中测试：

```cpp
auto lines = canvas->layoutTextBox(
    u8"First line.\nSecond line that wraps around.",
    wsc::RectF(0, 0, 200, 400),
    1.5f, 0, false, paint
);

for (const auto& line : lines) {
    // line.text         — 该行的文本内容
    // line.x, line.y    — 该行的绘制坐标
    // line.width        — 该行的宽度
    // line.lineHeight   — 该行的行高
    // line.ellipsized   — 该行是否被省略号截断
    // line.sourceStart  — 在原始文本中的起始位置
    // line.sourceLength — 对应原始文本的字符数
}
```

---

## 7.7 文本特效

### 描边文字

```cpp
wsc::Paint strokeText;
strokeText.setTextSize(48.0f);
strokeText.setStyle(wsc::Paint::Style::STROKE);
strokeText.setStrokeWidth(2.0f);
strokeText.setColor(wsc::Color(33, 33, 33, 255));
canvas->drawText("Outline", 50, 100, strokeText);
```

### 渐变文字

```cpp
wsc::Paint gradText;
gradText.setTextSize(48.0f);
gradText.setLinearGradient(50, 0, 350, 0,
    wsc::Color(255, 0, 128, 255), wsc::Color(0, 128, 255, 255));
canvas->drawText("Gradient Text", 50, 100, gradText);
```

### 阴影文字

```cpp
wsc::Paint shadowText;
shadowText.setTextSize(36.0f);
shadowText.setColor(wsc::Color(255, 255, 255, 255));
shadowText.setShadowLayer(6.0f, 2.0f, 3.0f, wsc::Color(0, 0, 0, 150));
canvas->drawText("Shadow Text", 50, 100, shadowText);
```

---

## 7.8 Text on Path

沿路径绘制文字：

```cpp
// 创建一条弧线路径
wsc::Path arc;
arc.addCircle(200, 200, 120);  // 沿圆形绘制

wsc::Paint textPaint;
textPaint.setTextSize(18.0f);
textPaint.setColor(wsc::Color(66, 133, 244, 255));

// drawTextOnPath(text, path, hOffset, vOffset, paint)
// hOffset: 沿路径方向的起始偏移
// vOffset: 垂直于路径的偏移（正值 = 向外）
canvas->drawTextOnPath(u8"Text flowing along a circular path", arc, 0, 0, textPaint);
```

### 波浪文字

```cpp
wsc::Path wave;
wave.moveTo(0, 150);
wave.cubicTo(100, 50, 200, 250, 300, 150);
wave.cubicTo(400, 50, 500, 250, 600, 150);

canvas->drawTextOnPath("Wavy text on a cubic bezier curve", wave, 0, -10, textPaint);
```

---

## 7.9 CJK 与多语言排版

WhatsCanvas 原生支持 CJK 文本排版：

```cpp
wsc::Paint paint;
paint.setTextSize(16.0f);
paint.setFontFamily(wsc::FontSystem::kDefaultCjkFamily);

// CJK 文本会正确地在字符间断行（无需空格）
canvas->drawTextBox(
    u8"中日韩文本排版支持：WhatsCanvas 能够正确处理中文、日本語、한국어的换行，"
    u8"并且支持标点符号的避头尾规则。括号（如这样）不会出现在行首。",
    wsc::RectF(50, 50, 280, 300),
    1.6f, 0, false, paint
);
```

### RTL 文本（阿拉伯文/希伯来文）

```cpp
wsc::Paint rtlPaint;
rtlPaint.setTextSize(20.0f);
rtlPaint.setFontFamily(wsc::FontSystem::kDefaultArabicFamily);
rtlPaint.setTextAlign(wsc::Paint::TextAlign::RIGHT);

// UAX #9 双向算法自动处理文本方向
canvas->drawText(u8"مرحبا بالعالم", 350, 100, rtlPaint);

// 混合方向文本也能正确处理
canvas->drawText(u8"Hello مرحبا World عالم!", 350, 140, rtlPaint);
```

---

## 7.10 Font Fallback 链

Fallback 链决定了当主字体缺少某个字形时，依次尝试哪些备选字体：

```cpp
// 创建自定义 fallback 链
wsc::FontFallbackChain chain("Roboto");
chain.addFallbackFamily("Noto Sans SC");       // 中文回退
chain.addFallbackFamily("Noto Sans JP");       // 日文回退
chain.addFallbackFamily("Noto Sans Arabic");   // 阿拉伯文回退
chain.addFallbackFamily("Noto Color Emoji");   // Emoji 回退

canvas->setFontFallbackChain(chain);
```

使用默认 fallback 链（推荐）：

```cpp
canvas->setFontFallbackChain(wsc::FontSystem::defaultFallbackChain());
```

---

## 7.11 系统字体发现

在桌面平台上可以枚举已安装的字体：

```cpp
// 发现系统中所有已安装的字体
auto faces = wsc::FontSystem::discoverInstalledFontFaces();

for (const auto& face : faces) {
    printf("Family: %s, Weight: %d, Slant: %d\n",
        face.family().c_str(),
        face.weight(),
        static_cast<int>(face.slant()));
}
```

---

## 7.12 综合示例：聊天气泡

![同时展示中文、阿拉伯文、Emoji 和自动换行的聊天界面](./images/chapter07_chat.png)

效果重点：示例输出 720 × 820 个物理像素，设置 DPR 2 后使用 360 × 410 的逻辑坐标；聊天正文采用 16 个逻辑单位和 400 字重。中文使用系统字体 fallback，阿拉伯文按 RTL 方向排版，长消息由 `drawTextBox` 控制行数。下方代码与生成图片的[可编译源码](https://github.com/ClarkWain/WhatsCanvas/blob/main/examples/tutorials/chapter07_chat.cpp)相同。

<!-- BEGIN GENERATED EXAMPLE: examples/tutorials/chapter07_chat.cpp -->
```cpp
// Chapter 07 comprehensive example: multilingual chat bubbles and wrapping.
#include <wsc/FontSystem.h>
#include <wsc/wsc.h>

int main()
{
    // 1. 输出 720x820 物理像素，但使用 360x410 的逻辑坐标布局。
    // DPR 只决定逻辑单位如何映射到物理像素，不需要手动放大每个坐标。
    constexpr int kPhysicalWidth = 720;
    constexpr int kPhysicalHeight = 820;
    constexpr float kDpr = 2.0f;
    constexpr float kLogicalWidth = kPhysicalWidth / kDpr;
    constexpr float kLogicalHeight = kPhysicalHeight / kDpr;

    auto canvas = wsc::Canvas::create(
        wsc::Canvas::Backend::Software, kPhysicalWidth, kPhysicalHeight);
    if (!canvas) return 1;
    canvas->setDevicePixelRatio(kDpr);
    if (!canvas->initializeContext()) return 1;
    for (const auto &face : wsc::FontSystem::defaultSystemFontFaces()) {
        canvas->registerFontFace(face);
    }
    canvas->setFontFallbackChain(wsc::FontSystem::defaultFallbackChain());

    // 2. 绘制聊天页面背景和标题栏。
    canvas->beginFrame();
    wsc::Paint background;
    background.setColor(wsc::Color(239, 243, 249, 255));
    canvas->drawRect(wsc::RectF(0, 0, kLogicalWidth, kLogicalHeight), background);

    wsc::Paint header;
    header.setLinearGradient(0, 0, kLogicalWidth, 58,
        wsc::Color(82, 116, 242, 255), wsc::Color(93, 86, 216, 255));
    canvas->drawRect(wsc::RectF(0, 0, kLogicalWidth, 58), header);

    wsc::Paint title;
    title.setColor(wsc::Color(255, 255, 255, 255));
    title.setTextSize(20.0f);
    title.setFontWeight(650);
    title.setTextAlign(wsc::Paint::TextAlign::CENTER);
    title.setTextBaseline(wsc::Paint::TextBaseline::MIDDLE);
    canvas->drawText("WhatsCanvas Chat", kLogicalWidth / 2, 22, title);
    title.setTextSize(10.5f);
    title.setFontWeight(400);
    title.setColor(wsc::Color(235, 239, 255, 210));
    canvas->drawText("CJK · RTL · Emoji · Auto wrapping", kLogicalWidth / 2, 42, title);

    // 3. 气泡函数统一处理左右布局、阴影、RTL 和自动换行。
    auto drawBubble = [&](const char *text, float y, float width, bool isMe,
                          bool rtl = false, bool multiline = false) {
        const float height = multiline ? 58.0f : 44.0f;
        const float x = isMe ? kLogicalWidth - 20.0f - width : 20.0f;
        const wsc::RectF bounds(x, y, width, height);
        canvas->drawBoxShadow(bounds, 18, 0, 5, 0, 2, wsc::Color(33, 48, 78, 28));
        wsc::Paint bubble;
        bubble.setColor(isMe ? wsc::Color(82, 116, 242, 255) : wsc::Color(255, 255, 255, 255));
        bubble.setAntiAlias(true);
        canvas->drawRoundRect(bounds, 18, bubble);

        wsc::Paint textPaint;
        textPaint.setColor(isMe ? wsc::Color(255, 255, 255, 255) : wsc::Color(35, 43, 60, 255));
        // 16 个逻辑单位在 DPR=2 时栅格化为约 32 个物理像素。
        // 这里显式使用正文的 Regular 字重，不靠加粗弥补字号过小。
        textPaint.setTextSize(16.0f);
        textPaint.setFontWeight(400);
        textPaint.setTextBaseline(wsc::Paint::TextBaseline::MIDDLE);
        textPaint.setTextLocale(rtl ? "ar" : "zh-CN");
        if (rtl) {
            textPaint.setTextAlign(wsc::Paint::TextAlign::RIGHT);
            canvas->drawText(text, x + width - 16, y + height / 2, textPaint);
        } else if (multiline) {
            textPaint.setTextBaseline(wsc::Paint::TextBaseline::TOP);
            canvas->drawTextBox(text, wsc::RectF(x + 16, y + 8, width - 32, height - 12),
                                22.0f, 2, true, textPaint);
        } else {
            canvas->drawText(text, x + 16, y + height / 2, textPaint);
        }
    };

    // 4. 使用中文、阿拉伯文和 Emoji 验证字体 fallback 与 shaping。
    drawBubble(u8"你好，欢迎体验 WhatsCanvas。", 72, 235, false);
    drawBubble(u8"文字排版看起来很顺滑。", 122, 215, true);
    drawBubble(u8"مرحبا! يدعم النص من اليمين.", 172, 240, false, true);
    drawBubble(u8"Emoji fallback works ✨ 🎨", 222, 220, true);
    drawBubble(u8"长文本会自动换行；超过指定行数时，可以使用省略号收尾。",
               272, 265, false, false, true);

    // 5. 绘制底部输入框和发送按钮。
    const wsc::RectF input(20, 346, 320, 48);
    canvas->drawBoxShadow(input, 24, 0, 6, 0, 2, wsc::Color(33, 48, 78, 24));
    wsc::Paint inputPaint;
    inputPaint.setColor(wsc::Color(255, 255, 255, 255));
    inputPaint.setAntiAlias(true);
    canvas->drawRoundRect(input, 24, inputPaint);
    wsc::Paint placeholder;
    placeholder.setColor(wsc::Color(145, 155, 177, 255));
    placeholder.setTextSize(15.0f);
    placeholder.setFontWeight(400);
    placeholder.setTextBaseline(wsc::Paint::TextBaseline::MIDDLE);
    canvas->drawText(u8"输入消息…", 34, 370, placeholder);
    inputPaint.setColor(wsc::Color(82, 116, 242, 255));
    canvas->drawCircle(320, 370, 16, inputPaint);

    // 6. 提交并保存多语言聊天效果图。
    canvas->endFrame();
    return canvas->savePixelsPPM("chapter07_chat.ppm") ? 0 : 2;
}
```
<!-- END GENERATED EXAMPLE: examples/tutorials/chapter07_chat.cpp -->

---

## 7.13 小结

本章学习了：

- [x] 字体注册（文件/内存/系统发现）
- [x] Fallback 链配置
- [x] 平台原生文本后端（DirectWrite / CoreText）
- [x] 单行文本绘制与对齐
- [x] 字体样式（weight / slant / spacing / features）
- [x] 文本测量（宽度/指标/包围盒）
- [x] 多行文本与省略号
- [x] 文本特效（描边/渐变/阴影）
- [x] Text on Path
- [x] CJK 与 RTL 多语言排版

**下一章**：[图层滤镜与特效](./08-layer-filters.md) —— 学习模糊、内阴影、毛玻璃等高级视觉效果。

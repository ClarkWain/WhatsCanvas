# 第六章：图片绘制

> 本章目标：掌握图片资源的加载方式、各种绘制方法（缩放、裁剪、圆角、九宫格、平铺），以及外部纹理互操作。

---

## 6.1 Image 对象

`wsc::Image` 是一个 GPU 纹理资源的封装。它的生命周期与 Canvas 绑定——加载时需要传入 Canvas 引用。

```cpp
wsc::Image image;  // 空的 Image 对象

// 加载后，image 持有 GPU 纹理句柄
// 使用完毕后 Image 析构时会自动释放资源
```

**注意**：Image 不可拷贝，只能移动：

```cpp
wsc::Image img1;
// img1 = img2;          // 编译错误！
wsc::Image img2 = std::move(img1);  // OK
```

---

## 6.2 加载图片

### 从文件路径加载

```cpp
wsc::Image image;
bool ok = canvas->loadImage(image, "assets/photo.png");
if (!ok) {
    // 加载失败处理
}
```

支持 PNG 和 JPEG 格式。

### 从内存加载（编码数据）

当图片数据已经在内存中时（比如从网络下载、资源打包）：

```cpp
// data 是 PNG/JPEG 的原始字节
std::vector<unsigned char> pngData = loadFromNetwork("...");

wsc::Image image;
bool ok = canvas->loadImageFromEncodedMemory(
    image, pngData.data(), pngData.size(), true /*generateMipmaps*/);
```

### 从原始 RGBA 像素加载

```cpp
int w = 64, h = 64;
std::vector<unsigned char> pixels(w * h * 4, 255);  // 全白

// 画一个红色方块
for (int y = 10; y < 50; ++y)
    for (int x = 10; x < 50; ++x) {
        int idx = (y * w + x) * 4;
        pixels[idx + 0] = 255;  // R
        pixels[idx + 1] = 0;    // G
        pixels[idx + 2] = 0;    // B
        pixels[idx + 3] = 255;  // A
    }

wsc::Image image;
canvas->loadImageFromRGBA(image, pixels.data(), w, h);
```

---

## 6.3 基础绘制

### 按原始尺寸绘制

```cpp
// 在 (x, y) 位置绘制图片的原始大小
canvas->drawImage(image, 50.0f, 50.0f, paint);
```

### 缩放到指定区域

```cpp
// 将图片拉伸到目标矩形
canvas->drawImage(image, wsc::RectF(50, 50, 300, 200), paint);
```

### 源区域 → 目标区域（部分绘制）

```cpp
// 只绘制图片的左上角 1/4，放大到目标区域
wsc::RectF src(0, 0, image.getWidth() / 2.0f, image.getHeight() / 2.0f);
wsc::RectF dst(50, 50, 300, 200);
canvas->drawImage(image, src, dst, paint);
```

---

## 6.4 适应模式 (ImageFit)

类似 CSS 的 `object-fit`，控制图片如何适应目标区域：

```cpp
wsc::RectF dst(50, 50, 300, 200);

// CONTAIN：完整显示图片，可能有留白
canvas->drawImageFit(image, dst, wsc::Canvas::ImageFit::CONTAIN, paint);

// COVER：填满区域，可能裁掉边缘
canvas->drawImageFit(image, dst, wsc::Canvas::ImageFit::COVER, paint);

// FILL：拉伸变形填满（默认）
canvas->drawImageFit(image, dst, wsc::Canvas::ImageFit::FILL, paint);
```

### 锚点控制

COVER 模式下，指定保留哪个区域：

```cpp
// 保留图片顶部（适合人像头部不被裁掉）
canvas->drawImageFit(image, dst, wsc::Canvas::ImageFit::COVER,
    wsc::Canvas::ImageAnchor::TOP, paint);

// 自定义锚点 (0~1, 0~1)
canvas->drawImageFit(image, dst, wsc::Canvas::ImageFit::COVER,
    0.5f, 0.3f, paint);  // 关注点偏上
```

---

## 6.5 圆角图片

### 统一圆角

```cpp
canvas->drawImageRounded(image, wsc::RectF(50, 50, 200, 200), 20.0f, paint);
```

### 四角独立圆角

```cpp
canvas->drawImageRounded(image, wsc::RectF(50, 50, 200, 200),
    30.0f,  // 左上
    10.0f,  // 右上
    30.0f,  // 右下
    10.0f,  // 左下
    paint);
```

### 圆形图片（头像）

```cpp
// 以圆形裁剪绘制图片
canvas->drawImageCircle(image, wsc::PointF(150, 150), 60.0f, paint);
```

---

## 6.6 九宫格 (Nine-Patch)

九宫格适用于可拉伸的 UI 元素（按钮背景、对话框气泡等）。四个角保持原始大小，边缘拉伸，中心区域铺满。

```cpp
// centerSrc: 图片中可拉伸的中心区域（源坐标）
wsc::RectF centerSrc(20, 20, 60, 60);  // 假设图片 100x100，留 20px 边距

// dst: 目标绘制区域
wsc::RectF dst(50, 50, 300, 100);

canvas->drawImageNinePatch(image, centerSrc, dst, paint);
```

---

## 6.7 平铺 (Tiled)

将图片重复铺满目标区域（类似 CSS `background-repeat`）：

```cpp
// 按图片原始尺寸平铺
canvas->drawImageTiled(image, wsc::RectF(0, 0, 400, 400), paint);

// 指定每块的大小
canvas->drawImageTiled(image, wsc::RectF(0, 0, 400, 400),
    64.0f, 64.0f, paint);  // 每块 64x64
```

---

## 6.8 图片更新

### 整体替换像素

```cpp
// 替换图片的全部内容（尺寸可以改变）
std::vector<unsigned char> newPixels = generateFrame();
canvas->replaceImageRGBA(image, newPixels.data(), newW, newH);
```

### 局部更新

仅更新图片的一个矩形区域（适合视频帧、动态纹理）：

```cpp
// 只更新 (x=10, y=10) 处大小为 32x32 的区域
canvas->updateImageRGBA(image, subPixels.data(), 10, 10, 32, 32);
```

---

## 6.9 外部纹理互操作

### OpenGL 纹理

如果已有一个 OpenGL 纹理，可以直接让 WhatsCanvas 绘制它：

```cpp
GLuint texId = ...; // 外部创建的 OpenGL 纹理
int texW = 512, texH = 512;

wsc::Image image;
canvas->wrapExternalTexture(image, texId, texW, texH);
canvas->drawImage(image, wsc::RectF(0, 0, 400, 400), paint);
```

### Metal 纹理

```cpp
id<MTLTexture> mtlTex = ...;
wsc::Image image;
canvas->wrapExternalMetalTexture(image, (__bridge void*)mtlTex, w, h);
```

---

## 6.10 图片采样配置

通过 Paint 控制图片的采样和过滤方式：

```cpp
wsc::Paint imgPaint;

// 缩放时的采样质量
imgPaint.setImageSampling(wsc::Paint::ImageSampling::LINEAR);       // 默认
imgPaint.setImageSampling(wsc::Paint::ImageSampling::NEAREST);      // 像素风格
imgPaint.setImageSampling(wsc::Paint::ImageSampling::MIPMAP_LINEAR);// 缩小最佳

// 超出边界时的处理
imgPaint.setImageTileMode(wsc::Paint::ImageTileMode::CLAMP);    // 拉伸边缘像素
imgPaint.setImageTileMode(wsc::Paint::ImageTileMode::REPEAT);   // 重复
imgPaint.setImageTileMode(wsc::Paint::ImageTileMode::MIRROR);   // 镜像
imgPaint.setImageTileMode(wsc::Paint::ImageTileMode::DECAL);    // 透明
```

---

## 6.11 综合示例：图片画廊

![同一张图片在 CONTAIN、COVER、圆形裁剪和平铺模式下的结果](../images/chapter06_gallery.png)

示例使用 `loadImageFromRGBA` 在内存中生成测试图片，不依赖仓库外部素材。下方代码与生成图片的[可编译源码](https://github.com/ClarkWain/WhatsCanvas/blob/main/examples/tutorials/chapter06_gallery.cpp)相同。

<!-- BEGIN GENERATED EXAMPLE: examples/tutorials/chapter06_gallery.cpp -->
```cpp
// Chapter 06 comprehensive example: image fit, circular crop and tiling.
#include <wsc/FontSystem.h>
#include <wsc/wsc.h>

#include <cmath>
#include <vector>

namespace {

std::vector<unsigned char> makeLandscape(int width, int height)
{
    // 直接生成 RGBA 测试图，避免教程依赖仓库外部素材。
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 4u);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const float fy = static_cast<float>(y) / height;
            unsigned char r = static_cast<unsigned char>(90 + 92 * fy);
            unsigned char g = static_cast<unsigned char>(142 + 62 * fy);
            unsigned char b = static_cast<unsigned char>(224 - 28 * fy);
            // 天空中的太阳。
            const float sunDx = x - width * 0.72f;
            const float sunDy = y - height * 0.30f;
            if (sunDx * sunDx + sunDy * sunDy < 28.0f * 28.0f) {
                r = 255; g = 221; b = 132;
            }
            // 两层山脊和底部水面。
            const float ridgeA = height * 0.58f + std::abs(x - width * 0.34f) * 0.38f;
            const float ridgeB = height * 0.66f + std::abs(x - width * 0.72f) * 0.24f;
            if (y > ridgeA) { r = 54; g = 83; b = 108; }
            if (y > ridgeB) { r = 35; g = 63; b = 83; }
            if (y > height * 0.80f) {
                const int shimmer = ((x / 12 + y / 6) % 2) * 12;
                r = static_cast<unsigned char>(35 + shimmer);
                g = static_cast<unsigned char>(92 + shimmer);
                b = static_cast<unsigned char>(118 + shimmer);
            }
            const std::size_t index = (static_cast<std::size_t>(y) * width + x) * 4u;
            pixels[index] = r; pixels[index + 1] = g; pixels[index + 2] = b; pixels[index + 3] = 255;
        }
    }
    return pixels;
}

} // namespace

int main()
{
    // 1. 创建画布并注册用于标题、标签的系统字体。
    auto canvas = wsc::Canvas::create(wsc::Canvas::Backend::Software, 960, 600);
    if (!canvas || !canvas->initializeContext()) return 1;
    for (const auto &face : wsc::FontSystem::defaultSystemFontFaces()) {
        canvas->registerFontFace(face);
    }
    canvas->setFontFallbackChain(wsc::FontSystem::defaultFallbackChain());

    // 2. 将内存中的 RGBA 像素上传为 Image。
    wsc::Image photo;
    const auto pixels = makeLandscape(320, 180);
    if (!canvas->loadImageFromRGBA(photo, pixels, 320, 180, true)) return 2;

    // 3. 绘制页面背景和标题。
    canvas->beginFrame();
    wsc::Paint background;
    background.setColor(wsc::Color(244, 247, 252, 255));
    canvas->drawRect(wsc::RectF(0, 0, 960, 600), background);

    wsc::Paint title;
    title.setColor(wsc::Color(30, 39, 58, 255));
    title.setTextSize(32.0f);
    title.setFontWeight(650);
    title.setTextBaseline(wsc::Paint::TextBaseline::MIDDLE);
    canvas->drawText("One image, four drawing modes", 50, 58, title);

    wsc::Paint imagePaint;
    imagePaint.setColor(wsc::Color(255, 255, 255, 255));
    imagePaint.setAntiAlias(true);
    imagePaint.setImageSampling(wsc::Paint::ImageSampling::MIPMAP_LINEAR);

    // 4. 三个白色面板共用同一个布局函数。
    auto panel = [&](float x, const char *label) {
        const wsc::RectF bounds(x, 112, 260, 300);
        canvas->drawBoxShadow(bounds, 22, 0, 18, 0, 8, wsc::Color(35, 49, 83, 26));
        wsc::Paint surface;
        surface.setColor(wsc::Color(255, 255, 255, 255));
        surface.setAntiAlias(true);
        canvas->drawRoundRect(bounds, 22, surface);
        wsc::Paint caption = title;
        caption.setTextSize(17.0f);
        caption.setFontWeight(600);
        caption.setTextAlign(wsc::Paint::TextAlign::CENTER);
        canvas->drawText(label, x + 130, 376, caption);
    };

    panel(50, "CONTAIN");
    panel(350, "COVER");
    panel(650, "CIRCLE");

    // 5. 同一张图片分别使用 CONTAIN、COVER 和圆形裁剪。
    canvas->drawImageFit(photo, wsc::RectF(72, 142, 216, 190), wsc::Canvas::ImageFit::CONTAIN, imagePaint);
    canvas->drawImageFit(photo, wsc::RectF(372, 142, 216, 190), wsc::Canvas::ImageFit::COVER, imagePaint);
    canvas->drawImageCircle(photo, wsc::PointF(780, 236), 94, imagePaint);

    // 6. 底部区域演示指定 tile 尺寸的平铺。
    const wsc::RectF tiledBounds(50, 466, 860, 84);
    canvas->drawBoxShadow(tiledBounds, 18, 0, 14, 0, 6, wsc::Color(35, 49, 83, 24));
    canvas->drawImageTiled(photo, tiledBounds, 150, 84, imagePaint);

    // 7. 输出与教程图片一致的结果。
    canvas->endFrame();
    return canvas->savePixelsPPM("chapter06_gallery.ppm") ? 0 : 3;
}
```
<!-- END GENERATED EXAMPLE: examples/tutorials/chapter06_gallery.cpp -->

---

## 6.12 API 速查表

| 方法 | 说明 |
|------|------|
| `loadImage(img, path)` | 从文件加载 PNG/JPEG |
| `loadImageFromEncodedMemory(...)` | 从内存编码数据加载 |
| `loadImageFromRGBA(...)` | 从原始 RGBA 像素加载 |
| `drawImage(img, x, y, paint)` | 原始尺寸绘制 |
| `drawImage(img, dst, paint)` | 拉伸到目标区域 |
| `drawImage(img, src, dst, paint)` | 源区域映射到目标 |
| `drawImageFit(img, dst, fit, paint)` | CONTAIN/COVER/FILL |
| `drawImageRounded(img, dst, r, paint)` | 圆角图片 |
| `drawImageCircle(img, center, r, paint)` | 圆形图片 |
| `drawImageNinePatch(img, src, dst, paint)` | 九宫格拉伸 |
| `drawImageTiled(img, dst, paint)` | 平铺 |
| `replaceImageRGBA(...)` | 整体替换像素 |
| `updateImageRGBA(...)` | 局部更新像素 |
| `wrapExternalTexture(...)` | 包装 OpenGL 纹理 |

---

## 6.13 小结

本章学习了：

- [x] Image 对象的创建和生命周期
- [x] 三种加载方式（文件/编码内存/原始像素）
- [x] 基础绘制和区域映射
- [x] ImageFit 适应模式与锚点
- [x] 圆角和圆形图片
- [x] 九宫格拉伸
- [x] 图片平铺
- [x] 动态更新像素
- [x] 外部纹理互操作
- [x] 采样和平铺配置

**下一章**：[文本排版](./07-text-bindlayout.md) —— 学习字体注册、文本绘制、多行排版和 text-on-path。

# WhatsCanvas — A C++17 2D Rendering Library Between NanoVG and Skia

> This article introduces WhatsCanvas, an embeddable 2D rendering library for native applications. If you are looking for a C++ drawing solution whose API style is close to HTML Canvas, that supports multi-backend rendering, and that ships complete text layout and layer filter capabilities, this article is worth reading.

For the Chinese version, see [`zh/00-whatscanvas-intro.md`](./zh/00-whatscanvas-intro.md).

---

## 1. Background: Why WhatsCanvas?

In the native C++ 2D rendering space, developers usually face this trade-off:

| Option | Pros | Cons |
|--------|------|------|
| **NanoVG** | Lightweight, easy to integrate | No multilingual text, no layer filters, no pixel regression |
| **Skia** | Full-featured, industrial grade | Large, complex to build, wide API surface |
| **Cairo** | Mature and stable | Limited text capabilities, no GPU backend |

WhatsCanvas fits into exactly the gap between NanoVG and Skia:

- Ships a **Canvas / Paint / Path** trio of APIs (similar to HTML Canvas, familiar to front-end developers)
- Supports **multilingual text** (CJK, RTL, bidirectional text, HarfBuzz shaping)
- Provides built-in **layer filters** (blur, inner shadow, frosted glass)
- Supports **5 render backends** (Software, OpenGL, OpenGL ES, Vulkan, Metal)
- Covers **6 platforms** (Windows, Linux, macOS, Android, iOS, Web)
- Ships a complete **pixel regression testing** infrastructure

---

## 2. Quick Start: Draw Your First Frame in 60 Seconds

WhatsCanvas ships a Software backend that does not require a GPU, window, or graphics context, which makes it ideal for quick validation:

```cpp
#include <wsc/wsc.h>

int main()
{
    // Create a 256x256 Software Canvas
    auto canvas = wsc::Canvas::create(wsc::Canvas::Backend::Software, 256, 256);
    if (!canvas || !canvas->initializeContext()) {
        return 1;
    }

    canvas->beginFrame();

    // Configure the paint: blue fill + anti-aliasing
    wsc::Paint fill;
    fill.setColor(wsc::Color(40, 120, 240, 255));
    fill.setAntiAlias(true);

    // Draw a rounded rectangle
    canvas->drawRoundRect(wsc::RectF(40, 40, 176, 176), 24.0f, fill);

    canvas->endFrame();

    // Write out to a PPM file
    return canvas->savePixelsPPM("first.ppm") ? 0 : 1;
}
```

Twenty-four lines are enough for a complete offscreen render. The CMake setup is equally minimal:

```cmake
cmake_minimum_required(VERSION 3.16)
project(MyApp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
find_package(WhatsCanvas 1.1.0 CONFIG REQUIRED)

add_executable(MyApp main.cpp)
target_link_libraries(MyApp PRIVATE WhatsCanvas::Software)
```

### 2.1 Nail Down Physical Size, Logical Size, and DPR First

The offscreen example above does not set a DPR, so one drawing unit equals one output pixel. That is fine for producing small images, but you should not copy the pattern directly into high-density windows or mobile UIs.

WhatsCanvas treats three sizing concepts separately:

| Concept | Meaning | Example |
|---------|---------|---------|
| Physical size | Pixel width and height of the Canvas buffer or framebuffer | `720 × 820 px` |
| DPR | How many physical pixels one logical unit maps to | `2.0` |
| Logical size | Width and height your layout code uses | `360 × 410` |

```cpp
constexpr int physicalWidth = 720;
constexpr int physicalHeight = 820;
constexpr float dpr = 2.0f;

auto canvas = wsc::Canvas::create(
    wsc::Canvas::Backend::Software, physicalWidth, physicalHeight);
canvas->setDevicePixelRatio(dpr);

const float logicalWidth = physicalWidth / dpr;
const float logicalHeight = physicalHeight / dpr;
```

Once you set a DPR, coordinates, corner radii, stroke widths, and text sizes all use logical units, and the Canvas maps them to physical pixels at output time. `getWidth()` and `getHeight()` still return the physical dimensions; do not use them directly as your logical layout width and height.

Android hosts typically pass `DisplayMetrics.density` to `setDevicePixelRatio`, so one logical unit can be treated as 1 dp. `Paint::setTextSize` itself does not recognize sp; the Android host must convert with `TypedValue.applyDimension(COMPLEX_UNIT_SP, ...)` and then divide by density to get the Canvas logical font size. The full contract and examples live in the [sizing note on the tutorials home page](./README.md).

---

## 3. Core Architecture and API Design

### 3.1 Three Core Objects

The WhatsCanvas API is organized around three core concepts:

```
Canvas  ──  Drawing surface, manages frame lifecycle and state stack
Paint   ──  Drawing attributes (color, gradient, text, blend mode, ...)
Path    ──  2D geometric path (Bezier curves, hit-testing, ...)
```

The design is immediately familiar if you have used HTML Canvas or Android Canvas.

### 3.2 Canvas: Drawing API Overview

```cpp
// Basic shapes
canvas->drawRect(rect, paint);
canvas->drawRoundRect(rect, radius, paint);
canvas->drawCircle(cx, cy, radius, paint);
canvas->drawOval(bounds, paint);
canvas->drawPath(path, paint);
canvas->drawLine(x1, y1, x2, y2, paint);
canvas->drawBoxShadow(rect, radius, spread, blur, dx, dy, color);

// Image drawing
canvas->drawImage(image, x, y, paint);
canvas->drawImageFit(image, dst, ImageFit::COVER, paint);
canvas->drawImageRounded(image, dst, radius, paint);
canvas->drawImageNinePatch(image, centerSrc, dst, paint);
canvas->drawImageTiled(image, dst, paint);

// Text drawing
canvas->drawText("Hello", x, y, paint);
canvas->drawTextBox(text, bounds, lineHeight, maxLines, ellipsize, paint);
canvas->drawTextOnPath(text, path, hOffset, vOffset, paint);
float width = canvas->measureText(text, paint);

// State management
canvas->save();
canvas->translate(dx, dy);
canvas->scale(sx, sy);
canvas->rotate(radians);
canvas->clipPath(path);
canvas->restore();
```

### 3.3 Paint: Rich Drawing Attributes

```cpp
wsc::Paint paint;

// Color and style
paint.setColor(wsc::Color(40, 120, 240, 255));
paint.setStyle(wsc::Paint::Style::FILL);  // FILL / STROKE / FILL_AND_STROKE
paint.setStrokeWidth(2.0f);
paint.setAntiAlias(true);

// Gradients
paint.setLinearGradient(x1, y1, x2, y2, {
    {0.0f, Color::RED}, {0.5f, Color::GREEN}, {1.0f, Color::BLUE}
});
paint.setRadialGradient(cx, cy, radius, startColor, endColor);

// Shadow
paint.setShadowLayer(radius, dx, dy, shadowColor);

// Blend modes (14 total)
paint.setBlendMode(wsc::Paint::BlendMode::MULTIPLY);

// Text
paint.setTextSize(24.0f);
paint.setFontFamily("Roboto");
paint.setFontWeight(700);
paint.setTextAlign(wsc::Paint::TextAlign::CENTER);

// Path effects
paint.setDashPathEffect({10.0f, 5.0f}, 0.0f);
paint.setCornerPathEffect(8.0f);
```

### 3.4 Path: Construction and Queries

```cpp
wsc::Path path;
path.moveTo(10, 10);
path.lineTo(100, 10);
path.quadTo(controlX, controlY, endX, endY);
path.cubicTo(c1x, c1y, c2x, c2y, ex, ey);
path.close();

// Shape helpers
path.addRoundRect(rect, radius);
path.addCircle(cx, cy, radius);

// Queries and hit-testing
bool hit = path.contains(x, y);
bool strokeHit = path.strokeContains(x, y, strokeWidth);
RectF bounds = path.getBounds();
```

---

## 4. Layer Filters: Frosted Glass Out of the Box

WhatsCanvas ships a complete layer filter system that supports content blur, backdrop blur, inner shadow, frosted glass, and other common UI effects:

```cpp
// Build a frosted glass filter
auto frosted = wsc::ImageFilter::frostedGlass(
    8.0f,   // Blur radius
    1.18f,  // Saturation
    1.04f,  // Brightness
    1.02f,  // Contrast
    0.012f  // Grain
);

// Create a layer with backdrop blur
canvas->saveLayer(bounds, paint, wsc::LayerOptions()
    .setBackdropFilter(frosted));

// Draw the foreground content on top of the blurred background
canvas->drawRoundRect(rect, radius, foregroundPaint);

canvas->restore();
```

Supported filter types include:
- `ImageFilter::blur()` — Gaussian blur
- `ImageFilter::innerShadow()` — Inner shadow
- `ImageFilter::frostedGlass()` — Frosted glass (blur + saturation + brightness + contrast + grain)
- `ImageFilterChain` — Chained composition of multiple filters
- Color matrix transforms

---

## 5. Multi-Backend Architecture

WhatsCanvas supports multiple render backends through a unified `Canvas::create()` API:

```cpp
using Backend = wsc::Canvas::Backend;

// Try backends in priority order and fall back automatically
auto canvas = wsc::Canvas::create(
    {Backend::Vulkan, Backend::Metal, Backend::OpenGL, Backend::Software},
    width, height);
```

| Backend | CMake Target | Use Case |
|---------|--------------|----------|
| Software | `WhatsCanvas::Software` | Tests, headless, screenshots, CI environments |
| OpenGL 3.3 | `WhatsCanvas::OpenGL` | Primary path for desktop apps |
| OpenGL ES 3.0 | `WhatsCanvas::OpenGLES` | Mobile, WebGL |
| Vulkan | Built into the OpenGL target | High-performance, low-overhead rendering |
| Metal | `WhatsCanvas::Metal` | macOS / iOS native |

Backends can be trimmed at build time; the ones you do not need are excluded from compilation and linking entirely:

```cmake
# CPU rendering only, zero GPU dependency
set(WHATSCANVAS_BUILD_OPENGL OFF)
set(WHATSCANVAS_BUILD_SOFTWARE ON)
```

---

## 6. Text Layout: More Than Drawing Characters

The WhatsCanvas text stack goes well beyond what a typical 2D drawing library ships:

- **Font discovery and fallback chain**: automatic system font discovery with multi-level fallback
- **CJK layout**: no-space line breaking, correct punctuation handling
- **RTL and bidirectional text**: full UAX #9 implementation (861,948 test cases all pass)
- **HarfBuzz shaping**: correct shaping of Arabic, Devanagari, and other complex scripts
- **Font features**: weight / slant / OpenType features / variable axes
- **COLR/CPAL color emoji**: supports v0 and common COLRv1 paint graphs
- **Native platform backends**: optional DirectWrite on Windows, optional CoreText on Apple

```cpp
// Text drawing example
wsc::Paint textPaint;
textPaint.setTextSize(18.0f);
textPaint.setFontFamily("Noto Sans SC");
textPaint.setColor(wsc::Color(33, 33, 33, 255));

// Multi-line text + ellipsis
canvas->drawTextBox(
    u8"WhatsCanvas supports CJK text layout, including automatic line breaking and ellipsis handling.",
    wsc::RectF(20, 20, 300, 200),
    1.5f,    // Line-height multiplier
    3,       // Max lines
    true,    // Enable ellipsis
    textPaint
);

// Text along a path
wsc::Path arc;
arc.addArc(bounds, 0.0f, 3.14f);
canvas->drawTextOnPath(u8"Text on Path", arc, 0, 0, textPaint);
```

---

## 7. Cross-Platform Support

| Platform | Automation Status | Render Backends |
|----------|-------------------|-----------------|
| Windows x64 | CI unit tests + pixel regression + package-consumer validation | OpenGL, GLES, Software, Vulkan |
| Linux x64 | CI build + unit tests + pixel gates | OpenGL, GLES, Software |
| macOS | CI unit tests + Metal pixel gates + universal release package | Metal, OpenGL, Software |
| Android | Three ABIs (armeabi-v7a, arm64-v8a, x86_64) | OpenGL ES |
| iOS | Metal/CoreText on device and simulator | Metal |
| Web | Emscripten/WebGL 2 + browser automation | OpenGL ES (compiled to WebGL 2) |

---

## 8. Performance

The repository archives a detailed benchmark comparison against NanoVG (Windows / Core i7-8700 / GTX 1060 / 1920×1080 / OpenGL):

| Scenario | Result |
|----------|--------|
| Anti-aliased geometry (256–4096 shapes) | 8 wins, 1 tie; max frame time down 26.7% |
| Image drawing (64–1024 images) | 9/9 wins; max frame time down 58.5% |
| Dynamic text (64–1024 draws) | 9/9 wins; max frame time down 32.0% |

**26 wins, 0 losses, 1 tie** total, with all 27 pixel-quality checks passing.

> Method: each process warms up for 5 frames and measures 30; each cell uses 2 ABBA blocks, 4 fresh processes per side, and 10,000 bootstrap resamples for statistical confidence.

---

## 9. Integration Options

WhatsCanvas offers several integration paths to fit different engineering workflows:

### Option 1: Precompiled GitHub Release Package

```bash
# Download whatscanvas-win64-release-1.1.0.zip
# Extract and point CMAKE_PREFIX_PATH at the install directory
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/whatscanvas
```

### Option 2: CMake Subdirectory

```cmake
add_subdirectory(third_party/WhatsCanvas)
target_link_libraries(MyApp PRIVATE WhatsCanvas::OpenGL)
```

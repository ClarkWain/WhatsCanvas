# WhatsCanvas Tutorials

> This tutorial series introduces the WhatsCanvas 2D rendering library step by step. Each chapter is self-contained; reading them in order is recommended.

For the Chinese version, see [`zh/README.md`](./zh/README.md).

## Table of Contents

| Ch. | Title | Level | File |
|:---:|-------|:-----:|------|
| 00 | [Project Overview and Boundaries](./00-whatscanvas-intro.md) | Overview | `00-whatscanvas-intro.md` |
| 01 | [Environment Setup and First Frame](./01-environment-setup.md) | Beginner | `01-environment-setup.md` |
| 02 | [Basic Shape Drawing](./02-basic-shapes.md) | Beginner | `02-basic-shapes.md` |
| 03 | [Paint in Depth](./03-paint-bindepth.md) | Elementary | `03-paint-bindepth.md` |
| 04 | [Path and Curves](./04-path-bindcurves.md) | Elementary | `04-path-bindcurves.md` |
| 05 | [State Stack and Transforms](./05-state-bindtransforms.md) | Elementary | `05-state-bindtransforms.md` |
| 06 | [Image Drawing](./06-image-bindrawing.md) | Intermediate | `06-image-bindrawing.md` |
| 07 | [Text Layout](./07-text-bindlayout.md) | Intermediate | `07-text-bindlayout.md` |
| 08 | [Layer Filters and Effects](./08-layer-filters.md) | Intermediate | `08-layer-filters.md` |
| 09 | [Windowed Presentation and Interaction](./09-windowed-presentation.md) | Advanced | `09-windowed-presentation.md` |
| 10 | [Multiple Backends and Fallback](./10-multi-backend.md) | Advanced | `10-multi-backend.md` |
| 11 | [Performance Optimization](./11-performance.md) | Advanced | `11-performance.md` |
| 12 | [Cross-Platform in Practice](./12-cross-platform.md) | Advanced | `12-cross-platform.md` |

## Prerequisites

- A C++17 compiler (MSVC 2019+, GCC 9+, Clang 10+)
- CMake 3.16+
- WhatsCanvas 1.1.0+ ([Get and Build](https://github.com/ClarkWain/WhatsCanvas/blob/main/README.md))

## Conventions

- Code in the tutorials uses the **Software backend** by default unless a chapter states otherwise.
- `Canvas::create` / `setSize` take physical pixel dimensions. After you set a DPR, the drawing APIs operate in logical coordinates.
- `Paint::setTextSize` takes a logical unit; it does not recognize Android `sp` automatically.
- Short snippets inside a chapter explain individual APIs. End-of-chapter integrated examples with rendered output live under `examples/tutorials/` as compilable source files.
- Rendered images are produced by the Software backend example programs and saved as PNG, so text and shape edges can be inspected.
- Integrated example code blocks are generated from the corresponding `.cpp` files; after editing sources, run `pwsh examples/tutorials/sync_docs.ps1`.
- `wsc::` is the WhatsCanvas namespace prefix.

## Dimensions, DPR, and Text Units (must read)

WhatsCanvas does not decide layout units on the host's behalf. Canvas width and height are physical pixels; `setDevicePixelRatio` establishes the mapping from logical coordinates to physical pixels:

```text
logical width  = physical width  / DPR
logical height = physical height / DPR
physical output size = logical size × DPR
```

For example, a 720 × 820 offscreen buffer at DPR 2 provides a 360 × 410 logical drawing area:

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

Coordinates, corner radii, stroke widths, and `setTextSize` are all in logical units. Do **not** additionally call `canvas->scale(dpr, dpr)`, or content will be scaled twice. `getWidth()` and `getHeight()` still return physical sizes; when computing layout, use logical dimensions you have derived and stored yourself.

On Android you typically pass `DisplayMetrics.density` as the DPR. That way a Canvas logical unit acts as 1 dp. Text must also respect the user's font setting: the host should first convert sp to physical pixels, then divide by density, and only then pass the value to `setTextSize`:

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

On Android 14 and later, [non-linear font scaling](https://developer.android.com/about/versions/14/features#non-linear-font-scaling) may apply. Do not compute text size manually with `16 * fontScale` or `scaledDensity`. Use [`TypedValue.applyDimension`](https://developer.android.com/reference/android/util/TypedValue#applyDimension(int,%20float,%20android.util.DisplayMetrics)) and recompute text sizes when font settings change. Chapter 5 further covers DPR and the state matrix; Chapter 7 covers font size and line height; Chapters 9 and 12 cover how to handle sizing in desktop and Web hosts respectively.

## Running the Integrated Examples

```bash
cmake -S . -B build -DWHATSCANVAS_BUILD_OPENGL=OFF -DWHATSCANVAS_BUILD_SOFTWARE=ON
cmake --build build --config Release --target chapter02_cards
./build/Release/chapter02_cards  # Windows
```

Other integrated examples run the same way; replace the last target with the corresponding source file name.

Before committing, you can check whether the tutorial code blocks are still in sync with the sources:

```powershell
pwsh examples/tutorials/sync_docs.ps1 -Check
```

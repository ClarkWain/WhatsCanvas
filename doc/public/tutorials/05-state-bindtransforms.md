# Chapter 5: State Stack and Transforms

> Goal of this chapter: understand the Canvas state management model (save/restore), master translate/scale/rotate transforms, and use rectangle and path clipping.

For the Chinese version, see [`zh/05-state-bindtransforms.md`](./zh/05-state-bindtransforms.md).

---

## 5.1 The Canvas State Stack

Canvas maintains an internal state stack. Each call to `save()` pushes the current state (transform matrix and clip region) onto the stack; `restore()` pops it and restores the previous state.

```
save()    → save the current state
  ↓  change transform / clip
  ↓  draw content
restore() → restore to before save()
```

This is the core mechanism for keeping drawing operations from interfering with each other.

```cpp
canvas->save();

// Transform and draw inside the saved state
canvas->translate(100, 100);
canvas->drawRect(wsc::RectF(0, 0, 50, 50), paint);

canvas->restore();  // The transform is undone

// Coordinate system is back to original
canvas->drawRect(wsc::RectF(0, 0, 50, 50), paint);  // Top-left corner
```

### Nested save

```cpp
canvas->save();            // saveCount = 1
    canvas->translate(50, 0);
    canvas->save();        // saveCount = 2
        canvas->translate(0, 50);
        // Now the offset is (50, 50)
    canvas->restore();     // Back to offset (50, 0)
canvas->restore();         // Back to no offset
```

### restoreToCount

Restore to a specific save level in one call:

```cpp
int count = canvas->save();     // Record the save point
canvas->save();
canvas->save();
// ... multiple operations ...
canvas->restoreToCount(count);  // Return to the initial level
```

---

## 5.2 translate

Move the coordinate origin to a new position:

```cpp
canvas->save();
canvas->translate(150, 100);

// Draw with (150, 100) as the origin
wsc::Paint paint;
paint.setColor(wsc::Color(66, 133, 244, 255));
canvas->drawRect(wsc::RectF(0, 0, 100, 80), paint);  // Actual position (150, 100)

canvas->restore();
```

---

## 5.3 scale

Scale around the current origin:

```cpp
canvas->save();
canvas->translate(200, 200);  // Move to the canvas center first
canvas->scale(2.0f, 2.0f);   // 2x zoom

// Draw a "logical" 50x50 rectangle; it renders as 100x100
wsc::Paint paint;
paint.setColor(wsc::Color(76, 175, 80, 255));
canvas->drawRect(wsc::RectF(-25, -25, 50, 50), paint);

canvas->restore();
```

### Mirroring

```cpp
// Horizontal flip
canvas->scale(-1.0f, 1.0f);

// Vertical flip
canvas->scale(1.0f, -1.0f);
```

---

## 5.4 rotate

Rotate around the current origin. Angle is in **radians**:

```cpp
float pi = 3.14159265f;

canvas->save();
canvas->translate(200, 200);       // Move to the rotation center
canvas->rotate(pi / 4.0f);        // Rotate 45°

wsc::Paint paint;
paint.setColor(wsc::Color(255, 152, 0, 255));
paint.setAntiAlias(true);
canvas->drawRect(wsc::RectF(-60, -60, 120, 120), paint);

canvas->restore();
```

> **Key technique**: Rotation is always around the current origin. To rotate around a specific point:
> 1. `translate(cx, cy)` — move to the rotation center
> 2. `rotate(angle)` — rotate
> 3. `translate(-cx, -cy)` — move back (or draw with the center as the origin directly)

---

## 5.5 Composing Transforms

Transforms **accumulate in call order**. Different orders yield different results:

```cpp
// Translate then rotate
canvas->save();
canvas->translate(200, 200);
canvas->rotate(pi / 6);
canvas->drawRect(wsc::RectF(-40, -40, 80, 80), paint);
canvas->restore();

// Rotate then translate (different result!)
canvas->save();
canvas->rotate(pi / 6);
canvas->translate(200, 200);
canvas->drawRect(wsc::RectF(-40, -40, 80, 80), paint);
canvas->restore();
```

### Matrix operations

For more complex transforms you can manipulate the matrix directly:

```cpp
// Get the current matrix
wsc::Matrix4 mat = canvas->getMatrix();

// Set a custom matrix
canvas->setMatrix(customMatrix);

// Reset to identity
canvas->resetMatrix();

// Concatenate (right-multiply) another matrix
canvas->concat(additionalMatrix);
```

---

## 5.6 Coordinate Mapping

Map a point from local coordinates to device coordinates (or vice versa):

```cpp
canvas->save();
canvas->translate(100, 50);
canvas->scale(2.0f, 2.0f);

// Forward: local → device
wsc::PointF devicePt = canvas->mapPoint(wsc::PointF(10, 20));
// devicePt = (100 + 10*2, 50 + 20*2) = (120, 90)

// Inverse: device → local (for click hit-testing)
wsc::PointF localPt;
canvas->inverseMapPoint(wsc::PointF(120, 90), localPt);
// localPt = (10, 20)

canvas->restore();
```

---

## 5.7 Clipping

A clip restricts the visible region for subsequent drawing. Content outside the clip is not rendered.

### Rectangle Clip

```cpp
canvas->save();

// Set the clip region
canvas->clipRect(wsc::RectF(50, 50, 200, 200));

// This circle is clipped to the part inside the rectangle
wsc::Paint paint;
paint.setColor(wsc::Color(244, 67, 54, 255));
paint.setAntiAlias(true);
canvas->drawCircle(150, 150, 120, paint);

canvas->restore();  // The clip is restored too
```

### Path Clip

Use any path as the clip region:

```cpp
canvas->save();

// Circular clip
wsc::Path clipCircle;
clipCircle.addCircle(200, 200, 100);
canvas->clipPath(clipCircle);

// Draw image or complex content inside the circle
canvas->drawRect(wsc::RectF(0, 0, 400, 400), gradientPaint);

canvas->restore();
```

### Clips Accumulate

Multiple clips are **intersected** (the region only gets smaller):

```cpp
canvas->save();
canvas->clipRect(wsc::RectF(50, 50, 200, 200));
canvas->clipRect(wsc::RectF(100, 100, 200, 200));
// Final clip = intersection of the two rectangles = (100, 100, 150, 150)
canvas->restore();
```

---

## 5.8 Quick Reject

In a heavy drawing loop, `quickReject` lets you quickly test whether a region is fully outside the clip and skip drawing it:

```cpp
canvas->clipRect(wsc::RectF(0, 0, 200, 200));

// Quick test: skip drawing when the rectangle is entirely outside the clip
if (!canvas->quickReject(wsc::RectF(300, 300, 100, 100))) {
    canvas->drawRect(wsc::RectF(300, 300, 100, 100), paint);
}
```

---

## 5.9 Device Pixel Ratio (DPR)

DPR is not an ordinary content scale. The Canvas is still created in physical pixels; DPR acts as a root transform that maps logical coordinates onto the framebuffer:

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

Drawing `RectF(0, 0, 100, 40)` after this occupies 100 × 40 logical units and outputs 200 × 80 physical pixels. Text is also rasterized directly at the target resolution using the current DPR; there is no need to draw a low-resolution bitmap first and then scale it up.

Note the following behaviors:

- `Canvas::create`, `setSize`, `getWidth`, and `getHeight` operate in physical pixels.
- Drawing coordinates, stroke widths, and `Paint::setTextSize` use logical units.
- `setDevicePixelRatio` resets the current matrix to the new DPR root transform; call it before any application-level `translate`, `scale`, or `rotate`.
- `resetMatrix()` clears only application transforms; the DPR is preserved.
- Do not additionally call `canvas->scale(dpr, dpr)`, or geometry and text will be scaled twice.

When the window size or display scale changes, update the physical size, the DPR, and the logical layout size together. Convert click or touch coordinates into the same logical coordinate space before hit-testing.

---

## 5.10 Integrated Example: A Rotating Flower

![A ten-petal flower composed with rotation transforms](./images/chapter05_flower.png)

The key point: each petal is drawn in a local coordinate frame; `save`, `translate`, `rotate`, and `restore` arrange the petals around a shared center. Two guide circles are kept in the image as visual hints for the rotation radii. The code below matches the [compilable source](https://github.com/ClarkWain/WhatsCanvas/blob/main/examples/tutorials/chapter05_flower.cpp) that produced the picture.

<!-- BEGIN GENERATED EXAMPLE: examples/tutorials/chapter05_flower.cpp -->
```cpp
// Chapter 05 comprehensive example: a flower composed with save/restore transforms.
#include <wsc/wsc.h>

#include <cmath>

int main()
{
    // 1. Create the offscreen canvas and define shared geometry for the flower.
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

    // 2. Draw the background and two guide circles that mark the rotation radii.
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

    // 3. Draw the stem and leaves below the petals first.
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

    // 4. Each petal is drawn in the same local coordinate frame; only the canvas rotation changes.
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

    // 5. Draw the center circle last so it hides the petal overlap area.
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

    // 6. Submit and save the result.
    canvas->endFrame();
    return canvas->savePixelsPPM("chapter05_flower.ppm") ? 0 : 2;
}
```
<!-- END GENERATED EXAMPLE: examples/tutorials/chapter05_flower.cpp -->

---

## 5.11 Summary

This chapter covered:

- [x] The save/restore state stack
- [x] translate, scale, rotate
- [x] Order dependence when composing transforms
- [x] Direct matrix manipulation
- [x] Coordinate mapping (forward and inverse)
- [x] Rectangle and path clipping
- [x] The Quick Reject optimization
- [x] Device Pixel Ratio

**Next chapter**: [Image Drawing](./06-image-bindrawing.md) — loading, drawing, and transforming images.

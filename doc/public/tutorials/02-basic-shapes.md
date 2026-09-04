# Chapter 2: Basic Shape Drawing

> Goal of this chapter: master every basic shape drawing API WhatsCanvas offers, including rectangles, rounded rectangles, circles, ovals, arcs, lines, polygons, and shadows.

For the Chinese version, see [`zh/02-basic-shapes.md`](./zh/02-basic-shapes.md).

---

## 2.1 Preparation

Every example in this chapter is based on the following template:

```cpp
#include <wsc/wsc.h>

int main()
{
    auto canvas = wsc::Canvas::create(wsc::Canvas::Backend::Software, 400, 400);
    if (!canvas || !canvas->initializeContext()) return 1;

    canvas->beginFrame();

    // === Drawing code goes here ===

    canvas->endFrame();
    canvas->savePixelsPPM("output.ppm");
    return 0;
}
```

Subsequent examples show only the "drawing code" section.

---

## 2.2 Coordinate System

WhatsCanvas uses a **top-left origin** coordinate system:

```
(0,0) ────────────→ X+
  │
  │
  │
  ↓
  Y+
```

`RectF` is constructed as `RectF(x, y, width, height)`: top-left position plus width and height.

---

## 2.3 Rectangles

### Filled Rectangle

```cpp
wsc::Paint fill;
fill.setColor(wsc::Color(66, 133, 244, 255));  // Google Blue
fill.setStyle(wsc::Paint::Style::FILL);          // FILL is the default

canvas->drawRect(wsc::RectF(50, 50, 120, 80), fill);
```

### Stroked Rectangle

```cpp
wsc::Paint stroke;
stroke.setColor(wsc::Color(219, 68, 55, 255));  // Google Red
stroke.setStyle(wsc::Paint::Style::STROKE);
stroke.setStrokeWidth(3.0f);

canvas->drawRect(wsc::RectF(50, 50, 120, 80), stroke);
```

### Fill + Stroke

```cpp
wsc::Paint both;
both.setColor(wsc::Color(244, 180, 0, 255));     // Fill color: gold
both.setStyle(wsc::Paint::Style::FILL_AND_STROKE);
both.setStrokeWidth(2.0f);
both.setStrokeColor(wsc::Color(100, 100, 100, 255)); // Stroke color: gray

canvas->drawRect(wsc::RectF(50, 50, 120, 80), both);
```

---

## 2.4 Rounded Rectangles

```cpp
wsc::Paint paint;
paint.setColor(wsc::Color(15, 157, 88, 255));
paint.setAntiAlias(true);

// Uniform corner radius
canvas->drawRoundRect(wsc::RectF(50, 50, 200, 120), 16.0f, paint);
```

Corner radius values:
- `radius = 0` is equivalent to a plain rectangle
- `radius = min(width, height) / 2` is equivalent to a pill shape

---

## 2.5 Circles

```cpp
wsc::Paint paint;
paint.setColor(wsc::Color(255, 87, 34, 255));
paint.setAntiAlias(true);

// drawCircle(centerX, centerY, radius, paint)
canvas->drawCircle(200.0f, 200.0f, 80.0f, paint);
```

---

## 2.6 Ovals

```cpp
wsc::Paint paint;
paint.setColor(wsc::Color(156, 39, 176, 255));
paint.setAntiAlias(true);

// drawOval(bounds, paint) — bounds is the oval's bounding rectangle
canvas->drawOval(wsc::RectF(80, 120, 240, 160), paint);
```

---

## 2.7 Arcs

```cpp
wsc::Paint paint;
paint.setColor(wsc::Color(0, 150, 136, 255));
paint.setAntiAlias(true);
paint.setStyle(wsc::Paint::Style::STROKE);
paint.setStrokeWidth(4.0f);

// drawArc(bounds, startAngleRad, sweepAngleRad, useCenter, paint)
// bounds:         bounding rectangle of the ellipse
// startAngleRad:  starting angle in radians (0 = 3 o'clock direction)
// sweepAngleRad:  swept angle
// useCenter:      true connects the center (pie slice); false draws only the arc

float pi = 3.14159265f;
canvas->drawArc(wsc::RectF(50, 50, 200, 200), 0, pi * 1.5f, false, paint);
```

### Filled Pie Slice

```cpp
wsc::Paint piePaint;
piePaint.setColor(wsc::Color(255, 193, 7, 255));
piePaint.setAntiAlias(true);

canvas->drawArc(wsc::RectF(50, 50, 200, 200), 0, pi * 0.75f, true, piePaint);
```

---

## 2.8 Lines

```cpp
wsc::Paint linePaint;
linePaint.setColor(wsc::Color(33, 33, 33, 255));
linePaint.setStrokeWidth(2.0f);
linePaint.setAntiAlias(true);

// drawLine(x1, y1, x2, y2, paint)
canvas->drawLine(50, 50, 350, 200, linePaint);
```

### Line Cap Styles

```cpp
linePaint.setStrokeCap(wsc::Paint::StrokeCap::BUTT);   // Butt cap (default)
linePaint.setStrokeCap(wsc::Paint::StrokeCap::ROUND);  // Round cap
linePaint.setStrokeCap(wsc::Paint::StrokeCap::SQUARE); // Square cap (extends past the endpoint)
```

---

## 2.9 Polygons

```cpp
wsc::Paint polyPaint;
polyPaint.setColor(wsc::Color(63, 81, 181, 200));
polyPaint.setAntiAlias(true);

// Define vertices
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

## 2.10 Box Shadow

Similar to CSS `box-shadow`, draws a shadow for a rectangular area:

```cpp
// drawBoxShadow(rect, cornerRadius, spread, blur, offsetX, offsetY, color)
canvas->drawBoxShadow(
    wsc::RectF(100, 100, 200, 150),  // Rectangle
    12.0f,                            // Corner radius
    0.0f,                             // Spread
    20.0f,                            // Blur radius
    4.0f,                             // X offset
    8.0f,                             // Y offset
    wsc::Color(0, 0, 0, 80)          // Shadow color (semi-transparent black)
);

// Draw the card body above the shadow
wsc::Paint cardPaint;
cardPaint.setColor(wsc::Color(255, 255, 255, 255));
canvas->drawRoundRect(wsc::RectF(100, 100, 200, 150), 12.0f, cardPaint);
```

---

## 2.11 Integrated Example: A Set of Cards

![Three dashboard cards composed from basic shapes](./images/chapter02_cards.png)

The key point: the cards use only rounded rectangles, circles, lines, and box shadows — no image assets. The code below matches the [compilable source](https://github.com/ClarkWain/WhatsCanvas/blob/main/examples/tutorials/chapter02_cards.cpp) that produced the picture.

<!-- BEGIN GENERATED EXAMPLE: examples/tutorials/chapter02_cards.cpp -->
```cpp
// Chapter 02 comprehensive example: a small dashboard made from basic shapes.
#include <wsc/wsc.h>

int main()
{
    // 1. Create an offscreen canvas. The sample image is produced directly by the Software backend.
    auto canvas = wsc::Canvas::create(wsc::Canvas::Backend::Software, 960, 540);
    if (!canvas || !canvas->initializeContext()) return 1;
    canvas->beginFrame();

    // 2. Draw a uniform light background.
    wsc::Paint background;
    background.setColor(wsc::Color(244, 247, 252, 255));
    canvas->drawRect(wsc::RectF(0, 0, 960, 540), background);

    // 3. Each card varies only in accent color and progress value; the layout stays the same.
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

    // 4. Compose rounded rectangles, circles, and shadows into a complete card.
    for (const auto &card : cards) {
        const wsc::RectF bounds(card.x, 90, 260, 360);

        // Card surface and top accent region.
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

        // Avatar placeholder and two title placeholders.
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

        // Body skeleton lines.
        wsc::Paint skeleton;
        skeleton.setColor(wsc::Color(220, 226, 238, 255));
        canvas->drawRoundRect(wsc::RectF(card.x + 28, 222, 178, 12), 6, skeleton);
        canvas->drawRoundRect(wsc::RectF(card.x + 28, 250, 136, 10), 5, skeleton);
        canvas->drawRoundRect(wsc::RectF(card.x + 28, 278, 194, 10), 5, skeleton);

        // Progress bar.
        wsc::Paint track;
        track.setColor(wsc::Color(231, 235, 244, 255));
        canvas->drawRoundRect(wsc::RectF(card.x + 28, 332, 204, 12), 6, track);
        track.setColor(card.accent);
        canvas->drawRoundRect(wsc::RectF(card.x + 28, 332, 204 * card.progress, 12), 6, track);

        // Footer status badge.
        wsc::Paint badge;
        badge.setColor(wsc::Color(
            card.accent.getR(), card.accent.getG(), card.accent.getB(), 28));
        canvas->drawRoundRect(wsc::RectF(card.x + 28, 382, 86, 30), 15, badge);
        badge.setColor(card.accent);
        canvas->drawCircle(card.x + 46, 397, 5, badge);
        badge.setColor(wsc::Color(190, 198, 214, 255));
        canvas->drawRoundRect(wsc::RectF(card.x + 60, 393, 36, 8), 4, badge);
    }

    // 5. Submit the draw commands and save the picture that matches the tutorial.
    canvas->endFrame();
    return canvas->savePixelsPPM("chapter02_cards.ppm") ? 0 : 2;
}
```
<!-- END GENERATED EXAMPLE: examples/tutorials/chapter02_cards.cpp -->

---

## 2.12 API Cheat Sheet

| Method | Arguments | Description |
|--------|-----------|-------------|
| `drawRect` | `(RectF, Paint)` | Rectangle |
| `drawRoundRect` | `(RectF, radius, Paint)` | Rounded rectangle |
| `drawCircle` | `(cx, cy, r, Paint)` | Circle |
| `drawOval` | `(RectF, Paint)` | Oval (bounding rectangle) |
| `drawArc` | `(RectF, start, sweep, center, Paint)` | Arc / pie |
| `drawLine` | `(x1, y1, x2, y2, Paint)` | Line segment |
| `drawPolygon` | `(points, count, Paint)` | Polygon |
| `drawBoxShadow` | `(RectF, radius, spread, blur, dx, dy, Color)` | Box shadow |

---

## 2.13 Summary

This chapter covered:

- [x] The WhatsCanvas coordinate system (origin at top-left, X to the right, Y downward)
- [x] Drawing rectangles (filled / stroked / filled + stroked)
- [x] Drawing rounded rectangles
- [x] Drawing circles, ovals, arcs / pie slices
- [x] Drawing line segments and line cap styles
- [x] Drawing polygons
- [x] Simulating card visuals with box shadows

**Next chapter**: [Paint in Depth](./03-paint-bindepth.md) — a deep dive into color, gradients, shadows, blend modes, and other Paint attributes.

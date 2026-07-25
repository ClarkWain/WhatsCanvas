# Text Sharpness & HiDPI

WhatsCanvas rasterizes glyphs into a GPU glyph atlas and composites them as
textured quads. This note explains how the engine keeps text crisp — including
under zoom, scale, and high-DPI displays — and how to opt into HiDPI rendering.

## Why text can look blurry (and what the engine does about it)

Glyphs are rasterized at an integer pixel size, so their atlas texels map 1:1 to
screen pixels **only** when the glyph lands on an integer pixel boundary at its
native resolution. Three independent effects break that mapping; the engine
handles each automatically:

1. **Fractional placement.** If a glyph quad's device origin is fractional, the
   bilinear sampler averages neighbouring texels and a solid stem loses 20–49%
   of its coverage — the classic "washed / blurry" look. The engine snaps glyph
   quads to the device pixel grid whenever the current transform is a pure
   axis-aligned, unit-scale transform (identity / translation — the common UI
   case). Rotated or scaled text is left unsnapped.

2. **Magnified low-resolution glyphs.** If text is drawn under a scaled transform
   (for example a game that fits a design resolution to the window with
   `canvas.scale(s, s)`), a logically-sized glyph would be bilinearly magnified.
   Instead, the engine reads the transform's effective device-space scale and
   rasterizes the glyph at that resolution, then draws it 1:1. Zoomed, scaled,
   and rotated text stay crisp. The effective pixel size is quantized (to bound
   atlas growth) and capped.

3. **High-DPI displays.** See below — `setDevicePixelRatio` folds the display
   scale into the same effective-scale path.

## HiDPI with `setDevicePixelRatio`

On a high-DPI display the OS reports a **content scale** (e.g. 2.0 on a "Retina"
screen): the physical framebuffer is larger than the logical window. To render
crisply, size the canvas to the **physical framebuffer** and tell it the ratio:

```cpp
// After creating the window and loading GL:
int fbW = 0, fbH = 0;
glfwGetFramebufferSize(window, &fbW, &fbH);

float sx = 1.0f, sy = 1.0f;
glfwGetWindowContentScale(window, &sx, &sy);
const float dpr = sx > 0.0f ? sx : 1.0f;

auto canvas = wsc::Canvas::create(wsc::Canvas::Backend::OpenGL, 0, 0);
canvas->setSize(fbW, fbH);          // physical framebuffer pixels
canvas->setDevicePixelRatio(dpr);   // logical -> physical scale
canvas->initializeContext();
canvas->beginFrame();

// From here on, draw in LOGICAL coordinates:
canvas->drawText("Crisp on HiDPI", 24.0f, 40.0f, paint);  // 24,40 are logical px
canvas->endFrame();
```

Semantics:

- The device pixel ratio is folded into the **root transform**, so `resetMatrix()`
  restores a `ratio`-scaled base. Draw in logical coordinates; everything —
  including text — renders at physical resolution.
- Text automatically reuses the device-resolution rasterization path (effect 2
  above) and stays sharp.
- The public matrix API stays **logical**: `setMatrix()` composes onto the ratio
  base (so an absolute `setMatrix` never silently drops HiDPI), `getMatrix()`
  returns the logical matrix you built, and `mapPoint()` reports device pixels.
- Default ratio is `1.0`, a no-op — existing code is unaffected.

Handle window resizes by re-applying the size and ratio:

```cpp
void framebuffer_size_callback(GLFWwindow *window, int width, int height) {
    glViewport(0, 0, width, height);
    float sx = 1.0f, sy = 1.0f;
    glfwGetWindowContentScale(window, &sx, &sy);
    const float dpr = sx > 0.0f ? sx : 1.0f;
    canvas->setSize(width, height);        // physical
    canvas->setDevicePixelRatio(dpr);
    // lay out in logical units: width / dpr, height / dpr
}
```

The `examples/game/*` samples (Tetris, Racer, Bubble Shooter) use exactly this
pattern: they lay the scene out in logical design units and set the device pixel
ratio from the window content scale.

## Not yet: gamma-correct text edges

Text anti-aliasing is currently blended in sRGB space, which makes AA edges a
little heavier than a gamma-correct blend would. Gamma-correct rendering is
available globally via `Canvas::setGammaCorrect(true)` but is off by default,
because turning it on changes **all** rendering (shapes, gradients, images) and
would regenerate every golden-image baseline. It is intentionally a separate
rendering-policy decision, not part of the text-sharpness fixes above.

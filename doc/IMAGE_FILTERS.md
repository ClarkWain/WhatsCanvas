# Image Filters And Backdrop Effects

WhatsCanvas models pixel effects as filters attached to a saved layer. This
keeps filtering separate from geometry, clipping, and paint styles while
preserving immediate-mode Canvas drawing.

## Public API

`LayerOptions::imageFilter()` processes content drawn inside the saved layer.
`LayerOptions::backdropFilter()` snapshots and processes commands recorded
before the layer, then places the layer content above that filtered backdrop.

```cpp
wsc::Paint compositePaint;
compositePaint.setColor(wsc::Color(255, 255, 255, 255));

wsc::LayerOptions options;
options.setBackdropFilter(wsc::ImageFilter::blur(18.0f));

canvas->saveLayer(panelBounds, compositePaint, options);
canvas->clipPath(panelShape);

wsc::Paint tint;
tint.setColor(wsc::Color(255, 255, 255, 64));
canvas->drawPath(panelShape, tint);
canvas->drawText("Backdrop filter", x, y, textPaint);

canvas->restore();
```

The existing `saveLayer(bounds, paint)` overloads remain unchanged. Filters are
opt-in through the overloads that accept `LayerOptions`.

## Blur Semantics

- Blur radii use filter-target pixels and are clamped to 64 pixels so Software
  and GL-family backends use the same kernel reach.
- The current implementation uses a separable Gaussian kernel whose radius
  reaches approximately three standard deviations.
- RGBA filtering accumulates premultiplied RGB and unpremultiplies the result,
  avoiding colored fringes around transparent content.
- `Clamp` repeats edge pixels outside the source image. `Decal` treats samples
  outside the source image as transparent.
- A backdrop sees only drawing commands recorded before `saveLayer`.
- The filter capture is expanded by its radius, while final composition remains
  clipped to the original layer bounds and active Canvas clip.
- Layer paint alpha, tint, color matrix, image sampling, and blend mode are
  applied when the completed layer is composited into its parent.

## Backend Status

| Backend | Image blur | Backdrop blur | Execution |
| --- | --- | --- | --- |
| Software | Supported | Supported | Deterministic CPU separable Gaussian reference |
| OpenGL / OpenGLES | Supported | Supported | Two-pass GPU RGBA Gaussian |
| Vulkan | Planned | Planned | Must remain GPU-side; no readback fallback |

Unsupported filters degrade gracefully: ordinary layer content is preserved,
and an unavailable backdrop filter becomes a no-op.

## Validation

- `WhatsCanvasSoftwareRendererTests` covers empty-layer backdrop composition,
  hard-edge background diffusion, layer-content blur, alpha preservation, and
  layer-bound clipping.
- `WhatsCanvasOpenGLBackdropFilterTests` validates real GPU output through a
  hidden OpenGL 3.3 context and pixel readback.
- `WhatsCanvasRenderTargetPoolTests` prevents offscreen targets from being
  reused while a deferred filter/composite command still references their
  texture.

## Roadmap

1. Add Vulkan GPU blur and OpenGL/Vulkan pixel-parity coverage.
2. Add composable color-matrix and offset filter nodes.
3. Add adaptive downsampling for large animated blur regions.
4. Add a high-level frosted-glass helper implemented on top of `saveLayer`.
5. Add filter pass/cache statistics and representative performance benchmarks.

The core API deliberately starts with a small filter surface. Inner shadows,
morphology, displacement maps, and custom shader filters remain optional
extensions after blur performance and cross-backend behavior are stable.

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
options.setBackdropFilter(wsc::ImageFilter::frostedGlass(
    12.0f, // Gaussian sigma
    1.18f, // saturation
    1.04f, // brightness
    1.02f, // contrast
    0.012f // grain
));

canvas->saveLayer(panelBounds, compositePaint, options);
canvas->restore();

wsc::Paint tint;
tint.setColor(wsc::Color(255, 255, 255, 64));
canvas->drawRect(panelBounds, tint);
canvas->drawText("Backdrop filter", x, y, textPaint);
```

For a rounded panel, establish the clip before the layer. Tint, gradients,
images, and text may remain inside the layer when they need the same group
opacity or image filter:

```cpp
canvas->save();
canvas->clipPath(roundedPanelPath);
canvas->saveLayer(panelBounds, compositePaint, options);

canvas->drawPath(roundedPanelPath, tint);
canvas->drawText("Sharp foreground", x, y, textPaint);
canvas->restore(); // composite the filtered backdrop and layer content
canvas->restore(); // release the rounded clip
```

The existing `saveLayer(bounds, paint)` overloads remain unchanged. Filters are
opt-in through the overloads that accept `LayerOptions`.

## Blur Semantics

- Blur radii use filter-target pixels and are clamped to 64 pixels so Software
  and GL-family backends use the same kernel reach.
- `blurSigma()` uses standard deviation, then derives a sampled radius of
  `3 * sigma`. This convention is convenient when matching design tools and
  APIs such as Skia. Sigma is therefore clamped to `64 / 3`.
- The current implementation uses a separable Gaussian kernel whose radius
  reaches approximately three standard deviations.
- GL-family backends automatically use a conservative 2x downsample when both
  target dimensions are at least 128 pixels and either blur radius reaches 24
  pixels. The Gaussian kernel is scaled into the reduced target, cutting the
  dominant convolution work to roughly one quarter.
- Downsampled filters use a full-resolution restore pass. Saturation,
  brightness, contrast, and grain are applied there so color treatment remains
  stable and monochrome grain does not become blocky when enlarged.
- RGBA filtering accumulates premultiplied RGB and unpremultiplies the result,
  avoiding colored fringes around transparent content.
- `Clamp` repeats edge pixels outside the source image. `Decal` treats samples
  outside the source image as transparent.
- A backdrop sees only drawing commands recorded before `saveLayer`.
- The filter capture is expanded by its radius, while final composition remains
  cropped to the original layer bounds. Pixels just outside the layer can
  contribute to edge blur without the sampling outset becoming visible output.
- Layer paint alpha, tint, color matrix, image sampling, and blend mode are
  applied when the completed layer is composited into its parent.

## Frosted Glass Treatment

`frostedGlass()` is a convenience factory over the same blur implementation.
After the two blur passes it applies:

1. Rec. 709 luminance-based saturation adjustment.
2. Contrast around mid-gray, followed by brightness scaling.
3. Stable monochrome grain in framebuffer pixel space.

The color treatment affects the filtered image only. Layer content drawn after
`saveLayer()` remains sharp and keeps its own `Paint` styling. Grain values
around `0.005-0.02` are intended to reduce banding; larger values are an
intentional texture effect. `setColorAdjustment()` and `setGrain()` are also
available on a filter created by `blur()` or `blurSigma()`.

OpenGL offscreen replay uses the same command path as direct rendering, so
paths, gradients, images, and text retain their active arbitrary-path clip.
Multiple independent rounded backdrop layers can therefore be composed in
stream order. Keeping the filtered layer backdrop-only and drawing sharp
foreground content after restoring it remains the cheapest arrangement when
the foreground does not need group opacity or an image filter.

## Backend Status

| Backend | Image blur | Backdrop blur | Execution |
| --- | --- | --- | --- |
| Software | Supported | Supported | Deterministic CPU separable Gaussian reference |
| OpenGL / OpenGLES | Supported | Supported | Two-pass GPU RGBA Gaussian; adaptive 2x blur plus full-resolution restore for large kernels |
| Vulkan | Planned | Planned | Must remain GPU-side; no readback fallback |

Unsupported filters degrade gracefully: ordinary layer content is preserved,
and an unavailable backdrop filter becomes a no-op.

## Validation

- `WhatsCanvasSoftwareRendererTests` covers empty-layer backdrop composition,
  hard-edge background diffusion, post-blur color adjustment, sampling-outset
  cropping, layer-content blur, alpha preservation, and layer-bound clipping.
- `WhatsCanvasOpenGLBackdropFilterTests` validates real GPU output through a
  hidden OpenGL 3.3 context and pixel readback, including color adjustment and
  sampling-outset cropping.
- `WhatsCanvasRenderTargetPoolTests` prevents offscreen targets from being
  reused while a deferred filter/composite command still references their
  texture.

## Roadmap

1. Add Vulkan GPU blur and OpenGL/Vulkan pixel-parity coverage.
2. Add a composable filter graph with generic color-matrix and offset nodes.
3. Extend the backend-neutral shared encoder to arbitrary clipped image and
   gradient primitives for non-GL device command execution.
4. Add filter pass/cache statistics and representative performance benchmarks.

The core API deliberately starts with a small filter surface. Inner shadows,
morphology, displacement maps, and custom shader filters remain optional
extensions after blur performance and cross-backend behavior are stable.

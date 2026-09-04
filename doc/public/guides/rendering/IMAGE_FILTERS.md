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

### Inner shadow

`innerShadow()` derives an inset shadow from the alpha of the layer content:

```cpp
wsc::LayerOptions insetOptions;
insetOptions.setImageFilter(wsc::ImageFilter::innerShadow(
    12.0f,                  // Gaussian radius
    4.0f, 5.0f,            // X/Y offset
    wsc::Color(0, 0, 0, 180)
));

canvas->saveLayer(cardBounds, compositePaint, insetOptions);
canvas->drawPath(roundedCardPath, cardPaint);
canvas->restore();
```

Positive X shades the inside-left edge and positive Y shades the inside-top
edge, matching common inset-shadow conventions. The shadow is clipped to the
source silhouette, preserves the source alpha, and never expands the visible
layer output. The capture still grows by `radius + abs(offset)` so the
transparent area needed to form the edge is available to the filter. Radii are
clamped to 64 pixels and offsets to 256 pixels for backend parity.

### Composable filter chains

`ImageFilterChain` keeps node order explicit and supports up to eight nodes.
Existing blur and inner-shadow values can be mixed with a row-major 4x5 RGBA
color matrix and a transparent-boundary offset:

```cpp
const std::array<float, 20> desaturate = {
    0.30f, 0.59f, 0.11f, 0.0f, 0.0f,
    0.30f, 0.59f, 0.11f, 0.0f, 0.0f,
    0.30f, 0.59f, 0.11f, 0.0f, 0.0f,
    0.0f,  0.0f,  0.0f,  1.0f, 0.0f,
};

wsc::ImageFilterChain chain;
chain.append(wsc::ImageFilter::blurSigma(4.0f))
    .appendColorMatrix(desaturate)
    .appendOffset(6.0f, 4.0f);

wsc::LayerOptions options;
options.setImageFilter(chain);
canvas->saveLayer(bounds, compositePaint, options);
// Draw layer content.
canvas->restore();
```

The matrix uses four rows for output R, G, B, and A. Each row contains four
channel multipliers followed by an additive normalized-channel offset. Offset
nodes use Decal semantics: newly exposed pixels are transparent. Generic nodes
are lowered through the same backend image path used by ordinary Canvas
drawing, so Software, OpenGL, OpenGLES, and Vulkan preserve the same order.
Sampling and output bounds conservatively accumulate across the chain.

## Blur Semantics

- Blur radii use filter-target pixels and are clamped to 64 pixels so Software,
  GL-family, and Vulkan backends use the same kernel reach.
- `blurSigma()` uses standard deviation, then derives a sampled radius of
  `3 * sigma`. This convention is convenient when matching design tools and
  APIs such as Skia. Sigma is therefore clamped to `64 / 3`.
- The current implementation uses a separable Gaussian kernel whose radius
  reaches approximately three standard deviations.
- GPU backends independently use a conservative 2x downsample on each axis
  whose target extent is at least 128 pixels and whose blur radius reaches 24
  pixels. A vertical-only large blur therefore keeps full horizontal detail,
  while a large two-axis blur cuts the dominant convolution work to roughly one
  quarter.
- Downsampled filters use a full-resolution restore pass. Saturation,
  brightness, contrast, and grain are applied there so color treatment remains
  stable and monochrome grain does not become blocky when enlarged.
- RGBA filtering accumulates premultiplied RGB and unpremultiplies the result,
  avoiding colored fringes around transparent content.
- `Clamp` repeats edge pixels outside the source image. `Decal` treats samples
  outside the source image as transparent.
- A backdrop sees only drawing commands recorded before `saveLayer`.
- The filter capture is expanded by its radius. Backdrop filters and inner
  shadows remain cropped to the original layer bounds; a layer-content blur
  may expand its visible result by its blur radius. Pixels outside the original
  bounds can therefore contribute without exposing a sampling-only outset.
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

| Backend | Image blur | Backdrop blur | Inner shadow | Color matrix / offset chain | Execution |
| --- | --- | --- | --- | --- | --- |
| Software | Supported | Supported | Supported | Supported | Deterministic CPU reference |
| OpenGL / OpenGLES | Supported | Supported | Supported | Supported | GPU Gaussian/filter passes plus shared image-pass lowering |
| Vulkan | Supported | Supported | Supported | Supported | GPU Gaussian/filter passes plus shared image-pass lowering |

Unsupported filters degrade gracefully: ordinary layer content is preserved,
and an unavailable backdrop filter becomes a no-op.

## Validation

- `WhatsCanvasSoftwareRendererTests` covers empty-layer backdrop composition,
  hard-edge background diffusion, post-blur color adjustment, sampling-outset
  cropping, layer-content blur, inner-shadow direction and clipping, alpha
  preservation, layer-bound clipping, and ordered color-matrix/offset output.
- `WhatsCanvasOpenGLBackdropFilterTests` validates real GPU output through a
  hidden OpenGL 3.3 context and pixel readback, including color adjustment and
  sampling-outset cropping, inner-shadow output, framebuffer state restoration,
  layer-bound isolation, and ordered color-matrix/offset output.
- `WhatsCanvasVulkanImageFilterTests` validates the native Vulkan image handle,
  GPU blur diffusion, Clamp/Decal edges, alpha-safe color adjustment, adaptive
  per-axis downsampling, transparent-edge color safety, public image/backdrop
  filtering, inner-shadow composition, premultiplied translucent layers, layer
  orientation, and cropped gradient/image clipping. Backdrop and inner-shadow
  output are also checked against the Software reference. The composable
  color-matrix/offset scene is byte-identical between Vulkan and Software.
- `WhatsCanvasOpenGLFilterPixelParityTests`,
  `WhatsCanvasOpenGLESFilterPixelParityTests`, and
  `WhatsCanvasVulkanFilterPixelParityTests` render the same deterministic
  zero-grain composite scene and enforce bounded max/mean premultiplied-RGBA
  error and bad-pixel ratio against Software. Linux CI supplies Mesa GL/EGL and
  lavapipe contexts, so an unavailable context fails instead of silently
  skipping.
- `WhatsCanvasRenderTargetPoolTests` prevents offscreen targets from being
  reused while a deferred filter/composite command still references their
  texture.
- `Canvas::getRenderStats()` exposes `filterCount`, `filterPassCount`,
  `downsampledFilterCount`, `filterInputPixelCount`, and
  `filterPixelPassCount`. These counters reset with the frame and make
  expensive glass layouts visible without backend-specific tooling.
- `WhatsCanvasCoreBenchmarks` includes a repeatable
  `software_backdrop_blur_320x180_r24` workload.
  `WhatsCanvasImageFilterBenchmarks` measures four overlapping glass panels and
  24 inner-shadow controls through Software/OpenGL/Vulkan, with structured
  timing, hash, pass, downsample, and pixel-work output. The OpenGL showcase
  prints the same counters for visual profiling.

## Roadmap

1. Extend the backend-neutral shared encoder to arbitrary clipped image and
   gradient primitives for non-GL device command execution.
2. Add morphology and displacement-map nodes after their cross-backend pixel
   contracts are defined.
3. Expand real-device performance baselines for large and overlapping glass
   regions.

The core API deliberately keeps a small filter surface. Morphology,
displacement maps, and custom shader filters remain optional extensions after
the current effects and cross-backend behavior are stable.

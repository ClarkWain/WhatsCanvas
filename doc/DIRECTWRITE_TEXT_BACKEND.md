# DirectWrite Text Backend (Windows)

WhatsCanvas ships a native Windows text backend built on **DirectWrite** (shaping,
layout, metrics, font enumeration) and **Direct2D + WIC** (headless glyph
rasterization). It is an alternative to the portable FreeType/stb glyph-atlas
backend and produces high-quality, OS-consistent text on Windows.

## Selecting the backend

Select it from the public `Canvas` API (portable is the default):

```cpp
// Native DirectWrite with ClearType, else portable fallback off Windows.
canvas->setTextBackend(wsc::Canvas::TextBackend::DirectWrite,
                       wsc::Canvas::TextRenderMode::ClearType);
if (canvas->textBackend() == wsc::Canvas::TextBackend::DirectWrite) {
    // native backend active
}
```

`setTextBackend` resets text state (registered fonts / fallback chains), so call
it before registering fonts. A `DirectWrite` request falls back to the portable
glyph-atlas backend when DirectWrite is unavailable.

The backend can also be created directly through the internal factory:

```cpp
#include "text/DirectWriteTextBackend.h"

if (wsc::text::isDirectWriteAvailable()) {                 // true on _WIN32
    wsc::text::DirectWriteBackendOptions options;
    options.rasterMode = wsc::text::DirectWriteRasterMode::Grayscale; // or ClearType
    std::unique_ptr<wsc::text::ITextBackend> backend =
        wsc::text::createDirectWriteTextBackend(options);   // nullptr off Windows
}
```

## What it does

- **Shaping & layout** via `IDWriteTextLayout` (complex scripts, bidi, kerning).
- **Metrics** — width/height, and ascent/descent/lineGap from the font's design
  metrics.
- **Rasterization** into an RGBA bitmap through a Direct2D WIC render target.
- **System font fallback** — `IDWriteTextLayout` resolves missing glyphs
  automatically.

## Raster modes

| Mode | Use when | Notes |
| --- | --- | --- |
| `Grayscale` (default) | Any surface, transforms, animation | Coverage in the alpha channel; white RGB. Safe and portable. |
| `ClearType` | Axis-aligned text over a known opaque background | Preserves horizontal RGB subpixel coverage; best-effort composite. Avoid under alpha blending / transforms (colored fringes). |

## Text styling surface

All of these `Paint` knobs flow through to DirectWrite:

- `setFontFamily` / `setFontWeight` (100–900) / `setFontSlant` (normal/italic/oblique)
- `setTextSize`
- `setLetterSpacing` — baked into the layout via `IDWriteTextLayout1::SetCharacterSpacing`, so measurement and rendering stay consistent
- `setTextAlign` / `setTextBaseline`
- `setTextLocale` (BCP-47, e.g. `"en-US"`, `"ja-JP"`) — locale-aware shaping and Han-unification fallback
- `setUnderline` / `setStrikethrough` — text decorations drawn by DirectWrite over the run

## Custom fonts

`registerFontFace` accepts both on-disk and in-memory fonts:

```cpp
backend->registerFontFace(wsc::FontFace::fromFile(wsc::FontDescriptor("Inter"),
                                                  "C:/fonts/Inter-Regular.ttf"));

std::vector<std::uint8_t> bytes = /* load a .ttf/.otf */;
backend->registerFontFace(wsc::FontFace::fromMemory(wsc::FontDescriptor("Inter"), bytes));
```

- On-disk fonts use a custom `IDWriteFontCollectionLoader` + file enumerator.
- In-memory fonts use `IDWriteInMemoryFontFileLoader` (`IDWriteFactory5`,
  Windows 10+); unavailable factories degrade gracefully (returns `false`).
- A registered family is preferred over the system collection; the system
  collection still drives automatic fallback.

## Custom font fallback chains

```cpp
wsc::FontFallbackChain chain("Inter");
chain.addFallbackFamily("Yu Gothic");      // Japanese
chain.addFallbackFamily("Microsoft YaHei"); // Simplified Chinese
backend->setFontFallbackChain(chain);
```

Builds an `IDWriteFontFallback` (via `IDWriteFontFallbackBuilder`) mapping the
Unicode range to the requested families in order, then appends the system
fallback so any uncovered scripts still resolve. An empty chain clears it.

## HiDPI

DirectWrite text participates in the same device-resolution path as the portable
backend: under a scaled transform or `setDevicePixelRatio`, glyphs rasterize at
the effective device pixel size and stay crisp. See
[Text Sharpness & HiDPI](TEXT_SHARPNESS_AND_HIDPI.md).

## Known limitations

- **Bitmap per draw**: text is rasterized to a CPU bitmap and uploaded as a GPU
  texture each `drawText`. The COM apartment, the WIC/D2D factories, and now the
  position-independent bitmap + intrinsic metrics are cached on the backend
  (LRU, 4 MB byte budget by default), so repeated identical text hits the cache
  and reuses the pixels; only the on-screen position is re-applied. A shared GPU
  glyph atlas that also skips the per-draw texture upload remains a future
  optimization.
- **ClearType** is best-effort: it renders white-on-opaque-black and derives an
  alpha from the brightest subpixel, so true subpixel sharpness only holds for
  axis-aligned text over an opaque destination.
- **Per-`Paint` render-mode override** is not yet wired; the render mode is chosen
  when the backend is selected via `setTextBackend`.

# WhatsCanvas Text Rendering Handoff

Status: implementation handoff for the text-rendering owner.

## Executive summary

WhatsCanvas has a usable cross-platform font lookup and fallback system, but it
does not currently provide Windows-native DirectWrite/ClearType output. The
normal path is a portable FreeType/STB-style glyph atlas with grayscale alpha
coverage. This is portable and composable, but it looks softer than native
Windows UI text at small sizes.

The current Windows-native bitmap fallback is GDI `TextOutW`, not DirectWrite.
It explicitly converts RGB output into one alpha channel, so any ClearType
subpixel information is discarded before compositing.

## Current font selection

The logical primary family is `WhatsCanvas Sans`; the actual platform files
are registered by `FontSystem::defaultSystemFontFaces()`.

| Platform | Latin primary | CJK fallback | Serif | Monospace |
| --- | --- | --- | --- | --- |
| Windows | Segoe UI regular/bold | Microsoft YaHei | Georgia | Consolas |
| macOS | SFNS/San Francisco | PingFang | Georgia | Menlo |
| Linux | DejaVu Sans | Noto Sans CJK | DejaVu Serif | DejaVu Sans Mono |

The default fallback order is primary, CJK, Arabic, Hebrew, Symbol, Serif,
then Mono. On Windows, Arabic/Hebrew map to Arial and Symbol maps to Segoe UI
Symbol. Font files are hard-coded by path; this is not an OS-native font
fallback resolver.

Relevant source:

- `include/wsc/Font.h`: default families, file registrations, fallback chain.
- `src/text/BasicTextBackend.cpp`: face selection and raster path.

## Current rendering paths

1. Portable glyph atlas (normal path)
   - Rasterizes glyph alpha coverage and composites it through WhatsCanvas.
   - Cross-platform and suitable for transparent surfaces, transforms, effects,
     screenshots, and GPU/Software parity.
   - No RGB subpixel ClearType information.

2. Windows-native bitmap fallback
   - Activated only when native text is enabled **and** `Paint` has an explicit
     font family.
   - Uses GDI `TextOutW` into a DIB, not DirectWrite.
   - Takes `max(R, G, B)` as alpha, therefore converts any color/subpixel
     output to grayscale alpha.
   - Current WhatsUI `Text` has no font-family API, so it normally does not
     select this path.

3. DirectWrite
   - `TextBackendKind::DirectWrite` exists as an enum/API placeholder.
   - Capability discovery currently reports it unavailable and construction
     disables native text. It is not an implementation.

## Why text differs from native Windows UI

- No DirectWrite shaping/rasterization or system font fallback resolver.
- No ClearType RGB subpixel output retained through composition.
- The portable atlas uses grayscale coverage, which is correct for arbitrary
  transparency but softer at small pixel sizes.
- Small text was previously rendered at 1x in WhatsUI; HiDPI scaling has now
  been added in WhatsUI, but raster quality still depends on the backend.
- The default Windows CJK face is Microsoft YaHei, not necessarily the same
  family used by a native Windows control in the active locale.

## Recommended implementation order

1. Implement a real DirectWrite backend.
   - Use `IDWriteFactory`, `IDWriteTextFormat`, `IDWriteTextLayout`, and
     `IDWriteFontFallback`.
   - Obtain metrics and glyph runs from the same layout object used to paint.
   - Support weight, style, stretch, locale, OpenType features, and fallback.

2. Support two explicit raster modes.
   - `Grayscale`: default for transparent backgrounds, animation, transforms,
     composited layers, screenshots, and cross-backend consistency.
   - `ClearType`: only for axis-aligned text over a known opaque background.
     It must be disabled when alpha compositing could cause colored fringes.

3. Preserve subpixel data where ClearType is allowed.
   - Do not collapse RGB channels to `max(R,G,B)`.
   - Carry RGB coverage or render DirectWrite output directly into an opaque
     final target.

4. Expose text styling in WhatsUI.
   - `fontFamily`, `fontWeight`, `fontStyle`, `letterSpacing`, `locale`, and
     a text rendering mode preference.
   - The exact same parameters must be supplied to measure and paint.

5. Add test scenes.
   - 10/12/14/16px Latin and CJK text at 1x, 1.25x, 1.5x, and 2x.
   - Light/dark opaque panels, translucent panels, transforms, and scrolling.
   - Pixel tests for grayscale mode; visual/manual platform tests for
     ClearType, because it is display-dependent.

## Acceptance criteria

- At 1x, 12–14px Segoe UI text visually matches a native DirectWrite control
  in grayscale mode within an agreed screenshot tolerance.
- ClearType is only enabled on safe opaque targets and does not produce color
  fringes on translucent UI.
- Layout width, ascent, descent, and baseline come from the same backend run
  used for rendering.
- Windows font fallback covers Latin, Simplified Chinese, Arabic, Hebrew,
  symbols, and emoji without ASCII replacement.


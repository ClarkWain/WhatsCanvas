# Pluggable Text, Font, and Shaping Architecture

## Status

Accepted.

## Context

The text subsystem must support multiple languages, fallback fonts, shaping,
layout, platform portability, and deterministic fallback behavior. The initial
implementation was intentionally lightweight; the current tree has since
landed the portable glyph-atlas path and a Windows DirectWrite adapter.

That rules out continuing to grow text capabilities directly inside the canvas core with ad-hoc flags.

## Decision

Text moves to a dedicated, pluggable subsystem.

The current implementation uses the following interface-first structure:

- `ITextBackend` defines the text measurement and render-planning surface consumed by `Canvas`.
- `BasicTextBackend` is the default facade and owns backend selection, fallback,
  diagnostics, and cache coordination; its portable glyph-atlas path is the
  cross-platform baseline.
- The portable path uses registered/system font faces, optional HarfBuzz
  shaping, optional FreeType rasterization, and a reusable glyph atlas.
- `DirectWriteTextBackend` is the Windows native adapter for explicit
  `Canvas::TextBackend::DirectWrite` selection. It provides DirectWrite layout,
  custom font registration, fallback chains, grayscale/ClearType raster modes,
  and bounded bitmap/GPU texture caches.
- `TextUtils` contains shared UTF-8, bidi, layout, and geometry helpers.
- `NativeText` remains the Windows compatibility path for the legacy native
  text mode; it is distinct from DirectWrite.

The target architecture is:

- `FontManager`
- `FontCollection`
- `FontFace`
- `TextShaper`
- `TextRun`
- `TextLayout`
- `GlyphAtlas`
- backend/platform adapters

The current implementation now exposes explicit backend slots and capability queries:

- Portable glyph-atlas backend: implemented and required.
- Windows native compatibility path: available on Windows when native text is enabled.
- HarfBuzz shaping adapter: optional at build time.
- DirectWrite adapter: shipped on Windows and selectable through the public
  `Canvas` API; requests fall back to the portable path when unavailable.
- CoreText adapter: shipped on Apple platforms and selectable through the public
  `Canvas` API; non-Apple requests fall back to the portable path.

Current follow-up priorities:

1. Expand cross-platform HarfBuzz + FreeType parity and validation.
2. Expand CoreText color-glyph and cross-version pixel validation.
3. Evaluate a shared glyph atlas/cache strategy for distinct repeated native
   text draws.
4. Keep a browser/WASM-compatible path as a longer-term extension.

## Rules

- Canvas text APIs must consume text/layout abstractions, not backend-specific font handles.
- Measurement and rendering must come from the same shaping/layout pipeline.
- Font fallback is owned by the text subsystem, not by `Canvas` call sites.

## Consequences

### Positive

- The project gains a path to real Unicode, CJK, RTL, and complex script support.
- Platform-native and cross-platform text engines can coexist.
- Even the temporary backend path now has clearer cache ownership and bounded memory growth.

### Negative

- The text stack is a first-class subsystem with its own cache and testing
  needs.
- Native text backends can differ in rasterization details, so callers should
  choose grayscale when they need compositing safety or cross-backend parity.

## Follow-up

1. Continue cross-platform font-stack and pixel-validation coverage.
2. Expand CoreText simulator/device and color-glyph validation.
3. Consider batching distinct DirectWrite strings through a shared glyph atlas
   if profiling shows the bitmap cache is insufficient.

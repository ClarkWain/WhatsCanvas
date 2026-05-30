# ADR-003: Pluggable Text, Font, and Shaping Architecture

## Status

Accepted.

## Context

Current text rendering is intentionally lightweight and useful for demos, but it is not a production typography stack. The project goals require support for multiple languages, fallback fonts, shaping, layout, and platform portability.

That rules out continuing to grow text capabilities directly inside the canvas core with ad-hoc flags.

## Decision

Text moves to a dedicated, pluggable subsystem.

The current implementation has started that migration with a small interface-first slice:

- `ITextBackend` defines the text measurement and render-planning surface consumed by `Canvas`.
- `BasicTextBackend` is the default implementation used today.
- `BasicTextBackend` now keeps a bounded cache for Windows native text measurement and bitmap generation instead of unbounded growth.
- `TextUtils` contains the current ASCII fallback measurement and geometry generation helpers.
- `NativeText` contains the current Windows GDI measurement and bitmap generation helpers.

The target architecture is:

- `FontManager`
- `FontCollection`
- `FontFace`
- `TextShaper`
- `TextRun`
- `TextLayout`
- `GlyphAtlas`
- backend/platform adapters

Planned adapter priorities:

1. Windows: DirectWrite adapter.
2. Cross-platform: HarfBuzz + FreeType adapter.
3. Apple platforms: CoreText adapter.
4. Web: browser / WASM-compatible path.

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

- The text stack becomes a first-class subsystem with its own cache and testing needs.
- Existing simple text flows will need migration once the new subsystem lands.

## Follow-up

1. Expand `ITextBackend` into richer shaping/layout abstractions (`TextRun`, `TextLayout`, glyph caching).
2. Replace the Windows GDI helper path with a real DirectWrite adapter.
3. Add a cross-platform HarfBuzz + FreeType backend and make backend selection explicit.
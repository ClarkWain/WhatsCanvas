# WhatsCanvas Text Feature Matrix

This matrix defines the production text surface for WhatsCanvas. It separates what is already supported, what has a stable contract, and what still needs a rendering backend implementation.

## Current Support

| Capability | Status | Notes |
| --- | --- | --- |
| UTF-8 input validation | Supported | Invalid byte sequences are normalized before measurement/render paths. |
| ASCII fallback geometry | Supported | Basic backend can render normalized text through ASCII fallback geometry. |
| Native Windows text bitmap path | Supported | Used when a font family is supplied and native measurement/render succeeds. |
| Cross-platform font rasterization | Supported | Registered file-backed or memory-backed TrueType faces can be rasterized through the portable font rasterizer. |
| Atlas-backed glyph rendering | Supported | Rasterized glyphs are packed into `GlyphAtlas`; Canvas submits atlas quads through the image path. |
| Public font face model | Supported | `FontFace`, `FontDescriptor`, `FontFallbackChain`, and `FontManager` are public value/model types. |
| Font file registration contract | Supported | `ITextBackend::registerFontFace` accepts file-backed faces. |
| Font memory registration contract | Supported | `ITextBackend::registerFontFace` accepts memory-backed faces. |
| Fallback chain contract | Supported | `ITextBackend::setFontFallbackChain` and `resolveFontFamilies` define resolution order. |
| Text metrics | Supported | `measureText`, `measureTextBounds`, `measureTextMetrics`, and backend metrics are available. |
| Bounded multiline layout | Supported | `Canvas::layoutTextBox` returns line rows, source ranges, widths, line height, and ellipsis state. |
| Text box rendering | Supported | `drawTextBox` uses the same layout path as `layoutTextBox`. |
| Letter spacing | Supported | Basic geometry and native bitmap paths apply letter spacing. |
| Alignment and baseline | Supported | Left/center/right and top/middle/bottom modes are exposed through `Paint`. |
| Text on path | Supported | Current implementation uses ASCII fallback glyph placement. |
| Glyph atlas ownership | Contract supported | `GlyphAtlas` owns atlas allocation, glyph upload, eviction, pending rebuild keys, and context rebuild hooks. |
| Stroke text | Supported | Paint stroke/fill-and-stroke text queues stroked text geometry before fill text. |
| Text shadow | Supported | Paint shadow layer queues text shadow passes for geometry text. |
| Glyph availability query | Contract supported | Basic backend reports ASCII availability, registered font ranges, native font-family paths, and rasterizer-backed glyph coverage. |
| Diagnostics hook | Contract supported | Backend diagnostics report rejected font/fallback registration events. |
| Fallback range query | Contract supported | Font faces can declare codepoint ranges; glyph availability resolves primary and fallback families. |
| Missing glyph diagnostics | Contract supported | Missing non-ASCII glyph queries add coalesced diagnostics with codepoint and requested family. |
| Missing glyph render hooks | Contract supported | Geometry fallback render results expose missing glyph codepoints and source ranges. |

## Planned Backend Work

| Capability | Status | Intended Direction |
| --- | --- | --- |
| Persistent GPU atlas resource | Planned | Keep the atlas texture on the renderer side and update dirty rects instead of uploading an RGBA atlas snapshot per text draw. |
| Complex shaping backend | Planned | Add shaping for scripts and font features that need glyph substitution or reordering before rasterization. |
| Atlas-aware text blur | Planned | Prefer atlas-aware blur once persistent atlas rendering owns glyph texture updates. |

## Acceptance Targets

- Text input must accept valid UTF-8 and fail predictably for invalid input.
- Measurement, layout, and rendering must agree on normalized text.
- Public layout APIs must expose enough row data for application-side UI layout.
- Fallback resolution must be deterministic and inspectable.
- Missing glyphs must eventually be diagnosable without requiring a draw call.
- Atlas-backed rendering must survive context recreation through explicit rebuild hooks.

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
| Persistent GPU atlas resource | Supported | Canvas owns a reusable GPU atlas image resource and updates it when the CPU atlas content changes. |
| Dirty-rect atlas updates | Supported | Glyph uploads expose dirty rectangles; Canvas updates matching GPU atlas subregions when possible. |
| RGBA glyph atlas path | Contract supported | `GlyphAtlas`, text render results, and Canvas atlas upload can carry RGBA glyph pixels; concrete color font format decoding is future backend work. |
| Shaped glyph run abstraction | Supported | Portable raster text uses shaped runs with source byte mapping, glyph indices, glyph advances, offsets, and letter spacing before atlas upload. |
| Glyph-index rasterization path | Supported | Font rasterization can render by glyph index, which is required by real shaping outputs. |
| Basic RTL run ordering | Supported | The built-in shaper detects RTL-first runs and emits glyphs in visual order while preserving source byte mapping. |
| Bidi run segmentation | Supported | Mixed-direction text is split into directional byte ranges before font segmentation and shaping; leading neutral text is retained, weak-only text defaults to LTR, and RTL-base paragraphs reverse visual run order. |
| OpenType shaping adapter boundary | Supported | `BasicTextBackendOptions` can request an OpenType shaping backend; unavailable adapters fall back to simple shaping with diagnostics. |
| Optional OpenType shaping implementation | Build-time supported | When HarfBuzz is found and `WHATSCANVAS_ENABLE_OPENTYPE_SHAPING=ON`, the OpenType backend emits glyph-index shaped runs from HarfBuzz output. |
| Multi-font shaping segmentation | Supported | Portable raster text is split by resolved font face before shaping, so fallback families can shape/render as independent runs. |
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
| Text shadow | Supported | Paint shadow layer queues text shadow passes for geometry, bitmap, and atlas text. |
| Atlas-aware text blur | Supported | Atlas text uses a dedicated multi-sample shadow path tuned for glyph texture quads. |
| Glyph availability query | Contract supported | Basic backend reports ASCII availability, registered font ranges, native font-family paths, and rasterizer-backed glyph coverage. |
| Diagnostics hook | Contract supported | Backend diagnostics report rejected font/fallback registration events. |
| Fallback range query | Contract supported | Font faces can declare codepoint ranges; glyph availability resolves primary and fallback families. |
| Missing glyph diagnostics | Contract supported | Missing non-ASCII glyph queries add coalesced diagnostics with codepoint and requested family. |
| Missing glyph render hooks | Contract supported | Geometry fallback render results expose missing glyph codepoints and source ranges. |

## Planned Backend Work

| Capability | Status | Intended Direction |
| --- | --- | --- |
| Full Unicode bidi algorithm | Planned | Expand directional handling to the full UAX #9 rule set, including embedding levels, isolates, mirroring, and neutral resolution. |
| Color font format decoding | Planned | Add COLR/CPAL, CBDT/CBLC, SBIX, and SVG glyph extraction that can feed the RGBA glyph atlas path. |

## Acceptance Targets

- Text input must accept valid UTF-8 and fail predictably for invalid input.
- Measurement, layout, and rendering must agree on normalized text.
- Public layout APIs must expose enough row data for application-side UI layout.
- Fallback resolution must be deterministic and inspectable.
- Missing glyphs must eventually be diagnosable without requiring a draw call.
- Atlas-backed rendering must survive context recreation through explicit rebuild hooks.

# WhatsCanvas Text Feature Matrix

This matrix defines the production text surface for WhatsCanvas. It separates what is already supported, what has a stable contract, and what still needs a rendering backend implementation.

## Current Support

| Capability | Status | Notes |
| --- | --- | --- |
| UTF-8 input validation | Supported | Invalid byte sequences are normalized before measurement/render paths; multiline layout accepts LF and CRLF row separators. |
| ASCII fallback geometry | Supported | Basic backend can render normalized text through ASCII fallback geometry. |
| Native Windows text bitmap path | Supported | Used when a font family is supplied and native measurement/render succeeds. |
| Cross-platform font rasterization | Supported | Registered file-backed or memory-backed TrueType faces can be rasterized through the portable font rasterizer; FreeType is used when available and `stb_truetype` remains the dependency-free fallback. |
| Optional FreeType rasterizer | Build-time supported | When FreeType is found and `WHATSCANVAS_ENABLE_FREETYPE_RASTERIZER=ON`, glyph lookup, metrics, kerning, and alpha glyph rasterization use FreeType. |
| Atlas-backed glyph rendering | Supported | Rasterized glyphs are packed into `GlyphAtlas`; Canvas submits atlas quads through the image path. |
| Indexed glyph lookup | Supported | `GlyphAtlas` maintains a hash index from glyph key to entry slot, avoiding linear scans on repeated glyph uploads in longer text runs. |
| Persistent GPU atlas resource | Supported | Canvas owns a reusable GPU atlas image resource and updates it when the CPU atlas content changes. |
| Dirty-rect atlas updates | Supported | Glyph uploads expose dirty rectangles; Canvas updates matching GPU atlas subregions when possible. |
| RGBA glyph atlas path | Supported | `GlyphAtlas`, text render results, and Canvas atlas upload can carry RGBA glyph pixels for color font layers and alpha-derived glyphs. |
| Color font table detection | Contract supported | Font rasterizer utilities can detect COLR/CPAL, CBDT/CBLC, SBIX, and SVG OpenType tables as a backend capability probe before concrete glyph extraction. |
| COLR/CPAL v0 glyph decoding | Supported | Portable font rasterization can decode COLR/CPAL v0 layer records, rasterize each layer outline, composite palette colors into RGBA glyph bitmaps, and upload them through the atlas path. |
| Shaped glyph run abstraction | Supported | Portable raster text uses shaped runs with source byte mapping, glyph indices, glyph advances, offsets, and letter spacing before atlas upload. |
| Glyph-index rasterization path | Supported | Font rasterization can render by glyph index, which is required by real shaping outputs. |
| Simple kerning | Supported | The portable simple shaping path applies registered-font glyph kerning pairs when OpenType shaping is not active. |
| Basic RTL run ordering | Supported | The built-in shaper detects RTL-first runs, mirrors common paired punctuation, and emits glyphs in visual order while preserving source byte mapping. |
| Unicode UAX #9 bidi resolution | Supported | Mixed-direction text is resolved through a dedicated bidi pass with paragraph direction, explicit embedding/override/isolate controls, weak type resolution, neutral resolution, implicit levels, invisible formatting controls, and visual run ordering. |
| OpenType shaping adapter boundary | Supported | `BasicTextBackendOptions` can request an OpenType shaping backend; unavailable adapters fall back to simple shaping with diagnostics. |
| Optional OpenType shaping implementation | Build-time supported | When HarfBuzz is found and `WHATSCANVAS_ENABLE_OPENTYPE_SHAPING=ON`, the OpenType backend emits glyph-index shaped runs from HarfBuzz output. |
| Multi-font shaping segmentation | Supported | Portable raster text is split by resolved font face before shaping, so fallback families can shape/render as independent runs. |
| Public font face model | Supported | `FontFace`, `FontDescriptor`, `FontFallbackChain`, and `FontManager` are public value/model types. |
| Font file registration contract | Supported | `ITextBackend::registerFontFace` accepts file-backed faces. |
| Font memory registration contract | Supported | `ITextBackend::registerFontFace` accepts memory-backed faces. |
| TrueType Collection face selection | Supported | `FontFace::fromFile` and `FontFace::fromMemory` accept a collection face index; portable rasterization, FreeType loading, `stb_truetype` loading, cache keys, and color table detection honor the selected face. |
| Fallback chain contract | Supported | `ITextBackend::setFontFallbackChain` and `resolveFontFamilies` define resolution order. |
| Text metrics | Supported | `measureText`, `measureTextBounds`, `measureTextMetrics`, and backend metrics are available; registered font metrics use real ascent/descent/line-gap data. |
| Bounded multiline layout | Supported | `Canvas::layoutTextBox` returns line rows, source ranges, widths, line height, and ellipsis state; line breaking supports ASCII words, tab/Unicode space separators, zero-width break opportunities, long unspaced tokens, basic CJK no-space wrapping, common CJK punctuation attachment, and UTF-8-safe ellipsis trimming. |
| Text box rendering | Supported | `drawTextBox` uses the same layout path as `layoutTextBox`. |
| Letter spacing | Supported | Basic geometry and native bitmap paths apply letter spacing. |
| Alignment and baseline | Supported | Left/center/right and top/middle/bottom modes are exposed through `Paint`. |
| Text on path | Supported | Path placement iterates normalized UTF-8 glyph tokens and reuses the text backend for measurement and rendering, so registered-font atlas text can participate. |
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
| Additional color font formats | Planned | Add CBDT/CBLC, SBIX, SVG, and newer COLR paint graph extraction on top of the table detection and RGBA glyph atlas path. |

## Acceptance Targets

- Text input must accept valid UTF-8 and fail predictably for invalid input.
- Measurement, layout, and rendering must agree on normalized text.
- Public layout APIs must expose enough row data for application-side UI layout.
- Fallback resolution must be deterministic and inspectable.
- Missing glyphs must eventually be diagnosable without requiring a draw call.
- Atlas-backed rendering must survive context recreation through explicit rebuild hooks.

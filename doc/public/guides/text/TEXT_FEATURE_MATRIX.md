# WhatsCanvas Text Feature Matrix

This matrix defines the production text surface for WhatsCanvas. The stable v1
text target is complete. The final section lists optional format expansions;
those entries do not represent missing requirements for the current product.

## Current Support

| Capability | Status | Notes |
| --- | --- | --- |
| UTF-8 input validation | Supported | Invalid byte sequences are normalized before measurement/render paths; multiline layout accepts LF and CRLF row separators. |
| ASCII fallback geometry | Supported | Basic backend can render normalized text through ASCII fallback geometry. |
| Native Windows text bitmap path | Supported | Used when a font family is supplied and native measurement/render succeeds. |
| Native Apple CoreText bitmap path | Supported | Apple builds use CoreText for measurement, typesetter line breaking, system fallback, file/memory registration, OpenType features and variation axes, decoration, and bounded grayscale RGBA bitmap caching. |
| Cross-platform font rasterization | Supported | Registered file-backed or memory-backed TrueType faces can be rasterized through the portable font rasterizer; immutable shared-memory sources let platform providers materialize fonts without retaining a stable path. FreeType is used when available and `stb_truetype` remains the dependency-free fallback. |
| FreeType rasterizer | Default for GL-family targets | When FreeType is found and `WHATSCANVAS_ENABLE_FREETYPE_RASTERIZER=ON` (default), glyph lookup, metrics, kerning, and alpha glyph rasterization use FreeType. The Software-only target retains `stb_truetype`. |
| Font rasterizer cache policy | Supported | Loaded font faces are bounded by a mutex-protected LRU cache with explicit capacity control, cache clearing, and hit/miss/eviction stats. |
| Atlas-backed glyph rendering | Supported | Rasterized glyphs are packed into `GlyphAtlas`; Canvas submits atlas quads through the image path. |
| Indexed glyph lookup | Supported | `GlyphAtlas` maintains a hash index from glyph key to entry slot, avoiding linear scans on repeated glyph uploads in longer text runs. Keys include stable file/memory font-source identity and collection face index, so different font sources cannot alias merely because their family/style metadata matches. |
| Persistent GPU atlas resource | Supported | Canvas owns a reusable GPU atlas image resource and updates it when the CPU atlas content changes. |
| Dirty-rect atlas updates | Supported | Glyph uploads expose dirty rectangles; Canvas updates matching GPU atlas subregions when possible. |
| Dirty-rect collapse | Supported | Glyph uploads keep precise dirty rectangles for short updates and collapse to one full-atlas dirty rectangle when a long text run would exceed the per-frame dirty-rect count or dirty-area budget; collapse events are exposed through atlas stats. |
| RGBA glyph atlas path | Supported | `GlyphAtlas`, text render results, and Canvas atlas upload can carry RGBA glyph pixels for color font layers and alpha-derived glyphs. |
| Color font table detection | Contract supported | Font rasterizer utilities can detect COLR/CPAL, CBDT/CBLC, SBIX, and SVG OpenType tables as a backend capability probe before concrete glyph extraction. |
| COLR/CPAL v0 glyph decoding | Supported | Portable font rasterization can decode COLR/CPAL v0 layer records, rasterize each layer outline, composite palette colors into RGBA glyph bitmaps, and upload them through the atlas path. |
| CBDT/CBLC bitmap glyph decoding | Supported for the v1 target | Portable font rasterization supports the CBDT/CBLC 2.0 and 3.0 forms used by the validated Android environments: CBLC index format 1 with CBDT image format 17 PNG records, including strike selection, metrics, scaling, RGBA atlas upload, and a deterministic bundled-font contract. Additional formats are optional compatibility extensions. |
| Shaped glyph run abstraction | Supported | Portable raster text uses shaped runs with source byte mapping, glyph indices, glyph advances, offsets, and letter spacing before atlas upload. |
| Glyph-index rasterization path | Supported | Font rasterization can render by glyph index, which is required by real shaping outputs. |
| Simple kerning | Supported | The portable simple shaping path applies registered-font glyph kerning pairs when OpenType shaping is not active. |
| Basic RTL run ordering | Supported | The built-in shaper detects RTL-first runs, mirrors common paired punctuation, and emits glyphs in visual order while preserving source byte mapping. |
| Unicode UAX #9 bidi resolution | Supported | Mixed-direction text is resolved through a dedicated bidi pass with paragraph direction, explicit embedding/override/isolate controls, weak type resolution, neutral resolution, implicit levels, invisible formatting controls, and visual run ordering. |
| OpenType shaping adapter boundary | Supported | `BasicTextBackendOptions` can request an OpenType shaping backend; unavailable adapters fall back to simple shaping with diagnostics. |
| OpenType shaping implementation | Default for GL-family targets | When HarfBuzz is found and `WHATSCANVAS_ENABLE_OPENTYPE_SHAPING=ON` (default), the OpenType backend emits glyph-index shaped runs from HarfBuzz output. The Software-only target retains simple shaping. |
| Android complex emoji shaping | Supported | The Android project requires HarfBuzz and FreeType. Full UTF-16 clusters plus Unicode emoji-presentation intent drive system matching; ZWJ/skin-tone, regional-flag, keycap, and tag sequences remain indivisible and can use GSUB color glyphs. |
| Locale/direction shaping input | Supported | Portable HarfBuzz shaping receives `Paint::setTextLocale` plus the resolved bidi-run direction before script/property guessing. |
| OpenType feature control | Supported | `Paint::setFontFeature` applies global four-character OpenType features such as `liga`, `kern`, and `smcp` to portable HarfBuzz shaping and native DirectWrite typography. Feature settings participate in native bitmap cache identity. |
| Extended grapheme segmentation | Supported | Portable text uses generated Unicode property tables and UAX #29 GB1–GB999 rules, including Hangul, Prepend/SpacingMark, Indic conjuncts, emoji ZWJ sequences, and regional-indicator pairing. The generator is a maintainer tool; consumers do not need Python or Unicode data files at runtime. |
| Multi-font shaping segmentation | Supported | Portable raster text resolves one face per extended grapheme cluster before shaping, so a user-perceived character is not split between fallback faces. |
| Public font face model | Supported | `FontFace`, `FontDescriptor`, `FontFallbackChain`, and `FontManager` are public value/model types. |
| Font file registration contract | Supported | `ITextBackend::registerFontFace` accepts file-backed faces. |
| Font memory registration contract | Supported | `ITextBackend::registerFontFace` accepts memory-backed faces. |
| Lazy application font providers | Portable and DirectWrite supported | `LazyFontProvider` records asset/dynamic source metadata without loading bytes, memoizes successful and failed first-match loads, supports family-scoped retry/removal, and attaches through `Canvas::addFontProvider`. DirectWrite lazily bridges the winning family into a generation-tracked custom collection. |
| Asynchronous remote font providers | Core and portable backend supported | `RemoteFontProvider` exposes a host-driven `IDLE -> QUEUED -> DOWNLOADING -> LOADED/PERMANENT_FAILURE` lifecycle, coverage-aware deterministic subset selection, request deduplication, concurrency/retry limits, cumulative transfer budgets, optional content fingerprints, stale-callback tokens, family-scoped cache invalidation, and deduplicated `takeChangedFamilies()` notification draining. Browser fetch/FontFaceSet glue and frame scheduling remain platform-host work. |
| TrueType Collection face selection | Supported | `FontFace::fromFile` and `FontFace::fromMemory` accept a collection face index; HarfBuzz shaping, portable rasterization, FreeType/stb loading, atlas/cache keys, CoreText/DirectWrite/fontconfig discovery, and color table detection honor the selected face. |
| Fallback chain contract | Supported | `ITextBackend::setFontFallbackChain` and `resolveFontFamilies` define resolution order. |
| Text metrics | Supported | `measureText`, `measureTextBounds`, `measureTextMetrics`, and backend metrics are available; registered font metrics use real ascent/descent/line-gap data. |
| Bounded multiline layout | Supported | `Canvas::layoutTextBox` returns line rows, source ranges, widths, line height, and ellipsis state; line breaking supports ASCII words, tab/Unicode space separators, zero-width break opportunities, long unspaced tokens, basic CJK no-space wrapping, common CJK punctuation attachment, and UTF-8-safe ellipsis trimming. Emergency wrapping is UAX #29 cluster-safe; the broader break-opportunity policy is intentionally a compact UAX #14 subset. |
| Installed-font refresh | Supported | `FontSystem::refreshInstalledFonts` publishes a process-wide snapshot with a monotonic generation, while `Canvas::refreshSystemFonts` rebuilds system aliases/caches without discarding application-registered faces or fallback chains. DirectWrite refreshes its native system collection as well. |
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
| Raster text fallback diagnostics | Contract supported | Raster shaping, face resolution, glyph rasterization, atlas upload, and atlas retry failures add coalesced diagnostics before falling back to alternate text rendering. |
| Missing glyph render hooks | Contract supported | Geometry fallback render results expose missing glyph codepoints and source ranges. |

## OpenType Feature Controls

`Paint::setFontFeature(tag, value)` does not use a fixed feature whitelist. It
accepts any case-sensitive OpenType tag of exactly four bytes and applies the
override to the complete text run. Portable HarfBuzz shaping and native
DirectWrite and CoreText consume the setting; the dependency-free simple shaper ignores it.
The selected font must contain and use the feature for output to change.

Use value `0` to explicitly disable a feature, `1` to enable its usual form,
and another unsigned value only when the feature defines alternate indices.
Calling `setFontFeature` again with the same tag updates it. Calling
`clearFontFeatures()` removes all overrides and restores font/shaper defaults,
which is different from keeping a feature explicitly disabled with value `0`.

```cpp
wsc::Paint body;
body.setFontFeature("liga", 0); // Explicitly disable standard ligatures.
body.setFontFeature("tnum", 1); // Request tabular-width numerals.

wsc::Paint defaults = body;
defaults.clearFontFeatures();   // Restore the font/shaper defaults.
```

Common standardized tags include:

| Category | Tags | Typical use |
| --- | --- | --- |
| Ligatures and context | `liga`, `clig`, `dlig`, `calt` | Standard/contextual/discretionary substitutions. |
| Spacing and marks | `kern`, `mark`, `mkmk` | Pair kerning and combining-mark positioning. |
| Capitals | `smcp`, `c2sc`, `case` | Small capitals and case-sensitive forms. |
| Numeral style | `lnum`, `onum`, `pnum`, `tnum` | Lining/old-style and proportional/tabular numerals. |
| Fractions | `frac`, `numr`, `dnom` | Fractions, numerators, and denominators. |
| Font-specific alternates | `ss01`–`ss20`, `cv01`–`cv99` | Stylistic sets and character variants, when supplied by the font. |
| Vertical forms | `vert`, `vrt2` | Vertical substitutions; normally ineffective in the current horizontal layout. |

This table is illustrative rather than exhaustive. Unknown four-byte tags are
still forwarded and normally have no effect.

## Optional Format Extensions

| Capability | Status | Intended Direction |
| --- | --- | --- |
| Additional color font formats | Optional | Add more CBDT/CBLC index/image formats, SBIX, SVG, and advanced COLR paint/composite extraction if a target application requires them. The existing RGBA glyph atlas path already satisfies the stable v1 text target. |

## Acceptance Targets

- Text input must accept valid UTF-8 and fail predictably for invalid input.
- Measurement, layout, and rendering must agree on normalized text.
- Public layout APIs must expose enough row data for application-side UI layout.
- Fallback resolution must be deterministic and inspectable.
- Missing glyphs must eventually be diagnosable without requiring a draw call.
- Atlas-backed rendering must survive context recreation through explicit rebuild hooks.

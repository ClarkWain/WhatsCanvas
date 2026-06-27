# WhatsCanvas Text Feature Matrix

This matrix defines the production text surface for WhatsCanvas. It separates what is already supported, what has a stable contract, and what still needs a rendering backend implementation.

## Current Support

| Capability | Status | Notes |
| --- | --- | --- |
| UTF-8 input validation | Supported | Invalid byte sequences are normalized before measurement/render paths. |
| ASCII fallback geometry | Supported | Basic backend can render normalized text through ASCII fallback geometry. |
| Native Windows text bitmap path | Supported | Used when a font family is supplied and native measurement/render succeeds. |
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
| Glyph availability query | Contract supported | Basic backend reports ASCII availability and treats native font-family paths as renderable. |
| Diagnostics hook | Contract supported | Backend diagnostics report rejected font/fallback registration events. |

## Planned Backend Work

| Capability | Status | Intended Direction |
| --- | --- | --- |
| Cross-platform font rasterization | Planned | Add a backend that works consistently on desktop and mobile. |
| Glyph atlas ownership | Planned | Own atlas allocation, glyph upload, eviction, and rebuild hooks inside the text subsystem. |
| Emoji fallback | Planned | Resolve emoji families through fallback chain and report missing glyphs. |
| Missing glyph diagnostics | Partial | Contract exists; richer per-codepoint diagnostics belong in the cross-platform backend. |
| Stroke text | Planned | Prefer glyph outline or distance-field path once atlas backend exists. |
| Text blur/shadow | Planned | Prefer paint-level effect pass or atlas-aware blur, avoiding special-case CPU bitmaps. |

## Acceptance Targets

- Text input must accept valid UTF-8 and fail predictably for invalid input.
- Measurement, layout, and rendering must agree on normalized text.
- Public layout APIs must expose enough row data for application-side UI layout.
- Fallback resolution must be deterministic and inspectable.
- Missing glyphs must eventually be diagnosable without requiring a draw call.
- Atlas-backed rendering must survive context recreation through explicit rebuild hooks.

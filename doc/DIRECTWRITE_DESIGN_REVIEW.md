# DirectWrite Support Design Review

Status: July 13, 2026 (original review) — updated after PRs #27–#44.

Scope: this note reviews the current DirectWrite text backend as it exists in the workspace today. It focuses on architectural readiness, rendering behavior, and likely performance characteristics.

## Status Update (post PRs #27–#44)

All five issues below have been addressed. The DirectWrite backend is now the recommended Windows text path.

| # | Issue | Status | Landed in |
|---|-------|--------|-----------|
| 1 | Not publicly integrated | **Resolved** — `Canvas::TextBackend::DirectWrite` is a public option. The portable backend remains the default; DirectWrite is selected explicitly on Windows. | PR #27 (backend) + subsequent public-surface changes |
| 2 | Bitmap path too heavy for repeated UI text | **Resolved** — three-layer cache: cached DirectWrite/D2D/WIC factories (PR #37), cached bitmap pixels + intrinsic metrics (PR #39, 4 MB LRU by default), cached GPU texture (PR #43, 256-entry LRU keyed by stable content id). Only a shared glyph atlas that batches distinct text in one frame remains as future work. |
| 3 | Backend contract not aligned with portable path | **Resolved** — `registerFontFace`, `setFontFallbackChain`, `resolveFontFamilies` all work and match portable semantics (PR #38 + cross-platform matrix update). |
| 4 | Layout fidelity gaps (breakLines, letter spacing) | **Resolved** — real DirectWrite line breaking via `IDWriteTextLayout` (PR #34); character spacing applied through `IDWriteTextLayout1::SetCharacterSpacing`. |
| 5 | ClearType safety | **Resolved** — per-`Paint` `TextRenderMode` (PR #41) makes ClearType an explicit, per-draw opt-in with cached raster mode; ADR-003 documents the opaque-surface / axis-aligned safety policy. |

The remainder of this file is preserved as the original historical review for
traceability. Its issue descriptions and recommendations are superseded by the
status table above; use [`DIRECTWRITE_TEXT_BACKEND.md`](DIRECTWRITE_TEXT_BACKEND.md)
for the current contract.

## Historical Executive Summary (superseded)

WhatsCanvas now has a real DirectWrite backend implementation, not just an enum placeholder.

At the time of the original review, it was treated as an experimental backend.
The following issue list is retained only to explain the work that led to the
current implementation:

1. DirectWrite is not part of the public Canvas configuration surface.
2. The rendering path is bitmap-based and expensive per draw.
3. Backend capability contracts do not yet match the portable text path.
4. Some layout semantics are still approximated outside DirectWrite.
5. ClearType safety rules are documented but not enforced by the framework.

## What Already Looks Sound

- `ITextBackend` is the right abstraction seam for platform-native text backends.
- The DirectWrite backend can construct, measure text, and render bitmap output on Windows.
- Measurement and rasterization both flow through DirectWrite layout objects instead of ad-hoc GDI helpers.
- Grayscale and ClearType raster modes are separated explicitly, which is the right high-level direction.

## Historical Main Issues (superseded)

### 1. DirectWrite Is Not Publicly Integrated Yet

- `Canvas` still constructs the default basic text backend directly.
- The default backend mode remains `Auto`.
- The real DirectWrite backend is only created when internal text options explicitly request it.

Why this matters:

DirectWrite currently behaves like an internal implementation option, not a stable public feature that package consumers can select through the main Canvas API.

### 2. The Rendering Path Is Too Heavy for Repeated UI Text

- The DirectWrite backend returns `Bitmap` text output, not glyph-atlas output.
- Each render call initializes COM state and creates WIC/D2D objects needed to paint into a bitmap.
- `Canvas::drawText()` then uploads that bitmap as a fresh GPU image resource.

Why this matters:

This turns text drawing into repeated CPU rasterization plus GPU texture upload, which is likely too expensive for scrolling UI, animated labels, chat logs, or any scene with frequent text reuse.

### 3. The Backend Contract Is Not Yet Aligned with the Portable Path

- The capability matrix advertises DirectWrite as an available native adapter and marks font registration as supported.
- The DirectWrite backend still returns `false` for `registerFontFace()` and `setFontFallbackChain()`.
- `resolveFontFamilies()` does not mirror the portable backend's fallback resolution behavior.

Why this matters:

Switching to DirectWrite currently changes more than the raster engine. It also changes font registration and fallback semantics, which makes backend swapping non-transparent.

### 4. Layout Fidelity Still Has Known Gaps

- `breakLines()` still performs greedy substring measurement rather than using DirectWrite line-layout output directly.
- Letter spacing is added to measured width and output bounds, but it is not yet applied as real DirectWrite character spacing inside the text layout.

Why this matters:

This can produce mismatches between measured bounds, alignment, wrapping, and the actual rendered glyph bitmap. It also adds avoidable measurement cost on long strings.

### 5. ClearType Usage Is Not Framework-Safe Yet

- The backend correctly documents ClearType as safe only for axis-aligned text over a known opaque background.
- After rasterization, the result still flows through the generic bitmap/image text path in `Canvas`.
- That means transforms, clip masks, gradients, shadows, and alpha-composited usage are still possible unless callers self-police.

Why this matters:

ClearType should be a surface-aware rendering policy, not only a backend option. Without framework-level restrictions, it is easy to use it in unsafe contexts and get visible color fringes.

## Historical Recommended Order Before Calling DirectWrite "Supported"

1. Expose a stable public way to select the DirectWrite backend.
2. Decide whether DirectWrite stays bitmap-based or gains a cache/atlas strategy for reused text.
3. Bring font registration, fallback chains, and family resolution to parity with the portable backend.
4. Move line breaking and character spacing fully into the DirectWrite layout pipeline.
5. Gate ClearType behind an explicit opaque-surface or axis-aligned safety policy.

## Historical Short Recommendation

Do not present DirectWrite as a production-ready text backend yet.

The architecture now has a real adapter, which is meaningful progress. But public integration, performance, backend contract parity, and ClearType safety still need to be finished before it should be treated as a fully supported path.

## Evidence Files

- `src/canvas/Canvas.cpp`
- `src/text/BasicTextBackend.h`
- `src/text/BasicTextBackend.cpp`
- `src/text/DirectWriteTextBackend.h`
- `src/text/DirectWriteTextBackend.cpp`
- `tests/DirectWriteBackendTests.cpp`
- `tests/TextBackendContractTests.cpp`

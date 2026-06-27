# WhatsCanvas Core Capability TODO

This TODO tracks public, product-level capability growth for WhatsCanvas. It is written as an internal execution checklist for making the engine stronger, more portable, and easier to adopt in real applications.

## Principles

- Keep the public story focused on WhatsCanvas as an independent 2D canvas engine.
- Preserve stable public APIs where possible; add adapters or new extension points before forcing call-site churn.
- Prefer backend-neutral model work first, then OpenGL/OpenGLES implementation work.
- Treat text, image resources, rendering correctness, and platform builds as first-class product capabilities.
- Every completed item should leave behind either tests, smoke coverage, examples, or documented validation notes.

## Status Legend

- `[ ]` Not started
- `[~]` In progress
- `[x]` Completed
- `[!]` Blocked or needs design decision

## Phase 1: Text And Font Foundation

- [ ] Define the production text feature matrix.
  - Unicode input coverage
  - UTF-8 validation behavior
  - font file loading
  - font memory loading
  - font family lookup
  - fallback chains
  - emoji fallback
  - text metrics
  - line breaking
  - glyph-missing diagnostics
- [x] Add public font management types.
  - `FontManager`
  - `FontCollection`
  - `FontFace`
  - `FontFallbackChain`
  - stable public handles or value objects
- [ ] Extend `ITextBackend` beyond the current minimal measurement/render surface.
  - font registration
  - fallback resolution
  - metrics query
  - line break query
  - glyph availability query
  - diagnostics hook
- [x] Replace ASCII-only sanitization with real UTF-8 processing.
- [ ] Add a cross-platform font backend implementation path.
  - Prefer a backend that can run consistently on desktop and mobile.
  - Keep platform-native text as an optional acceleration or compatibility backend.
- [ ] Add glyph atlas ownership.
  - atlas allocation
  - glyph upload
  - cache eviction
  - context-loss rebuild hooks
- [ ] Add text layout helpers.
  - single-line measurement
  - bounded multiline measurement
  - line rows with text ranges and widths
  - max-line clipping
  - ellipsis
  - alignment and baseline modes
- [ ] Add text rendering features.
  - fill text
  - letter spacing
  - line height
  - optional blur/shadow path
  - optional stroke text path
- [ ] Add text regression tests.
  - ASCII
  - Chinese
  - mixed Latin/CJK
  - emoji fallback
  - missing glyph reporting
  - multiline wrapping
  - alignment and baseline

## Phase 2: Image And Texture Resources

- [x] Define a stable image resource model.
  - loaded image
  - render-target canvas
  - externally supplied texture
  - memory-backed RGBA image
  - decoded image data
- [x] Add public or semi-public external texture wrapping.
  - native texture id input
  - width and height
  - ownership policy
  - format metadata
  - lifecycle invalidation
- [x] Add image loading from memory.
  - encoded bytes
  - raw RGBA bytes
  - optional mipmap generation
- [x] Add image replacement/update APIs.
  - [x] replace full pixels
  - [x] replace sub-rect pixels
  - [x] preserve dimensions where possible
- [x] Improve image draw coverage.
  - [x] source-rect drawing
  - [x] destination-rect drawing
  - [x] tint color
  - [x] color matrix
  - [x] sampling mode
  - [x] tile mode
  - [x] contain/cover/fill fit
  - [x] anchors
  - [x] nine-patch
- [x] Add rounded image helpers.
  - [x] uniform corner radius
  - [x] per-corner radius
  - [x] circular crop helper
- [x] Add image pattern support or document the intended equivalent.
  - [x] transform
  - [x] repeat/mirror/clamp/decal
  - [x] alpha
  - [x] source image reference
- [ ] Add image resource tests.
  - file decode
  - memory decode
  - raw RGBA upload
  - external texture wrapping
  - [x] replacement/update smoke coverage
  - [x] source-rect crop
  - [x] tint and color matrix
  - [x] tiled image rendering

## Phase 3: Paint, Effects, And Gradients

- [ ] Move gradient execution closer to shader-level evaluation where needed.
  - linear gradients
  - radial gradients
  - multi-stop gradients
  - tile modes
- [ ] Add box gradient or equivalent shadow-oriented primitive.
- [ ] Clarify shadow model.
  - paint-level shadow
  - text shadow
  - shape shadow
  - blur radius behavior
- [ ] Complete stroke style coverage.
  - cap
  - join
  - miter limit
  - dash
  - path effects
- [ ] Audit blend mode behavior on GL-family backends.
  - non-premultiplied output
  - premultiplied option
  - Porter-Duff modes
  - additive/multiply/screen
- [ ] Add effect regression tests.
  - gradients
  - shadows
  - blend modes
  - strokes
  - dashes

## Phase 4: OpenGLES And Mobile Readiness

- [x] Split desktop OpenGL and OpenGLES build assumptions.
  - avoid desktop-only `OpenGL::GL` requirements in OpenGLES-only builds
  - make GLES include/link behavior explicit
  - document loader expectations
- [ ] Validate shader portability.
  - GLES 3.0 shader version
  - precision qualifiers
  - unavailable desktop-only states
  - extension-sensitive paths
- [ ] Add Android-oriented build smoke target.
  - CMake configure
  - static library or shared library build
  - minimal GLES link check
- [ ] Add iOS-oriented build notes or smoke target.
- [ ] Add context lifecycle APIs and tests.
  - initialize
  - finalize
  - resource release before context destruction
  - resource rebuild after context recreation
- [ ] Add mobile GPU resource diagnostics.
  - texture count
  - FBO count
  - atlas usage
  - command count
  - draw call count

## Phase 5: Compatibility Adapters

- [ ] Design a stateful canvas adapter layer.
  - current fill paint
  - current stroke paint
  - current path
  - current font state
  - current alpha and blend state
  - image handle table
- [ ] Provide helper APIs for applications that prefer immediate-mode drawing.
  - begin path
  - fill current path
  - stroke current path
  - fill rect
  - stroke rect
  - draw image by handle
  - draw text with current font state
- [ ] Keep adapters separate from the core `wsc::Canvas` model.
- [ ] Add adapter tests using public sample scenes only.

## Phase 6: Validation And Public Examples

- [x] Create a core visual scene suite.
  - [x] text-heavy scene
  - [x] image-heavy scene
  - [x] gradient/effect scene
  - [x] clipping scene
  - [x] transform scene
  - [x] saveLayer scene
- [x] Add pixel hash gates where stable enough.
- [ ] Add fuzzy visual comparison for driver-sensitive scenes.
- [ ] Add API-level unit tests.
  - text layout
  - image resource lifecycle
  - paint conversion
  - matrix and clipping
- [ ] Add performance benchmarks.
  - text layout cost
  - glyph cache hit rate
  - image upload cost
  - draw call count
  - frame flush cost
- [ ] Update public examples as features land.
  - font fallback demo
  - multiline text demo
  - external texture demo
  - image pattern demo
  - mobile GLES smoke demo

## First Execution Slice

1. [x] Replace ASCII-only text handling with UTF-8 aware text plumbing.
2. [x] Add font registration and fallback design to the public text subsystem.
3. [x] Add image memory loading and external texture wrapping design.
4. [x] Clean up OpenGLES CMake assumptions so mobile builds do not inherit desktop-only requirements.
5. [x] Add a text-heavy and image-heavy validation scene.

## Definition Of Done

- The capability has a public or clearly internal API boundary.
- The implementation does not leak backend-specific types through general public APIs unless explicitly designed as an escape hatch.
- Desktop OpenGL builds remain green.
- OpenGLES builds remain configurable.
- Tests or smoke scenes cover the new behavior.
- README or architecture docs are updated when public usage changes.

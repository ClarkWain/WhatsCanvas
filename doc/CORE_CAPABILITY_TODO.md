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

- [x] Define the production text feature matrix.
  - [x] Unicode input coverage
  - [x] UTF-8 validation behavior
  - [x] font file loading
  - [x] font memory loading
  - [x] font family lookup
  - [x] fallback chains
  - [x] emoji fallback
  - [x] text metrics
  - [x] line breaking
  - [x] glyph-missing diagnostics
- [x] Add public font management types.
  - `FontManager`
  - `FontCollection`
  - `FontFace`
  - `FontFallbackChain`
  - stable public handles or value objects
- [x] Extend `ITextBackend` beyond the current minimal measurement/render surface.
  - [x] font registration
  - [x] fallback resolution
  - [x] metrics query
  - [x] line break query
  - [x] glyph availability query
  - [x] diagnostics hook
- [x] Replace ASCII-only sanitization with real UTF-8 processing.
- [x] Add a cross-platform font backend implementation path.
  - Prefer a backend that can run consistently on desktop and mobile.
  - Keep platform-native text as an optional acceleration or compatibility backend.
- [x] Add glyph atlas ownership.
  - [x] atlas allocation
  - [x] glyph upload
  - [x] cache eviction
  - [x] context-loss rebuild hooks
- [x] Add text layout helpers.
  - [x] single-line measurement
  - [x] bounded multiline measurement
  - [x] line rows with text ranges and widths
  - [x] max-line clipping
  - [x] ellipsis
  - [x] alignment and baseline modes
- [x] Add text rendering features.
  - [x] fill text
  - [x] letter spacing
  - [x] line height
  - [x] optional blur/shadow path
  - [x] optional stroke text path
- [x] Add text regression tests.
  - [x] ASCII
  - [x] Chinese
  - [x] mixed Latin/CJK
  - [x] emoji fallback
  - [x] missing glyph reporting
  - [x] multiline wrapping
  - [x] alignment and baseline

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
- [x] Add image resource tests.
  - [x] file decode
  - [x] memory decode
  - [x] raw RGBA upload
  - [x] external texture wrapping
  - [x] replacement/update smoke coverage
  - [x] source-rect crop
  - [x] tint and color matrix
  - [x] tiled image rendering

## Phase 3: Paint, Effects, And Gradients

- [x] Move gradient execution closer to shader-level evaluation where needed.
  - [x] linear gradients
  - [x] radial gradients
  - [x] multi-stop gradients
  - [x] tile modes
- [x] Add box gradient or equivalent shadow-oriented primitive.
- [x] Clarify shadow model.
  - [x] paint-level shadow
  - [x] text shadow
  - [x] shape shadow
  - [x] blur radius behavior
- [x] Complete stroke style coverage.
  - [x] cap
  - [x] join
  - [x] miter limit
  - [x] dash
  - [x] path effects
- [x] Audit blend mode behavior on GL-family backends.
  - [x] non-premultiplied output
  - [x] premultiplied option boundary
  - [x] Porter-Duff modes
  - [x] additive/multiply/screen
- [x] Add effect regression tests.
  - [x] gradients
  - [x] shadows
  - [x] blend modes
  - [x] strokes
  - [x] dashes

## Phase 4: OpenGLES And Mobile Readiness

- [x] Split desktop OpenGL and OpenGLES build assumptions.
  - avoid desktop-only `OpenGL::GL` requirements in OpenGLES-only builds
  - make GLES include/link behavior explicit
  - document loader expectations
- [x] Validate shader portability.
  - [x] GLES 3.0 shader version
  - [x] precision qualifiers
  - [x] unavailable desktop-only states
  - [x] extension-sensitive paths
- [x] Add Android-oriented build smoke target.
  - [x] CMake configure
  - [x] static library or shared library build
  - [x] minimal GLES link check
- [x] Add iOS-oriented build notes or smoke target.
- [x] Add context lifecycle APIs and tests.
  - [x] initialize
  - [x] finalize
  - [x] resource release before context destruction
  - [x] resource rebuild after context recreation
- [x] Add mobile GPU resource diagnostics.
  - [x] texture count
  - [x] FBO count
  - [x] atlas usage
  - [x] command count
  - [x] draw call count

## Phase 5: Compatibility Adapters

- [x] Design a stateful canvas adapter layer.
  - [x] current fill paint
  - [x] current stroke paint
  - [x] current path
  - [x] current font state
  - [x] current alpha and blend state
  - [x] image handle table
- [x] Provide helper APIs for applications that prefer immediate-mode drawing.
  - [x] begin path
  - [x] fill current path
  - [x] stroke current path
  - [x] fill rect
  - [x] stroke rect
  - [x] draw image by handle
  - [x] draw text with current font state
- [x] Keep adapters separate from the core `wsc::Canvas` model.
- [x] Add adapter tests using public sample scenes only.

## Phase 6: Validation And Public Examples

- [x] Create a core visual scene suite.
  - [x] text-heavy scene
  - [x] image-heavy scene
  - [x] gradient/effect scene
  - [x] clipping scene
  - [x] transform scene
  - [x] saveLayer scene
- [x] Add pixel hash gates where stable enough.
- [x] Add fuzzy visual comparison for driver-sensitive scenes.
- [x] Add API-level unit tests.
  - [x] text layout
  - [x] image resource lifecycle
  - [x] paint conversion
  - [x] diagnostics
  - [x] matrix and clipping
- [x] Add performance benchmarks.
  - [x] text layout cost
  - [x] text cache hit path
  - [x] image upload cost
  - [x] command recording cost
  - [x] frame flush cost
- [x] Update public examples as features land.
  - [x] font fallback demo
  - [x] multiline text demo
  - [x] external texture demo
  - [x] image pattern demo
  - [x] mobile GLES smoke demo

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

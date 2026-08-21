# Changelog

All notable changes to WhatsCanvas are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
For releases and downloadable artifacts, see the
[GitHub Releases](https://github.com/ClarkWain/WhatsCanvas/releases) page.

## [Unreleased]

## [0.8.0] - 2026-08-21

### Added
- Added an Emscripten/WebGL 2 host under `platforms/wasm`, including pinned
  toolchain bootstrap, build/serve scripts, a browser shell, and automated
  DPR, resize, background/foreground, context-restore, cold-reload, frame-rate,
  and screenshot checks.
- Added one shared visual-parity workflow for Android, iOS, Desktop, and Web.
  Four scenes across seven canonical viewport/DPR samples now produce 14
  captures per platform and 42 required pairwise platform comparisons.
- Added shared text, geometry, and compositing stress scenes covering CJK,
  emoji, bidi and fallback text; path fill/stroke, curves and transforms; and
  clipping, blend modes, shadows, filters, alpha, and save/restore behavior.
- Added release-oriented capture automation and troubleshooting guidance for
  Android emulators, iOS simulators, Desktop, and headless browsers.

### Changed
- Replaced the device-derived canonical viewport with a neutral, reciprocal
  `400 x 800` portrait / `800 x 400` landscape primary standard. Added named
  16:9 phone, 4:3 tablet and 16:10 desktop layout standards, while retaining
  the former Android measurements as a legacy-regression profile only.
- Consolidated native presentation samples under `examples/present`, moved
  compile/integration/visual harnesses under `tests`, and removed the Bubble
  Shooter sample while retaining the Tetris and Racer games.
- Moved platform hosts onto the same scene implementations, deterministic
  animation inputs, aspect-fit rules, and capture contract so new rendering
  scenarios can be added once and exercised consistently on every host.

### Fixed
- Fixed WebGL 2 path rendering by separating vertex and element-array streams,
  restoring the owning vertex array before sprite draws, and expanding glyph
  coverage textures where WebGL does not expose GLES texture swizzles.
- Fixed even-odd path triangulation around duplicated hole-bridge endpoints;
  valid holes no longer fall back to an incorrectly filled triangle fan.
- Fixed Android capture races by waiting for the requested scene and physical
  dimensions to finish their first native frame instead of accepting the
  system cold-start splash or a stale orientation.

## [0.7.0] - 2026-08-20

### Added
- Added a shared canonical viewport contract for Android, iOS and Desktop:
  `393 x 759` portrait and `786 x 377` landscape, aspect-fitted as one unit
  after platform safe-area handling.
- Added deterministic animation-time capture inputs, DPR-aware Desktop dumps,
  iOS capture metadata, and a standard-library visual parity tool with
  per-scene, per-region profiles, heat maps and required-platform matrices.
- Added high-DPR Desktop smoke coverage, regional iOS Metal/Software parity
  checks, semi-transparent blend coverage and a documented workflow for adding
  future validation scenes.

### Changed
- Android, iOS and Desktop feature-showcase hosts now render the same canonical
  scene geometry instead of independently reflowing cards for each window.
- Visual references now render the canonical logical window at explicit
  `DPR=3`, preserving mobile-density text, texture, shadow and raster-cache
  behavior before comparison normalization.

### Fixed
- Fixed macOS Retina viewport and capture sizing so framebuffer and logical
  coordinates no longer diverge or crop the showcase.
- Fixed Metal `SCREEN` blending for semi-transparent solid colors by matching
  the straight-RGB contract used by the OpenGL and Software backends.

## [0.6.0] - 2026-08-20

### Added
- Added a runnable UIKit host under `platforms/ios/` that renders through Metal
  and native CoreText without linking OpenGL ES. The feature scene aligns with
  the Android card matrix, targets 60 fps through `CADisplayLink`, supports
  portrait and landscape, and rebuilds GPU/text resources after backgrounding.
- Added a native CoreText text backend with system fallback, file- and
  memory-backed fonts, OpenType features and variations, CoreText line layout,
  decoration, and bounded glyph bitmap caching.
- Added iOS lifecycle UI tests, simulator/device screenshot diagnostics,
  CoreText and iOS demo pixel-parity tests, and a Metal public API contract
  suite. The 23 Metal tests run with Metal API Validation and exercise all 10
  required pipelines, 14 blend modes, 3 sampling filters, 4 address modes,
  supported image formats, filters, readback, lifecycle, and presentation.

### Changed
- Extended textured draw commands with vertex tinting so supported two-stop
  gradients can be applied across CoreText bitmap quads without expanding the
  Metal fragment uniform contract.
- Kept gradient endpoints in canvas space across OpenGL, Vulkan, and Software
  paths when device-pixel ratio is greater than one.

### Fixed
- Fixed iOS physical-device presentation, missing Metal shader resource
  bindings, stale textures retained across backend reinitialization, invalid
  texture update formats, and unchecked synchronous command-buffer failures.
- Fixed Gaussian text shadows and gradient-text behavior in the iOS demo while
  preserving stable rendering on A14 and iOS 26 simulator GPUs.
- Disabled the bundled HarfBuzz CoreText shaper where it conflicted with the
  native CoreText backend and macOS universal packaging.

## [0.5.0] - 2026-08-19

### Added
- Added `platforms/desktop/`, a portable desktop platform host built as
  `WhatsCanvasDesktopHost`. It exposes an `IScene` contract and a
  `SceneCatalog` registry designed to back the Android, iOS and Web hosts
  through a future `platforms/shared/scenes/` extraction, ships an OpenGL 3.3
  core-profile GLFW host and a dependency-free CPU `SoftwareRuntime`, and
  supports three run modes:
  `--scene=<name>` for interactive rendering (default GLFW window),
  `--dump-png=<path> --frames=N` for headless PPM dump, and
  `--benchmark --warmup=N --measured=M` for CPU/GPU frame-time percentiles and
  aggregated `Canvas::getRenderStats`. Bundled `feature_showcase` scene
  mirrors the Android host card-for-card (TEXT, PATH, CLIP, ARCS, TRANSFORM,
  SHADOW, IMAGE, MOTION) and reuses the retained-`Picture` + dynamic overlay
  split. Enabled by `WHATSCANVAS_BUILD_DESKTOP_PLATFORM` (ON by default when
  `WhatsCanvas::OpenGL` is available).
- Added `WhatsCanvasDesktopHostSmoke` CTest gate (label
  `smoke;desktop;platforms`) hooked into `cross-platform-validation.yml`, run
  through the Software backend so Windows, Linux, and macOS CI runners
  exercise the desktop host end-to-end without needing a display server or a
  GLFW window.

### Fixed
- Fixed a GLES linear/radial gradient regression on shapes drawn at large
  logical y-coordinates. `DrawPath`, `DrawImage`, and `DrawText` GLES shaders
  inherited `precision mediump float` from `GLShaderSource.h`, which cannot
  represent large canvas coordinates precisely enough for
  `dot(vLocalPos - uLinearStart, direction) / dot(direction, direction)`
  without catastrophic cancellation. Multi-stop and 2-color gradients on
  shapes placed near the bottom of a portrait Android layout collapsed to
  their first color stop on real Adreno hardware (verified regression on
  Redmi K30, Android 11, GLES 3). Each affected GLES shader now declares
  `precision highp float; precision highp int;` at the top of both vertex and
  fragment stages (mirroring the `GaussianBlurProgram` pattern) so the
  gradient math preserves precision on both real GLES devices and the CI
  llvmpipe runner while desktop `#version 330 core` output stays
  byte-identical.

## [0.4.0] - 2026-08-18

### Added
- Added backend-neutral retained `Picture` recording with per-Context compiled
  command caches, explicitly requested bounded raster caches, LRU accounting,
  and lifecycle-safe invalidation. New diagnostics expose cache hits, misses,
  memory, preparation stages, command allocation/reuse, uploads, and shader
  compilation costs.
- Added production-oriented font-provider resolution, lazy byte-backed fonts,
  Android API 29+ matcher snapshots that do not require stable file paths, and
  API 21-28 AOSP/OEM XML discovery. The regression corpus now includes complete
  AVD and Redmi configurations plus focused color-font fixtures.
- Added Unicode/fuzzing/sanitizer CI gates, Android Debug/Profile/Release
  validation, performance schema checks, shader warm-up statistics, and a
  debug-signed Android Profile demo APK on tagged releases.
- Added Chinese performance documentation and a deep-dive guide explaining why,
  how, and when to use retained Pictures.

### Changed
- Reworked the Android feature demo around a static retained/rasterized layer,
  dynamic overlays, VSYNC-driven `WHEN_DIRTY` rendering, refresh-rate hints,
  and Profile-native `-O2 -DNDEBUG` builds. On the validated Pixel 3 workload,
  steady-state rendering sustains the active 60 Hz display rate.
- Reduced steady-state CPU/GPU overhead through lazy common/full shader
  programs, smaller common variants, cached uniforms, image/path batching,
  pooled commands and staging buffers, path-cache admission control, and fast
  primary-face text lookup. Software rendering gained matching fast paths and
  additional regression coverage.
- Hardened orderly teardown and unexpected context-loss handling so compiled
  Picture commands, raster layers, atlases, programs, buffers, and temporary
  targets are recreated only for the owning replacement Context.

### Fixed
- Accepted the CBDT/CBLC 2.0 PNG color-emoji tables shipped by Android 12 on
  Pixel 3 in addition to 3.0 tables used by newer Android fonts.
- Normalized geometry-text fallback vertices after high-DPI raster scaling,
  preventing fallback question marks from being transformed twice during
  Picture playback.
- Preserved visible content across Android pause/resume and context recreation,
  while avoiding stale GPU-object reuse and synchronous full-scene rebuilds.
- Resolved GLES 3.0 instanced-draw entry points independently of desktop GLAD
  version buckets, preventing a null function call on Mesa EGL.

## [0.3.0] - 2026-08-15

### Added
- Added a runnable Android OpenGL ES host under `platforms/android/`, including
  a `GLSurfaceView` renderer, JNI ownership, lifecycle handling, density-aware
  input, Android system-font matching, and packaging for `armeabi-v7a`,
  `arm64-v8a`, and `x86_64`.
- Added an Android integration guide covering Gradle/NDK setup, native source
  integration, JNI and GL-thread rules, font fallback, lifecycle behavior,
  troubleshooting, and device validation.
- Established `platforms/` as the repository convention for Android and future
  iOS/Web hosts while keeping the cross-platform renderer in `src/` and
  `include/`.

### Changed
- Updated cross-platform documentation and validation guidance for the Android
  host and the future iOS integration path.
- Improved round-cap tessellation so the cap and stroke body share the same
  center split, preventing anti-aliased seams at their boundary.
- Updated OpenGL ES single-channel texture swizzles to set the R/G/B/A
  components individually for broader GLES driver compatibility.

### Fixed
- Fixed missing Android glyph output caused by incompatible single-channel
  texture swizzle setup.
- Fixed small gaps and dark seam artifacts in round caps and dashed strokes,
  including the affected Android rendering examples.

## [0.2.0] - 2026-08-13

### Added
- Added a Metal render backend for macOS / iOS, complementing the existing
  Software / OpenGL / Vulkan backends. The backend is selectable via
  `Canvas::create(Canvas::Backend::Metal, w, h)` and is enabled by default on
  Apple platforms (opt out with `-DWHATSCANVAS_ENABLE_METAL=OFF`).
  - Nine Metal render pipelines cover Solid, Textured, Gradient (linear +
    radial), Clip-fill, Clip-mask rasterisation, Mask multiplication, Blur
    (separable), Inner-shadow, and full-target Blit.
  - The full 14-mode Porter-Duff blend set is implemented per pipeline.
  - Image filters supported: Gaussian blur (separable, 31-tap), Inner shadow
    (4-pass), Color matrix, Rounded-corner mask, and post-blur saturation /
    brightness / contrast / grain modifiers.
  - Multi-clip path intersection uses ping-pong mask multiplication so
    stacked `clipPath` / `clipRect` calls correctly narrow the clipping
    region instead of unioning.
  - Windowed presentation via `ISwapchain` on top of `CAMetalLayer`
    `nextDrawable` + blit, wired through `Canvas::setOutputTarget` +
    `Canvas::present`. See `examples/present/` for a CAMetalLayer demo.
  - GPU frame timing (`beginGpuFrameTiming` / `endGpuFrameTiming` /
    `lastGpuFrameTimeNs`) is backed by `MTLCommandBuffer.GPUStartTime` /
    `GPUEndTime`.
  - Pipeline and sampler caches are pre-warmed at `initializeBackend`;
    large vertex and index uploads reuse pooled scratch `MTLBuffer`s.
  - Twenty-one dedicated Metal test targets cover the Vulkan-equivalent
    rendering surface, including a `MetalFilterPixelParityTests` gate that compares the Metal
    Blur output against the Software reference and a `MetalDrawListTests`
    gate that drives `MetalRenderDevice::executeDrawList` directly with
    hand-authored primitives.
  - Cross-backend performance benchmark on Apple M3 Pro (256×256, 128 rects
    × 200 frames) shows Metal 1.66 ms/frame vs OpenGL 2.45 ms/frame
    (~32% faster wall-clock, ~40% faster GPU-side).

### Changed
- Added cached native system-font discovery through CoreText, DirectWrite,
  and fontconfig, with explicit cache refresh APIs and stronger cross-platform
  fallback behavior.
- Updated examples to use `FontSystem` for portable text rendering and aligned
  the public documentation with the new Metal and font-discovery capabilities.
- Updated the release workflow to accept semantic-version tags beyond the
  `0.1.x` release line.

### Fixed
- Fixed collection-face discovery for `.ttc`, `.otc`, and `.dfont` resources
  on current macOS SDKs.
- Fixed missing direct standard-library includes in Metal tests so stricter
  toolchains do not depend on transitive headers.

## [0.1.20] - 2026-08-05

### Added
- Added `examples/hello_world/` — a minimal CMake project that mirrors the
  README's "60 seconds to draw the first frame" snippet verbatim, links only
  against `WhatsCanvasSoftware`, and writes `first.ppm` off-screen.
- Added a focused stroke-tessellation compatibility gate covering line caps,
  joins, miter limits, closed paths, duplicate points, degenerate input, a
  curated legacy-output fingerprint, and 300 deterministic robustness cases.

### Changed
- Replaced the vendored Polyline2D implementation with the internal
  `StrokeTessellator`, preserving the previous triangle output while removing
  an embedded third-party dependency.
- Extended package-manager dependency mode to consume the registry-provided
  `glad::glad` target instead of compiling the vendored GLAD source into
  WhatsCanvas.
- Added the explicit `WHATSCANVAS_X11=AUTO|ON|OFF` setting. Package builds can
  now disable X11 discovery deterministically, while `ON` requires X11 rather
  than silently changing the compiled feature set.

### Removed
- Removed the experimental `wsc::CanvasAdapter` helper class, the
  `wsc/CanvasAdapter.h` public header, and the `WhatsCanvasCanvasAdapterTests`
  unit gate. The adapter was an unfinished NanoVG-style facade that was never
  documented as a supported migration path; consumers should use `wsc::Canvas`,
  `wsc::Paint`, `wsc::Path`, and `wsc::Image` directly.

## [0.1.19] - 2026-08-03

### Added
- Added a visual API integration gallery covering software, OpenGL, OpenGL ES,
  and Vulkan setup patterns.
- Added a repository-provided vcpkg overlay port with independently selectable
  OpenGL, Software, and FreeType/HarfBuzz text features.

### Changed
- Enabled the bundled FreeType rasterizer and HarfBuzz OpenType shaping by
  default for OpenGL-family builds and release packages. Both remain
  configurable, while the dependency-free Software target continues to use
  its built-in stb rasterizer and simple text shaping.
- Added a package-manager dependency mode that consumes registry-provided glm,
  stb, FreeType, and HarfBuzz instead of their bundled submodules.

### Fixed
- Rebound OpenGL path vertex and index buffers for every draw so off-screen
  shadow and filter passes cannot leave stale VAO state that corrupts later
  indexed paths, including on NVIDIA drivers.
- Split installed renderer exports by component so a consumer requesting only
  `COMPONENTS Software` does not import OpenGL, FreeType, or HarfBuzz targets
  from a package that also contains the OpenGL renderer.
- Avoided mixing CMake module-mode and config-mode FreeType targets when an
  installed OpenGL package also resolves HarfBuzz through a package manager.

## [0.1.18] - 2026-07-30

### Added
- Added composable `ImageFilterChain` nodes for blur, inner shadow, color
  matrix, and offset effects, with matching Software, OpenGL, and Vulkan
  validation coverage.
- Added backend-neutral `FrameCompiler` packets and expanded `RenderStats`
  diagnostics for frame compilation, device execution, delayed GPU timing,
  compiled bytes, and tracked renderer memory.
- Archived the complete quality-gated 27-cell NanoVG comparison matrix,
  preserved its raw ABBA process samples, and added automated checks that keep
  public performance claims tied to the committed evidence.

### Changed
- Reorganized the README performance summary around workload coverage,
  quality gates, confidence intervals, and links to the detailed evidence.
- Batched pairwise-disjoint simple fills across blend-state changes while
  preserving strict barriers for overlapping, clipped, scissored, stroked, and
  shader-backed paths.
- Cached shared path bounds and packed AA coverage and bulk-remapped indices,
  reducing the final inconclusive 256-shape geometry case from 46 draw calls
  to 4. A four-block ABBA follow-up measured 0.605 ms for WhatsCanvas and
  0.807 ms for NanoVG with the pixel-quality gate passing.

## [0.1.17] - 2026-07-29

### Added
- Added retained-memory diagnostics for glyph atlases, tessellation and bitmap
  text caches, and pooled render targets through `Canvas::RenderStats` and the
  image-filter benchmark.
- Added dense mixed-geometry and multilingual text stress scenes to the unified
  performance suite.
- Added a machine-readable cross-library benchmark contract, fixed-font Latin
  scene, adapter runner, pixel-quality gates, and self-calibration tests.
- Added an optional NanoVG GL3 benchmark adapter and a checked-in,
  quality-gated three-process 1080p comparison baseline.
- Added parameterized cross-library matrices that vary workload scale, data,
  structure, textures, rounded geometry, render state, and generated text,
  with ABBA process scheduling and bootstrap confidence intervals.

### Changed
- Changed the unified performance suite default resolution from 960 x 540 to
  1920 x 1080.
- Bounded geometry, bitmap-text, and render-target caches by byte budgets;
  reused OpenGL/Vulkan filter targets; reduced the initial portable glyph atlas
  to 2048 x 2048; and removed one full-size floating-point working buffer from
  software blur and inner-shadow rendering.
- Accelerated the deterministic Software backend with fused RGBA Gaussian
  sampling, bounded row-parallel blur passes, common blend and solid-raster
  fast paths, and direct one-to-one layer image sampling without changing
  regression pixels.
- Accelerated rounded-image and shadow-heavy scenes with native uniform-rounded
  shader coverage, OpenGL sprite batching, cropped Software/Vulkan shadow work,
  GPU-only Vulkan blur, deferred temporary-target reclamation, and batched
  Vulkan path-shadow silhouette submissions.
- Reused OpenGL sprite GPU resources across frames and recorded compatible
  glyph-atlas quads as compact image batches instead of one command per glyph.
- Normalized translated fill geometry for reusable tessellation and anti-alias
  mesh caching, merged compatible Vulkan solid primitives, and reported the
  resulting backend draw-call count instead of the pre-merge command count.
- Accelerated Software rendering with an axis-aligned textured-quad scan path
  and a uniform-interior triangle path that skips unused barycentric work.
- Batched compatible OpenGL path geometry across affine transforms with bounded
  vertex chunks, and batched Vulkan textured quads with packed per-vertex tint
  plus per-frame descriptor-set reuse.
- Added ordered OpenGL multi-texture sprite batches, persistent sprite state,
  multi-packet path topology reuse, GPU shape parameters, compact short-path
  recording, and redundant GL-state elimination. The latest quality-gated
  27-cell matrix records 26 WhatsCanvas wins, no NanoVG wins, and one
  statistically inconclusive cell on the reference machine.
- Preserved compact indexed geometry through Vulkan submission, introduced
  asynchronous frame upload slots and persistent command buffers, and reduced
  Vulkan glyph-atlas and texture submission overhead.
- Eliminated high-count dynamic-text cache thrashing with O(1) LRU maintenance
  and a working-set-aware layout cache.

### Fixed
- Preserved OpenGLES image sampling precision and desktop-independent
  presentation paths.
- Avoided desktop-only sampler-object calls on OpenGLES and made wrapped
  OpenGL framebuffer readback independent of incidental driver binding state.
- Assigned distinct texture units to mixed OpenGL sampler types so strict Mesa
  drivers accept path draws as well as desktop vendor drivers.
- Honored Vulkan command scissors and hardened packed path-attribute decoding.
- Added cross-backend filter pixel-parity gates for OpenGL, OpenGLES, and
  Vulkan.

## [0.1.16] - 2026-07-25

### Added
- Added backend-neutral image and backdrop filters across Software,
  OpenGL/OpenGLES, and Vulkan: Gaussian blur, practical frosted-glass color and
  grain treatment, and `ImageFilter::innerShadow` /
  `ImageFilter::innerShadowSigma`. The exported `ImageFilter` value retains its
  existing ABI size by using type-specific payload storage.
- Added `LayerOptions` integration, filter work statistics, an image-filter
  showcase, and cross-backend filter/parity tests.
- Added full Windows shared-package builds for OpenGL, OpenGLES, and Vulkan,
  together with an installed-package consumer example and CI smoke coverage.

### Changed
- Large GPU blur kernels now use adaptive per-axis downsampling and a
  full-resolution restore pass to reduce pixel-pass work.
- Documentation and public/internal API comments now match the implemented
  frame lifecycle, output targets, Vulkan capabilities, packaging matrix, and
  text backend behavior.

### Fixed
- Preserved rounded/path clipping while rendering filtered OpenGL layers and
  hardened fractional-offset, Decal-edge, and translucent inner-shadow parity.
- Tessellated transformed curves in device space and prevented analytic-AA
  overdraw artifacts.
- Preserved Fluent font face identity and registered the Segoe UI Semibold face
  for correct weight selection.

## [0.1.15] - 2026-07-16

### Added
- **DirectWrite text backend** (Windows): first-class native adapter that
  replaces the portable rasterizer when `TextBackendKind::DirectWrite` is
  selected. Covers measurement, real line breaking via `IDWriteTextLayout`,
  weight/slant/spacing/locale styling, custom in-memory font registration and
  fallback chains, underline / strikethrough decorations, grayscale and
  ClearType raster modes, and `resolveFontFamilies` parity with the portable
  backend. See [`doc/DIRECTWRITE_TEXT_BACKEND.md`](doc/DIRECTWRITE_TEXT_BACKEND.md).
- **Per-`Paint` `TextRenderMode` override**: `Paint::setTextRenderMode(Auto |
  Grayscale | ClearType)` lets callers opt into ClearType per draw when the
  destination surface is known to be opaque and axis-aligned. The DirectWrite
  backend caches raster output per mode.
- **Cross-backend text decoration parity**: underline and strikethrough now
  render consistently on the portable backend, the DirectWrite backend, and
  the software backend, with a regression test that pixel-compares all three.
- **OpenGL `ClipFill` primitive support in `executeDrawList`**: a new
  `DrawClipFillProgram` in `src/opengl/` mirrors Vulkan's clip pipeline
  (mask.r × tint color), closing an ADR-006 parity gap. Handles both the
  full-target quad emit and the arbitrary-geometry emit.

### Changed
- **Text sharpness / HiDPI**: text now rasterizes at effective device pixel
  size under a scaled transform or `Canvas::setDevicePixelRatio`, and glyph
  quads snap to pixel grid on axis-aligned draws — glyphs stay crisp on
  high-DPI displays instead of being blurred by post-upscale.
- **Default anti-aliasing**: `Paint` now defaults to anti-aliased strokes and
  fills; opt out with `Paint::setAntiAlias(false)`.
- **DirectWrite three-layer cache**: repeat UI text draws pay only the
  `scissor + quad-submit` cost per frame. (1) COM apartment + WIC/D2D
  factories cached process-wide, (2) rasterized bitmap + intrinsic metrics
  cached in an LRU (4 MB byte budget by default) keyed by
  `(text, paint, dpr, render-mode)`, (3) `Canvas` keeps a 256-entry LRU of GPU
  textures keyed by the backend-provided content id so identical text skips
  the CPU→GPU upload entirely.
- **Shared `CommandDrawListEncoder` clipped-command handling**: previously
  aborted the whole encode on a clipped point/line/vector-text command;
  now logs a warning and continues so the rest of the offscreen replay still
  produces output. (ADR-006)
- **DirectWrite backend contract cleanup**: `registerFontFace`,
  `setFontFallbackChain`, and `resolveFontFamilies` now match portable
  behavior. Cross-platform validation matrix updated to reflect DirectWrite
  as shipped.

### Fixed
- **MSVC build on non-English Windows**: `CMakeLists.txt` now passes `/utf-8`
  to MSVC so UTF-8 source files decode consistently under the active ANSI
  code page. Previously a Chinese Windows installation could corrupt the
  DirectWrite backend test's CJK line-break declaration.

### Docs
- Refreshed `doc/DIRECTWRITE_DESIGN_REVIEW.md` with a status table mapping
  each of the five original review issues to the PR(s) that closed it.
- ADR-006 gained a Progress Log section (PR #42 / PR #44).

## [0.1.14] - 2026-07-08

### Removed
- **Breaking (pre-1.0):** the public `Canvas::flush()` method. Use `endFrame()`
  instead — drawing is now a symmetric `beginFrame() / endFrame()` pair.
  `endFrame()` renders the recorded commands onto a freshly-cleared framebuffer,
  makes them readable, and consumes them (call it exactly once per frame, right
  before `readPixelsRGBA` / `present`). Migration: replace every `canvas.flush()`
  with `canvas.endFrame()`.

### Changed
- Documented the offscreen frame lifecycle to avoid a "black readback" pitfall:
  `beginFrame → draw → endFrame → readPixelsRGBA` is the complete flow. Because
  `endFrame()` re-clears the framebuffer before rendering the (now consumed)
  commands, calling it twice yields an empty image. Clarified the
  `beginFrame`/`endFrame` API comments and added a Get Started section and a
  Troubleshooting entry.

## [0.1.13] - 2026-07-08

### Fixed
- Windows prebuilt-binary portability: compile with `_USE_STD_VECTOR_ALGORITHMS=0`
  so the shipped libraries do not reference toolset-version-specific MSVC STL
  helpers (`__std_min_element_f_` / `__std_max_element_f_`). Previously a package
  built on a newer CI toolchain failed to link on a consumer's older Visual
  Studio with `LNK2019`. The Windows packaging job is pinned to `windows-2022`,
  and `doc/TROUBLESHOOTING.md` documents the symptom and fixes.

## [0.1.12] - 2026-07-08

### Added
- Unified backend creation: `Canvas::create(Backend, width, height)` (plus a
  preference-list overload `Canvas::create({...}, w, h)`), `Backend backend()`,
  and `Canvas::isBackendAvailable(Backend)`. `Backend` is an enum
  (`Auto`, `OpenGL`, `OpenGLES`, `Software`, `Vulkan`, ...). The returned canvas
  is sized but initializes lazily on the first draw/flush.
- Experimental presentation / output-target layer: a single `Canvas::setOutputTarget`
  chooses where frames go — `OutputTarget::Offscreen` / `OffscreenTexture` /
  `ToWindow` / `GLFramebuffer` / `VulkanImageTarget` — with a unified frame loop
  (`beginFrame → draw → flush → present`; `present()` is a no-op for non-window
  targets). Backends: software (Windows GDI + Linux X11), OpenGL (WGL; guarded
  GLX), and **Vulkan windowed present** (present-ready instance/device +
  swapchain, validated under the Khronos validation layer). Plus GL/Vulkan
  wrap-external and `Canvas::vulkan*` interop accessors. Examples:
  `software_present`, `gl_present`, `vulkan_canvas_present`. See
  `doc/windowed-presentation-design.md`.
- Built-in diagnostics/logging facility (`wsc/Log.h`): severity levels, an
  adjustable threshold (`Log::setLevel`), and a pluggable sink
  (`Log::setHandler`) so applications can route WhatsCanvas messages into their
  own logging system. Existing ad-hoc `stderr` diagnostics now flow through it
  with consistent categories.
- Concise API doc comments across the public headers (`Canvas`, `Paint`, `Path`,
  `Image`, `Color`, `Font`, `Log`) for in-editor hover documentation.
- MIT `LICENSE`.
- `CONTRIBUTING.md` with the local build/test/validation workflow and repository
  conventions.
- `CHANGELOG.md`.
- Integration-focused **Get Started as a Library** guide and a
  **Troubleshooting & FAQ** page.
- MkDocs Material documentation site (GitHub Pages) built from `doc/`.

### Changed
- Reworked `README.md` into an evaluation → onboarding funnel with a capability
  overview, comparison table, and combined quality showcase image.

### Removed
- **Breaking:** the old creation API — the default `Canvas()` constructor,
  `Canvas::createSoftware`, `Canvas::createVulkan`, and
  `Canvas::isVulkanAvailable`. Use `Canvas::create(Backend, w, h)` and
  `Canvas::isBackendAvailable(Backend)` instead. Migration:
  `Canvas::createSoftware(w, h)` → `Canvas::create(Backend::Software, w, h)`;
  `Canvas::createVulkan(w, h)` → `Canvas::create(Backend::Vulkan, w, h)`;
  `Canvas c;` → `auto c = Canvas::create(Backend::OpenGL, w, h);`.

### Fixed
- Corrected stale documentation and the architecture diagram that described the
  Vulkan backend as unimplemented; all backends are now reflected accurately.

## [0.1.11]

### Added
- User-selectable **Vulkan** backend (`Canvas::createVulkan`,
  `Canvas::isVulkanAvailable`), opt-in via `-DWHATSCANVAS_ENABLE_VULKAN=ON`,
  rendering off-screen alongside the OpenGL family.
- Pure-CPU **software** backend (`Canvas::createSoftware`) with no GPU
  dependency, plus a standalone `WhatsCanvas::Software` target
  (`-DWHATSCANVAS_BUILD_SOFTWARE=ON`).

For changes prior to 0.1.11, see the
[GitHub Releases](https://github.com/ClarkWain/WhatsCanvas/releases) history.

[Unreleased]: https://github.com/ClarkWain/WhatsCanvas/compare/v0.5.0...HEAD
[0.5.0]: https://github.com/ClarkWain/WhatsCanvas/compare/v0.4.0...v0.5.0
[0.4.0]: https://github.com/ClarkWain/WhatsCanvas/compare/v0.3.0...v0.4.0
[0.3.0]: https://github.com/ClarkWain/WhatsCanvas/compare/v0.2.0...v0.3.0
[0.2.0]: https://github.com/ClarkWain/WhatsCanvas/compare/v0.1.20...v0.2.0
[0.1.20]: https://github.com/ClarkWain/WhatsCanvas/compare/v0.1.19...v0.1.20
[0.1.19]: https://github.com/ClarkWain/WhatsCanvas/compare/v0.1.18...v0.1.19
[0.1.18]: https://github.com/ClarkWain/WhatsCanvas/compare/v0.1.17...v0.1.18
[0.1.17]: https://github.com/ClarkWain/WhatsCanvas/compare/v0.1.16...v0.1.17
[0.1.16]: https://github.com/ClarkWain/WhatsCanvas/compare/v0.1.15...v0.1.16
[0.1.15]: https://github.com/ClarkWain/WhatsCanvas/compare/v0.1.14...v0.1.15
[0.1.14]: https://github.com/ClarkWain/WhatsCanvas/compare/v0.1.13...v0.1.14
[0.1.13]: https://github.com/ClarkWain/WhatsCanvas/compare/v0.1.12...v0.1.13
[0.1.12]: https://github.com/ClarkWain/WhatsCanvas/compare/v0.1.11...v0.1.12
[0.1.11]: https://github.com/ClarkWain/WhatsCanvas/releases/tag/v0.1.11

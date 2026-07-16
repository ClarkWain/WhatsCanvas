# Changelog

All notable changes to WhatsCanvas are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
For releases and downloadable artifacts, see the
[GitHub Releases](https://github.com/ClarkWain/WhatsCanvas/releases) page.

## [Unreleased]

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

[Unreleased]: https://github.com/ClarkWain/WhatsCanvas/compare/v0.1.15...HEAD
[0.1.15]: https://github.com/ClarkWain/WhatsCanvas/compare/v0.1.14...v0.1.15
[0.1.14]: https://github.com/ClarkWain/WhatsCanvas/compare/v0.1.13...v0.1.14
[0.1.13]: https://github.com/ClarkWain/WhatsCanvas/compare/v0.1.12...v0.1.13
[0.1.12]: https://github.com/ClarkWain/WhatsCanvas/compare/v0.1.11...v0.1.12
[0.1.11]: https://github.com/ClarkWain/WhatsCanvas/releases/tag/v0.1.11

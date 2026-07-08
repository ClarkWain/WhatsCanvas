# Changelog

All notable changes to WhatsCanvas are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
For releases and downloadable artifacts, see the
[GitHub Releases](https://github.com/ClarkWain/WhatsCanvas/releases) page.

## [Unreleased]

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

[Unreleased]: https://github.com/ClarkWain/WhatsCanvas/compare/v0.1.13...HEAD
[0.1.13]: https://github.com/ClarkWain/WhatsCanvas/compare/v0.1.12...v0.1.13
[0.1.12]: https://github.com/ClarkWain/WhatsCanvas/compare/v0.1.11...v0.1.12
[0.1.11]: https://github.com/ClarkWain/WhatsCanvas/releases/tag/v0.1.11

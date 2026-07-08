# Changelog

All notable changes to WhatsCanvas are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
For releases and downloadable artifacts, see the
[GitHub Releases](https://github.com/ClarkWain/WhatsCanvas/releases) page.

## [Unreleased]

### Added
- Experimental on-screen presentation layer: backend-neutral `NativeSurface` /
  `ISwapchain`, public `Canvas::attachPresentSurface` / `present` /
  `resizePresentSurface`, and a software (CPU) window path on Windows via GDI
  (`examples/software_present`). Other backends remain off-screen for now. See
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

[Unreleased]: https://github.com/ClarkWain/WhatsCanvas/compare/v0.1.11...HEAD
[0.1.11]: https://github.com/ClarkWain/WhatsCanvas/releases/tag/v0.1.11

# WhatsCanvas API Stability

WhatsCanvas aims to be a lightweight, embeddable Canvas-style 2D renderer. This document defines which surfaces are intended for application use and how changes should be reviewed.

## Stable Public Surface

The stable public surface lives under `include/wsc/` and is exported through the install/package target:

- `wsc/wsc.h`
- `wsc/base.h`
- `wsc/Canvas.h`
- `wsc/CanvasStats.h`
- `wsc/Color.h`
- `wsc/Export.h`
- `wsc/Paint.h`
- `wsc/Path.h`
- `wsc/Image.h`
- `wsc/ImageFilter.h`
- `wsc/Font.h`
- `wsc/FontResolver.h`
- `wsc/FontSystem.h`
- `wsc/Matrix.h`
- `wsc/Picture.h`
- `wsc/Log.h`
- `wsc/Surface.h`
- `wsc/Version.h`
- `wsc/TextureSource.h`

The generated API index is maintained in `doc/API_REFERENCE.md`.
Refresh it after public header changes through the configured CMake project:

```sh
cmake --build build --target WhatsCanvasGenerateApiReference
```

The direct script entry point is also available:

```sh
python scripts/generate_api_reference.py
```

CI or local checks can verify that it is current:

```sh
cmake --build build --target WhatsCanvasCheckApiReference
```

or directly:

```sh
python scripts/generate_api_reference.py --check
```

These types form the expected consumer contract:

- `wsc::Canvas`
- `wsc::Paint`
- `wsc::Path`
- `wsc::Image`
- `wsc::ImageFilter`
- `wsc::LayerOptions`
- `wsc::FontFace`
- `wsc::FontDescriptor`
- `wsc::FontFallbackChain`
- `wsc::FontManager`
- `wsc::ITextureSource`
- `wsc::NativeSurface`
- `wsc::SwapchainConfig`
- `wsc::OutputTarget`
- geometry/value types such as `PointF`, `SizeF`, `RectF`, and `Matrix4`

## Package Targets

The supported CMake package targets are:

- `WhatsCanvas::OpenGL` when `WHATSCANVAS_BUILD_OPENGL=ON`
- `WhatsCanvas::OpenGLES` when built with `WHATSCANVAS_BUILD_OPENGLES=ON`
- `WhatsCanvas::Software` when built with `WHATSCANVAS_BUILD_SOFTWARE=ON`

Vulkan does not have a separate package target. When enabled with
`WHATSCANVAS_ENABLE_VULKAN=ON`, it is compiled into the `WhatsCanvas::OpenGL`
target and selected at runtime with `Canvas::Backend::Vulkan`. Vulkan remains
optional and is unavailable when no usable Vulkan SDK/device is present.

Tagged mobile SDKs expose the same public headers through these delivery surfaces:

- Android Prefab package `whatscanvas`, target `whatscanvas::whatscanvas`
- iOS `WhatsCanvas.xcframework`, delivered as a static Metal library

Repository-internal targets, helper libraries, and example targets should not be treated as application-facing API.

## Experimental or Internal Surface

The following areas may change without API compatibility guarantees:

- files under `src/`
- command/render backend internals
- shader implementation details
- tests, benchmarks, and scripts
- generated build folders and packaged layout internals beyond the installed headers, libraries, and CMake config
- native backend placeholders that are not exposed as package targets

## Compatibility Rules

For the 1.x release line:

- documented public source APIs remain backward-compatible within 1.x;
- minor releases may add APIs and deprecate existing ones, while removals or
  incompatible semantic changes require a new major version;
- a deprecated API remains available for at least one subsequent minor release
  and identifies its replacement where one exists;
- patch releases contain compatible fixes, but may correct unsafe, undefined,
  or clearly contradicted behavior;
- documented header paths, release asset names, and package target names remain
  stable within 1.x; and
- binary compatibility applies only to an official package used with its stated
  architecture, compiler/runtime, SDK, configuration, and dependency versions.
  Cross-compiler, cross-STL, cross-NDK/CRT, or custom-build ABI compatibility is
  outside the contract; source compatibility is the portable boundary.

When changing stable public API:

- Prefer additive changes.
- Preserve existing method names, argument meaning, and default behavior where practical.
- Keep binary and source compatibility in mind for exported classes.
- Document new public APIs in README or a focused doc page.
- Add or update tests that exercise the public behavior.

Breaking changes should be explicit and intentional:

- update README usage examples if affected
- update this document when the public boundary changes
- bump the package version according to the release impact
- call out migration notes in release documentation

## Current Scope Boundary

The project is not trying to mirror every feature of large graphics engines. Stable API work should favor:

- common Canvas-style drawing
- predictable text and font handling
- GL-family backend reliability
- simple packaging and integration
- regression-friendly behavior

Features that significantly expand scope, such as full document backends, advanced color-management systems, or a browser-grade text editing stack, should be evaluated as optional extensions rather than assumed core API.

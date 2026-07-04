# WhatsCanvas API Stability

WhatsCanvas aims to be a lightweight, embeddable Canvas-style 2D renderer. This document defines which surfaces are intended for application use and how changes should be reviewed.

## Stable Public Surface

The stable public surface lives under `include/wsc/` and is exported through the install/package target:

- `wsc/wsc.h`
- `wsc/Canvas.h`
- `wsc/CanvasAdapter.h`
- `wsc/Paint.h`
- `wsc/Path.h`
- `wsc/Image.h`
- `wsc/Font.h`
- `wsc/Matrix.h`
- `wsc/base.h`
- `wsc/TextureSource.h`

The generated API index is maintained in `doc/API_REFERENCE.md`.
Refresh it after public header changes:

```sh
python scripts/generate_api_reference.py
```

CI or local checks can verify that it is current:

```sh
python scripts/generate_api_reference.py --check
```

These types form the expected consumer contract:

- `wsc::Canvas`
- `wsc::CanvasAdapter`
- `wsc::Paint`
- `wsc::Path`
- `wsc::Image`
- `wsc::FontFace`
- `wsc::FontDescriptor`
- `wsc::FontFallbackChain`
- `wsc::FontManager`
- `wsc::ITextureSource`
- geometry/value types such as `PointF`, `SizeF`, `RectF`, and `Matrix4`

## Package Targets

The supported CMake package targets are:

- `WhatsCanvas::OpenGL`
- `WhatsCanvas::OpenGLES` when built with `WHATSCANVAS_BUILD_OPENGLES=ON`

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

# WhatsCanvas 1.0.0

WhatsCanvas 1.0.0 is the first stable release of the C++17 embeddable
Canvas-style 2D renderer. The 1.x public source/API compatibility contract is
defined in `doc/public/reference/API_STABILITY.md`.

## Release Assets

- `whatscanvas-win64-release-1.0.0.zip`
- `whatscanvas-linux-x64-release-1.0.0.zip`
- `whatscanvas-macos-universal-release-1.0.0.zip`
- `whatscanvas-android-release-1.0.0.aar`
- `whatscanvas-ios-release-1.0.0.zip`

Desktop archives contain public headers, Release libraries, CMake package
files, and license notices. The Android asset is a Prefab AAR for
`armeabi-v7a`, `arm64-v8a`, and `x86_64`. The iOS archive contains a static
XCFramework for arm64 devices and arm64/x86_64 simulators. The Android demo APK
is validation-only and is not a release asset.

## Highlights

- Stable Canvas-style drawing, paths, images, filters, layers, text, pictures,
  surfaces, and render-stat APIs.
- Software, OpenGL, OpenGL ES, and Metal delivery, with optional Vulkan support.
- Portable OpenType shaping, font fallback, CJK, variable-font, and color-emoji support.
- Package-consumer, API-reference, version, performance, sanitizer, fuzz, and
  cross-backend validation infrastructure.

## Known Boundaries

- Applications own windows, input, UI layout, accessibility, lifecycle,
  application signing, and store distribution.
- Vulkan remains optional and has narrower device/driver coverage than the
  primary backends.
- WebGPU, Direct3D, full document/rich-text editing, and comprehensive color
  management are outside the 1.0 contract.
- Binary compatibility is limited to each official package's documented
  compiler/runtime, SDK, architecture, configuration, and dependency set.
- Repeat Android/iOS device testing was deferred for the 1.0 tag based on prior
  multi-device testing. Subsequent representative hardware validation completed
  the stable-v1 target; see
  [`mobile-hardware-signoff-2026-09.md`](../../../internal/validation/mobile-hardware-signoff-2026-09.md).

See [`CHANGELOG.md`](../../../../CHANGELOG.md), the current maintainer
[`release checklist`](../../../internal/operations/release-checklist.md), and
the archived [`dependency audit`](dependency-audit.md) for detailed evidence.


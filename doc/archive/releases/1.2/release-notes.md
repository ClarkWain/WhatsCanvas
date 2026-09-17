# WhatsCanvas 1.2.0

WhatsCanvas 1.2.0 adds bulk image drawing and glyph coverage-mask blur,
improves rendering and text-cache efficiency, and corrects clipped offscreen
rendering in the OpenGL backends.

## New APIs and examples

- `Canvas::drawImageRects` draws ordered source/destination rectangles with
  shared image and paint state, preserving scalar drawing semantics for
  complex sampling, color matrices, and Picture recording.
- `Paint::setTextMaskBlur` / `getTextMaskBlur` control glyph coverage-mask blur.
  The default remains zero. Overlapping glyphs can differ from blurring a
  composited text run; unsupported cases use a filtered layer.
- Desktop chess and Chinese chess examples include AI opponents, animated
  moves, and cross-platform font fallback.

## Rendering and correctness

- Release the owned Linux font-discovery configuration after enumeration,
  avoiding process-global Fontconfig allocations in embedded use.
- Fix incorrect clipping and missing tint in overlapping frosted-glass panels
  on OpenGL/OpenGL ES (issue #100). Clip-mask sampling now compensates for the
  cropped offscreen viewport origin.
- Add 29 analytic pixel checks to both OpenGL and OpenGL ES parity gates,
  covering solid, gradient and image content, overlapping and nested layers,
  and restoration of the main rendering target.
- Re-resolve glyph atlas entries after growth or repacking, including cached
  blurred glyphs, and avoid attributing unrelated sticky OpenGL errors to a
  valid image upload.

## Efficiency

- Bound portable text-metric and glyph-mask caches and invalidate them when
  their font/provider generations change.
- Initialize native text backends on first text use.
- Reduce image submission, program switching, clip-mask generation and buffer
  upload overhead while preserving ordering and layer boundaries.
- Specialize Gaussian shader loops for their active sample count.

Existing published performance measurements retain their documented hardware
and workload scope; these changes do not imply a new universal speedup claim.

## Compatibility

The new APIs are additive, with existing defaults retained. `Paint` gains stored
state: rebuild libraries and consumers against matching 1.2.0 headers instead
of mixing new headers with older binaries. Cross-compiler, runtime and custom
build ABI compatibility is outside the documented package contract.

## Distribution

Release assets use the existing package layout and target names:

- `whatscanvas-win64-release-1.2.0.zip`
- `whatscanvas-linux-x64-release-1.2.0.zip`
- `whatscanvas-macos-universal-release-1.2.0.zip`
- `whatscanvas-android-release-1.2.0.aar` (Prefab SDK)
- `whatscanvas-ios-release-1.2.0.zip` (XCFramework)

The Android demo APK is not a release asset. Web remains a source-built
WebAssembly/WebGL 2 integration. OpenGL and Software blur implementations may
have small pixel differences; the fix restores clipping and layer composition,
not bit-identical blur across backends.

See [CHANGELOG](../../../../CHANGELOG.md) for the full change list and
[API stability](../../../public/reference/API_STABILITY.md) for support boundaries.

# WhatsCanvas Android

This directory contains the first Android host for WhatsCanvas. It creates an
OpenGL ES 3 context with `GLSurfaceView`, crosses JNI on the render thread, and
draws through `WhatsCanvas::OpenGLES` into the host-owned framebuffer.

The demo records static card chrome, text, and fixed paths once with
`Canvas::recordPicture()`, raster-caches that explicitly isolated full-screen
Picture, then draws only the animated overlays each frame. This mirrors
Flutter's DisplayList + RepaintBoundary/RasterCache split while keeping GPU
resources out of the retained core object. The cache is context-keyed, bounded
by a per-Canvas byte budget, and purged before orderly context teardown. See the
[Retained Picture guide](../../doc/RETAINED_PICTURE.md) for current image/layer
limitations.

This README is the runnable sample's quick-start. For the production embedding
contract, JNI ownership, GL-thread lifecycle, fonts, orientation, validation,
and troubleshooting, see the
[Android Integration Guide](../../doc/ANDROID_INTEGRATION.md).

## Toolchain

The initial toolchain is aligned with the known-good GPUMark Android
configuration while keeping the sample independent of any machine-local path:

| Component | Version |
| --- | --- |
| Android Gradle Plugin | 7.2.2 |
| Gradle Wrapper | 7.4 |
| Kotlin | 1.7.20 |
| compile SDK | 33 |
| build tools | 33.0.1 |
| min SDK | 21 |
| target SDK | 33 |
| NDK | 21.4.7075529 |
| CMake | 3.18.1 |
| C++ | 17 |

The demo builds `armeabi-v7a`, `arm64-v8a`, and `x86_64` by default.

## Build

Create `local.properties` if Android Studio has not generated it:

```properties
sdk.dir=C\:\\Users\\your-name\\AppData\\Local\\Android\\Sdk
```

From this directory on Windows:

```bat
gradlew.bat :app:assembleDebug
gradlew.bat :app:assembleProfile
```

Use Debug for correctness/debugger work and Profile for representative device
performance. Debug intentionally compiles native code with `-O0`; Profile keeps
the package debuggable and symbolized but makes `-O2 -DNDEBUG` the final native
flags. The APKs are written to
`app/build/outputs/apk/debug/app-debug.apk` and
`app/build/outputs/apk/profile/app-profile.apk` respectively. Release remains
the shipping configuration and is not signed with the sample debug key.

Tagged GitHub releases also attach
`whatscanvas-android-demo-profile-<version>.apk`. It contains all three sample
ABIs and uses the Android debug key so it can be installed for feature and
performance evaluation. It is not a production-signed application or an AAR;
shipping applications must build/sign their own host from source.

## Current scope

- OpenGL ES 3 rendering through the existing WhatsCanvas GLES target.
- Per-view native renderer ownership, resize, frame rendering, and deterministic
  GL-thread cleanup over JNI.
- A responsive eight-card scene that uses 2x4 portrait and 4x2 landscape
  layouts and exercises UTF-8 text, system font fallback, gradients, paths,
  clipping, arcs, strokes, alpha blending, transforms, raw RGBA images, texture
  sampling, and animation through the device-pixel ratio.
- Android 10+ font discovery through the public system font matcher. API 21-28
  parse Android's system/vendor/product font configuration, with a small set of
  legacy paths retained only as the final compatibility fallback. Both paths
  preserve variable-font instance axes for HarfBuzz/FreeType and cache identity;
  public `Paint::setFontVariation()` can override an axis for an application run.
- Text/emoji presentation selectors, default emoji presentation, skin-tone ZWJ
  sequences, regional-indicator flags, and keycaps rendered through HarfBuzz,
  bundled FreeType, and the RGBA glyph atlas. CBDT/CBLC 2.0 and 3.0 PNG emoji
  are both accepted for older and newer Android system fonts. The sample scene
  and startup probe exercise these paths on the active device image.

The sample includes density-aware touch interaction, lifecycle/context-loss
handling, CI builds and lint, retained-scene diagnostics, and device checkpoints
on Pixel 3 (Android 12) and Redmi K30 (Android 11/MIUI 12.5). Encoded-image use
in this demo, managed-emulator instrumentation, AAR/Prefab packaging, broad OEM
device coverage, and Vulkan presentation remain follow-up work.

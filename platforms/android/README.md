# WhatsCanvas Android

This directory contains the first Android host for WhatsCanvas. It creates an
OpenGL ES 3 context with `GLSurfaceView`, crosses JNI on the render thread, and
draws through `WhatsCanvas::OpenGLES` into the host-owned framebuffer.

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
```

The APK is written to `app/build/outputs/apk/debug/app-debug.apk`.

## Current scope

- OpenGL ES 3 rendering through the existing WhatsCanvas GLES target.
- Per-view native renderer ownership, resize, frame rendering, and deterministic
  GL-thread cleanup over JNI.
- A responsive eight-card scene that uses 2x4 portrait and 4x2 landscape
  layouts and exercises UTF-8 text, system font fallback, gradients, paths,
  clipping, arcs, strokes, alpha blending, transforms, raw RGBA images, texture
  sampling, and animation through the device-pixel ratio.
- Android 10+ font discovery through the public system font matcher, with a
  legacy-path compatibility fallback for API 21-28.

Touch input, encoded image decoding, lifecycle stress automation, Android CI,
AAR packaging, and Vulkan presentation remain follow-up work.

# WhatsCanvas Android Integration Guide

This guide describes how to embed WhatsCanvas in an Android application using
OpenGL ES 3, `GLSurfaceView`, CMake, and JNI. The runnable implementation is in
the repository's `platforms/android/` directory; this document defines the integration
and lifecycle contract that a production host should follow.

WhatsCanvas does not currently publish an Android AAR. Integrate it from source
or use the checked-in Android application as the starting module.

## Integration Architecture

The Android host owns the window, EGL context, render thread, and presentation:

```text
Activity
  -> GLSurfaceView
    -> GLSurfaceView.Renderer (GL thread)
      -> per-view JNI native handle
        -> wsc::Canvas::Backend::OpenGLES
          -> GLSurfaceView default framebuffer
```

The important ownership boundary is that Android owns EGL and buffer swapping;
WhatsCanvas records and executes drawing commands into the framebuffer that is
current during `GLSurfaceView.Renderer.onDrawFrame`.

## Supported Configuration

The reference application currently uses:

| Component | Version / requirement |
| --- | --- |
| OpenGL ES | 3.0 or newer |
| Android Gradle Plugin | 7.2.2 |
| Gradle | 7.4 |
| Kotlin | 1.7.20 |
| compile / target SDK | 33 |
| min SDK | 21 |
| NDK | 21.4.7075529 |
| CMake | 3.18.1 |
| C++ | 17 |
| ABIs | `armeabi-v7a`, `arm64-v8a`, `x86_64` |

These exact build-tool versions describe the checked-in sample rather than a
permanent library requirement. OpenGL ES 3 and C++17 are the runtime and source
requirements that an embedding application must preserve.

## 1. Add the Native Build

Enable only the OpenGL ES backend in the Android CMake project. The sample uses
the following shape in `platforms/android/app/src/main/cpp/CMakeLists.txt`:

```cmake
set(WHATSCANVAS_BUILD_OPENGL OFF CACHE BOOL "" FORCE)
set(WHATSCANVAS_BUILD_OPENGLES ON CACHE BOOL "" FORCE)
set(WHATSCANVAS_BUILD_SOFTWARE OFF CACHE BOOL "" FORCE)
set(WHATSCANVAS_BUILD_DEMO OFF CACHE BOOL "" FORCE)
set(WHATSCANVAS_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

find_library(ANDROID_LIBRARY android REQUIRED)
find_library(EGL_LIBRARY EGL REQUIRED)
find_library(GLES3_LIBRARY GLESv3 REQUIRED)
find_library(LOG_LIBRARY log REQUIRED)

set(WHATSCANVAS_OPENGLES_LIBRARIES
    "${EGL_LIBRARY};${GLES3_LIBRARY}"
    CACHE STRING "" FORCE)

add_subdirectory("${WHATSCANVAS_ROOT}" whatscanvas)

add_library(my_android_renderer SHARED my_renderer_jni.cpp)
target_link_libraries(my_android_renderer PRIVATE
    WhatsCanvas::OpenGLES
    ${ANDROID_LIBRARY}
    ${EGL_LIBRARY}
    ${GLES3_LIBRARY}
    ${LOG_LIBRARY}
    ${CMAKE_DL_LIBS})
```

Set `WHATSCANVAS_ROOT` to the repository root. The relative path in the sample
depends on its checked-in directory layout and should not be copied unchanged
into a differently structured application.

The sample sets `WHATSCANVAS_ENABLE_FREETYPE_RASTERIZER=OFF` and
`WHATSCANVAS_ENABLE_OPENTYPE_SHAPING=OFF` to keep the first APK small, so text
falls back to `stb_truetype` rasterization and simple shaping. An application
that needs FreeType/HarfBuzz shaping should enable and package those
dependencies deliberately, then validate complex scripts, APK size, and every
ABI.

## 2. Configure Gradle

Connect the application module to CMake and select the ABIs that are actually
distributed:

```groovy
android {
    compileSdkVersion 33
    ndkVersion "21.4.7075529"

    defaultConfig {
        minSdkVersion 21
        targetSdkVersion 33

        externalNativeBuild {
            cmake {
                cppFlags '-std=c++17', '-fexceptions', '-frtti'
                arguments '-DANDROID_STL=c++_shared'
            }
        }

        ndk {
            abiFilters 'armeabi-v7a', 'arm64-v8a', 'x86_64'
        }
    }

    externalNativeBuild {
        cmake {
            path file('src/main/cpp/CMakeLists.txt')
            version '3.18.1'
        }
    }
}
```

Keep `libc++_shared.so` in the packaged APK when using
`-DANDROID_STL=c++_shared`. If the host application chooses a different ABI
set, dependencies and native libraries must use the same set.

## 3. Create the OpenGL ES Host

Configure `GLSurfaceView` before installing its renderer:

```kotlin
setEGLContextClientVersion(3)
setEGLConfigChooser(8, 8, 8, 8, 24, 8)
setRenderer(canvasRenderer)
renderMode = GLSurfaceView.RENDERMODE_CONTINUOUSLY
```

Declare the GLES requirement in `AndroidManifest.xml` so incompatible devices
are filtered or rejected before the renderer starts:

```xml
<uses-feature
    android:glEsVersion="0x00030000"
    android:required="true" />
```

The reference host requests `8/8/8/8 + depth 24 + stencil 8`. Current
anti-aliased path clipping uses clip masks rather than stencil operations, so a
stencil buffer is conservative rather than universally required. If the host
chooses a smaller EGL configuration, validate every feature it uses.

Load the JNI library from the renderer class or another initialization point:

```kotlin
System.loadLibrary("my_android_renderer")
```

## 4. Use One Native Renderer per View

Do not store the active `Canvas`, images, dimensions, or frame statistics in
process-wide JNI globals. Each `GLSurfaceView` should own an opaque native
handle whose C++ object owns all resources for that view.

The reference contract is:

| Kotlin/JNI operation | Native responsibility |
| --- | --- |
| `nativeCreate()` | Allocate a renderer object; do not create GL resources yet. |
| `nativeSurfaceCreated(handle)` | Load GLES procedures while the new context is current. |
| `nativeResize(handle, width, height, density)` | Set the viewport and create/initialize the sized Canvas. |
| `nativeRender(handle, elapsed)` | Execute one complete frame. |
| `nativeDestroy(handle)` | Finalize GL resources and delete the renderer on the GL thread. |

This avoids cross-view state corruption and makes context recreation explicit.
Treat the handle as thread-confined while rendering; the UI thread should only
create a handle while the GL thread is paused.

## 5. Initialize Canvas on the GL Thread

`onSurfaceCreated` runs with the GLES context current. Load functions there:

```cpp
void* loadOpenGlesProcedure(const char* name)
{
    if (const auto procedure = eglGetProcAddress(name)) {
        return reinterpret_cast<void*>(procedure);
    }
    return dlsym(RTLD_DEFAULT, name);
}

const bool loaded = wsc::Canvas::loadOpenGL(&loadOpenGlesProcedure);
```

In `onSurfaceChanged`, use physical pixel dimensions for the Canvas and apply
Android density separately:

```cpp
glViewport(0, 0, width, height);
const float safeDensity = std::isfinite(density) && density > 0.0f
    ? density : 1.0f;
auto canvas = wsc::Canvas::create(
    wsc::Canvas::Backend::OpenGLES, width, height);
canvas->setDevicePixelRatio(safeDensity);
if (!canvas->initializeContext()) {
    // Stop rendering and report initialization failure.
}
```

Accept every finite positive Android density, including low-density values
below `1.0`; use `1.0` only as a fallback for invalid input. Layout can then use
logical dimensions `width / density` and `height / density` while the renderer
retains a physical-resolution target. Read density again during
`onSurfaceChanged` instead of permanently capturing the value when the View is
constructed.

## 6. Render and Present

Render only from `onDrawFrame` or work explicitly queued to the GL thread:

```cpp
glDisable(GL_DEPTH_TEST);
glDisable(GL_SCISSOR_TEST);
glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

canvas->beginFrame();
// Canvas draw calls...
canvas->endFrame();
```

`GLSurfaceView` presents after the callback returns. The application should not
perform its own `eglSwapBuffers` inside the renderer callback.

## 7. Handle Pause and Resume

GL resources must be finalized while their EGL context is still current. Queue
destruction before pausing the `GLSurfaceView`:

```kotlin
override fun onPause() {
    canvasView.releaseNativeRenderer() // queueEvent { nativeDestroy(handle) }
    canvasView.onPause()                // waits for the GL thread to pause
    super.onPause()
}

override fun onResume() {
    super.onResume()
    canvasView.ensureNativeRenderer()
    canvasView.onResume()
}
```

Android's `GLSurfaceView` processes queued events before releasing the EGL
surface/context during `onPause`. `nativeDestroy` should call
`Canvas::finalizeContext()` before deleting the Canvas; finalization also clears
renderer-owned resources.

On resume, create a fresh native renderer. Expect Android to call
`onSurfaceCreated` and `onSurfaceChanged` again, and recreate images, glyph
atlases, fonts, clip resources, and other context-bound state.

Do not finalize a GLES Canvas from `Activity.onDestroy` on the UI thread. At
that point the EGL context may no longer be current.

### Unexpected EGL context loss

Orderly pause/resume is not the same as involuntary EGL context loss. After the
old context has already been lost, do not call GL deletion/finalization for its
objects against the newly created context. The current sample validates the
orderly pause path but does not yet expose a Canvas "abandon context" operation
for this case. A production host that must recover from unexpected loss needs a
separate abandoned-resource path, followed by complete renderer recreation.

## 8. Fonts and Text

Android 10 / API 29 added the NDK system font matcher. The reference host loads
the matcher dynamically so the APK remains compatible with min SDK 21:

- API 29+: match a Latin and a CJK character through `AFontMatcher`, then
  register the returned path, collection index, weight, and slant.
- If matching or registration fails, including on API 29+, probe the legacy
  Roboto and Noto CJK locations used by common Android system images. This is
  also the primary compatibility path on API 21-28.
- Configure a `FontFallbackChain` after registration.

The two probe characters do not enumerate a device's complete multilingual
fallback universe. System font files and family contents can vary by Android
version and OEM. For
deterministic brand typography, package licensed font assets with the app and
register those files or bytes instead of depending only on system paths.

If text is missing, inspect `WhatsCanvas` logcat messages for font registration,
verify that the font contains the requested glyphs, and confirm that the R8
glyph atlas is supported by the active GLES context.

## 9. Images and Other GL Resources

Images uploaded from RGBA data, decoded assets, wrapped external textures, and
render targets belong to the current GL context. Destroy them before
`Canvas::finalizeContext()` and recreate them after context initialization.

External textures remain owned by the host. Their lifetime must cover every
frame in which WhatsCanvas samples them, and they must be compatible with the
current GLES context.

## 10. Orientation and Responsive Layout

`onSurfaceChanged` can occur without Activity recreation when orientation and
screen-size changes are handled in the manifest. Always update:

- `glViewport` with physical pixels;
- Canvas size/context as required by the host strategy;
- logical layout width and height derived from density;
- cached size-dependent application geometry.

The reference scene uses a 2-by-4 grid in portrait and 4-by-2 in landscape,
with a compact card layout for short landscape viewports.

## 11. Build and Validate

From `platforms/android/` on Windows:

```bat
gradlew.bat :app:assembleDebug
gradlew.bat :app:lintDebug
gradlew.bat :app:assembleRelease
```

Install and launch the debug APK:

```bat
adb install -r app\build\outputs\apk\debug\app-debug.apk
adb shell am start -W -n com.whatscanvas.demo/.MainActivity
adb logcat -s WhatsCanvas:V AndroidRuntime:E libc:F
```

Validate at least:

- Latin and CJK text;
- fills, strokes, round caps, dashes, clipping, gradients, and blend modes;
- raw and decoded images used by the product;
- portrait and landscape resizing;
- background/foreground and repeated context recreation;
- every distributed ABI on an emulator or physical device;
- one real target device/GPU before release.

The source-level cross-platform gates remain relevant because geometry,
anti-aliasing, text atlas, and command generation are shared by Android and
other platforms. See `doc/CROSS_PLATFORM_VALIDATION_MATRIX.md` in the source
tree.

## Troubleshooting

### Blank or black surface

- Confirm GLES 3 was requested before `setRenderer`.
- Confirm `Canvas::loadOpenGL` and `initializeContext` succeeded.
- Confirm rendering happens on the GL thread with the context current.
- Check width and height passed to Canvas and `glViewport`.

### Text is absent

- Check the font registration paths and face indices in logcat.
- Verify fallback families and glyph coverage, especially for CJK.
- Ensure the glyph atlas texture was recreated after context loss.

### Dark seams on round caps or dashes

Use a version containing the round-cap shared-topology fix in
`StrokeTessellator`. This was a cross-platform analytic-AA issue, not an
Android-only GLES workaround.

### Emulator exits during app launch or resume

Distinguish an application crash from an emulator-host graphics failure. If the
entire AVD disappears and no `AndroidRuntime`/`libc` crash is recorded, retry
with another emulator GPU backend. On the reference Windows machine,
`-gpu host` was stable while SwiftShader state transitions were not.

## Current Limitations

- No published AAR or Prefab package.
- No Android CI or managed emulator gate yet.
- No checked-in touch/input abstraction.
- Encoded image decoding is available in the library but is not demonstrated by
  the current Android scene.
- Vulkan Android presentation is not wired into this host.
- Lifecycle validation is currently manual rather than an instrumentation test.
- Unexpected EGL context loss still needs an explicit abandoned-resource API;
  only orderly pause/resume recreation is implemented by the sample.

## Relationship to iOS

Android and iOS should keep separate host-integration documents because their
surface, context, lifecycle, font, and packaging contracts differ. Shared
Canvas behavior belongs in the platform-neutral library documentation.

The current Apple-platform boundary is documented in
[iOS Build Notes](IOS_BUILD_NOTES.md). When an in-repository UIKit/Metal host is
implemented, add `IOS_INTEGRATION.md` beside this guide and keep
`IOS_BUILD_NOTES.md` focused on toolchain and backend build configuration.

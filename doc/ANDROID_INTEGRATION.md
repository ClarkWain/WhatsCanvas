# WhatsCanvas Android Integration Guide

This guide describes how to embed WhatsCanvas in an Android application using
OpenGL ES 3, `GLSurfaceView`, CMake, and JNI. The runnable implementation is in
the repository's `platforms/android/` directory; this document defines the integration
and lifecycle contract that a production host should follow.

Tagged releases publish `whatscanvas-android-release-<version>.aar`. This
Prefab AAR contains the public `wsc` headers, `libwhatscanvas.so`, and the
required shared C++ runtime for `armeabi-v7a`, `arm64-v8a`, and `x86_64`.
The checked-in demo APK remains a local/CI validation host and is not a Release
asset.

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

## Use the Release AAR

Copy the release AAR into the application's `app/libs/` directory and add it as
a dependency:

```groovy
dependencies {
    implementation files('libs/whatscanvas-android-release-1.0.0.aar')
}

android {
    buildFeatures {
        prefab true
    }
}
```

Consume its Prefab target from the application's native CMake project:

```cmake
find_package(whatscanvas REQUIRED CONFIG)

add_library(my_android_renderer SHARED my_renderer_jni.cpp)
target_link_libraries(my_android_renderer PRIVATE
    whatscanvas::whatscanvas
    android EGL GLESv3 log)
```

Use C++17 and `-DANDROID_STL=c++_shared`, and restrict the application to ABIs
present in the AAR. The application still owns EGL, the `GLSurfaceView` or
equivalent host, the JNI lifecycle, frame scheduling, and signing. Include the
API from C++ as `<wsc/wsc.h>`.

Build the AAR from source with `gradlew.bat :whatscanvas:assembleRelease` in
`platforms/android/`. CI verifies the three native slices, Prefab metadata,
public headers, shared C++ runtime, and a clean three-ABI Prefab consumer before
publishing it.

## 1. Add the Native Build from Source

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

The sample enables both `WHATSCANVAS_ENABLE_FREETYPE_RASTERIZER` and
`WHATSCANVAS_ENABLE_OPENTYPE_SHAPING`. FreeType rasterizes Android color fonts
into the RGBA glyph atlas, while HarfBuzz is required for GSUB-based emoji such
as regional-indicator flags and ZWJ/skin-tone sequences. The Android build
contract keeps both features enabled and validates all three packaged ABIs.
Applications that disable FreeType retain the stb_truetype outline path but
lose COLRv1 rendering; disabling HarfBuzz degrades complex-script and emoji
sequence shaping and is not equivalent to the supported Android configuration.

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
renderMode = GLSurfaceView.RENDERMODE_WHEN_DIRTY
```

Drive `requestRender()` from `Choreographer.FrameCallback`. Continuous
`GLSurfaceView` mode owns an independent render loop and can produce buffers
that SurfaceFlinger never presents. VSYNC pacing avoids that wasted CPU/GPU
work and automatically follows the active display mode. Keep only one posted
callback, remove it on pause, and repost after every rendered VSYNC while
animation remains active. Do not gate the first callback on
`View.isAttachedToWindow`: `Activity.onResume()` is allowed to run before the
View is attached, and doing so can silently stop the loop after one frame.

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

### Animated surface refresh-rate intent

An application with continuous animation should declare that intent at both
the Window and Surface levels. The reference host sets
`WindowManager.LayoutParams.preferredRefreshRate` and, on API 30+, calls
`Surface.setFrameRate(60, FRAME_RATE_COMPATIBILITY_DEFAULT)` after the Surface
is created. `DEFAULT` is appropriate for animation that can follow the active
display cadence; `FIXED_SOURCE` is intended for video-like content with an
intrinsic frame rate. On API 31+ the demo permits an always/non-seamless mode
change, and on API 23+ it also selects the supported display mode nearest 60 Hz
through `preferredDisplayModeId`. These are preferences, not guarantees: OEM
display policies may
still select 30/50 Hz for power management. Consequently, report both the
renderer callback rate and SurfaceFlinger/display mode when diagnosing FPS;
an exact 30 or 50 FPS result on the matching active mode is correct pacing,
not a dropped-frame signal. The Redmi K30 validation device, for example,
reported a 60 Hz desired mode but retained its OEM-selected 50 Hz active mode;
WhatsCanvas recorded about 1 ms and submitted in about 0.7-2.2 ms while the
callback rate correctly remained 50 FPS.

The sample logs a five-second `renderedFps` window and bounded RenderStats
cache/upload counters. Production applications should route equivalent data to
their telemetry layer or compile the logs out, but should retain a way to
distinguish recording CPU, GL submission, GPU execution, and presentation.

## 7. Handle Pause and Resume

Do not destroy a healthy renderer on every ordinary pause. The reference host
sets `preserveEGLContextOnPause = true`, stops scheduling frames in
`Activity.onPause`, and retains the native renderer/cache when the Activity is
only moving to the background. This matches Flutter's separation between
pausing presentation and losing the graphics context.

When the Activity is actually finishing or changing configuration, queue
destruction before pausing the `GLSurfaceView`, while its context is current:

```kotlin
override fun onPause() {
    canvasView.stopRendering()
    if (isFinishing || isChangingConfigurations) {
        canvasView.releaseNativeRenderer() // queueEvent { nativeDestroy(handle) }
    }
    canvasView.onPause()
    super.onPause()
}

override fun onResume() {
    super.onResume()
    canvasView.ensureNativeRenderer()
    canvasView.onResume()
    canvasView.startRendering()
}
```

Android's `GLSurfaceView` processes queued events before releasing the EGL
surface/context during `onPause`. `nativeDestroy` should call
`Canvas::finalizeContext()` before deleting the Canvas; finalization also clears
renderer-owned resources.

On an ordinary resume the retained EGL Context, Canvas, Picture-derived command
cache, and raster layer remain valid. If Android revoked the context,
`onSurfaceCreated` reports a different current `EGLContext`; the sample calls
`Canvas::abandonContext()` before initializing the replacement Context. This
drops all old GL names without issuing deletion calls against the new Context,
then rebuilds images, glyph atlases, fonts, clip resources, and Picture raster
layers from portable CPU state.

Do not finalize a GLES Canvas from `Activity.onDestroy` on the UI thread. At
that point the EGL context may no longer be current.

### Redmi K30 performance checkpoint (2026-08-17)

The feature-matrix demo was measured on device `3d323803` at 1080 x 2305,
DPR 2.75, Adreno OpenGL ES 3.2. The comparison used the instrumented Debug APK.
The same workload was sampled both while the OEM selected 60 Hz and while MIUI
selected its 50 Hz mode.

| Metric | Compiled Picture only | Static raster layer + dynamic overlays |
| --- | ---: | ---: |
| Renderer callback rate | about 38-40 FPS | 59.0-59.6 FPS at 60 Hz; 49.7-50.0 FPS at 50 Hz |
| Commands/frame | about 122 | 10 |
| Draw calls/frame | about 112 | 9 |
| Path uploads/frame | about 35 | 6 |
| Warm path upload bytes/frame | about 190 KB | about 46-57 KB |
| Static Picture CPU/frame | about 0.27-0.36 ms after compile | about 0.03-0.13 ms |
| Dynamic record CPU/frame | about 3.5-13.7 ms | typically about 3.3-8.2 ms |
| Flush CPU/frame | about 14-21.5 ms | typically about 2.1-4.9 ms |
| Static raster residency | none | 1 layer, about 9.5 MB |

The static card/text/path scene is an explicit opaque compositing boundary. It
is recorded as backend-neutral Picture operations, compiled per Context, then
rasterized to a context-owned texture covering the Picture's conservative local
bounds. The raster cache has a configurable 32 MB per-Canvas soft budget, LRU
eviction, and size/byte/eviction statistics. Context teardown purges the derived
texture before backend finalization; the CPU Picture remains portable.

Continuously changing fill geometry no longer enters the final AA cache after a
single observation. It must reuse its base geometry once before admission. This
stopped animated progress widths from evicting stable entries; fill AA stabilized
near 764 KB and stroke AA near 4.3 MB. The screenshots before and after an
ordinary pause/resume produced an identical MD5 for a static text-card crop.
The resumed renderer hit the existing raster layer instead of spending about
1.25 seconds synchronously rebuilding it.

### Unexpected EGL context loss

Orderly finalization releases both eagerly initialized draw programs and lazy
context-bound effects, including Gaussian-blur targets and clip-coverage/fill
resources. The OpenGL lifecycle regression renders clipped shadows, finalizes
the old Canvas while its context is current, destroys that context, creates a
second context, and requires identical output with no GL error. This protects
the sample's normal pause/resume path from stale FBO, texture, VAO, and VBO
names.

Orderly pause/resume is not the same as involuntary EGL context loss. After the
old context has already been lost, do not call GL deletion/finalization for its
objects against the newly created context. Call `Canvas::abandonContext()` on
the GL thread instead. It invalidates shared image handles, compiled Picture
commands, raster layers, programs, buffers, atlases, clip/filter targets, and
other backend state without issuing GL deletes. Then make the replacement
Context current and call `initializeContext()`. Use `finalizeContext()` only for
orderly teardown while the owning Context is still current.

## 8. Fonts and Text

Android 10 / API 29 added the NDK system font matcher. WhatsCanvas now owns this
integration in its core Android system-font provider; an embedding application
does not need to discover or pre-register Android system font files itself.

- API 29+: each unresolved text cluster is passed to `AFontMatcher` with the
  requested generic family, weight, slant, and BCP-47 locale. While the
  returned `AFont` and its path are valid, WhatsCanvas snapshots the font into
  immutable shared memory together with collection index, actual weight/slant,
  and variation axes. Shaping and rasterization therefore do not reopen or
  retain a requirement for a stable Android filesystem path. The provider and
  rasterizer share that immutable byte snapshot; loading a face does not copy
  the complete font into a second persistent vector. An unexpectedly
  large font above the guarded snapshot limit retains the legacy file-backed
  fallback. Complete cluster coverage is still verified by the rasterizer.
- Matcher results are cached by family/style/locale/cluster. Calling
  `Canvas::refreshSystemFonts()` clears platform match results, loaded raster
  faces, shaping/layout results, and glyph-atlas state through a new resolver
  generation.
- API 21-28 cannot use the public NDK matcher. The provider parses modern
  `fonts.xml`, product/vendor customizations, and the legacy
  `system_fonts.xml`/`fallback_fonts.xml` schema. It retains family aliases,
  configured weight/slant, TTC index, locale tags, `fallbackFor`, and config
  order. Skia-compatible details include isolating `fallbackFor` faces from
  their parent named family, exact-weight alias families, product-defined named
  families, family/file `compact` or `elegant` variants, and variation-axis
  coordinates. The rasterizer still verifies the complete requested cluster. A small
  set of common Roboto/Noto paths remains only as the final fallback when
  configuration is absent or damaged.
- When XML omits weight or style, the provider reads only the necessary
  `OS/2`, `head`, and `post` table prefixes (including TTC/OTC collection
  offsets) before candidate sorting. Explicit XML values take precedence;
  style selection follows CSS3/Skia preference order rather than absolute
  weight distance.
- API 29+ matcher results also retain every `AFont_getAxis*` variation setting.
  Both API paths attach coordinates to `FontFace`; HarfBuzz shaping and
  FreeType rasterization consume the same values, and the values are part of
  face/glyph cache identity.
- Applications can override the discovered instance for one text run through
  public `Paint` state. A four-byte finite axis replaces the same tag from the
  resolved `FontFace`; other discovered axes remain intact:

  ```cpp
  wsc::Paint textPaint;
  textPaint.setFontFamily("sans-serif");
  textPaint.setFontVariation("wght", 525.0f);
  textPaint.setFontVariation("wdth", 90.0f);
  canvas.drawText("Variable text", 24.0f, 48.0f, textPaint);
  ```
- APK/bundle fonts can use `LazyFontProvider`. Registration stores only
  metadata; the loader is called on the first match for that family. The host
  can read from `AAssetManager`, an encrypted bundle, or another byte source:

  ```cpp
  auto assets = std::make_shared<wsc::LazyFontProvider>(
      wsc::FontProviderKind::ASSET, "apk-assets",
      [&](const std::string& assetPath)
          -> std::optional<std::vector<std::uint8_t>> {
        return loadAndroidAssetBytes(assetManager, assetPath);
      });
  wsc::LazyFontSource inter;
  inter.descriptor = wsc::FontDescriptor("App Inter", 400);
  inter.sourceId = "fonts/Inter-Regular.ttf";
  assets->registerSource(std::move(inter)); // no asset read here
  canvas.addFontProvider(assets);
  ```

  Loader callbacks run without the provider mutex. Failed loads are memoized;
  call `invalidateFamily()` after installing/replacing an asset to allow retry.
  `resolutionGeneration(family)` changes only for the affected family, so an
  unrelated font update does not invalidate every portable text cache.
  On Windows, DirectWrite also bridges the winning provider family into its
  custom font collection on first use and rebuilds it when that family's
  generation changes.

  `clearFontVariations()` restores the platform/font instance. Paint axes enter
  shaping, metrics, rasterization, shape/layout caches, and glyph-atlas
  identity together, so two instances cannot accidentally share glyph pixels.
- The public `FontSystem` default family aliases and fallback chain remain the
  cross-platform API. On Android they route to generic platform families such
  as `sans-serif`, `serif`, and `monospace`.
- U+FE0E and U+FE0F remain part of the original fallback cluster. The provider
  uses them to prefer text or emoji presentation families, while shaping and
  glyph coverage ignore the selector as a standalone drawable glyph.
- With FreeType enabled, the portable rasterizer interprets the common COLRv1
  paint graph used by Android's Noto Color Emoji (layers, solid/linear/radial
  fills, affine transforms, and visible composite fallback) and uploads RGBA
  glyphs. COLR/CPAL v0 and CBLC index-format 1 + CBDT image-format 17 PNG
  glyphs are also supported with both 2.0 and 3.0 CBDT/CBLC table versions.
  Android 12 on Pixel 3 ships the 2.0 form, while newer AOSP fixtures commonly
  use 3.0. Other CBDT/CBLC formats, sbix, SVG-in-OpenType, and exact advanced
  COLRv1 blend modes are still separate capability work.
- If rasterization still has to use the ASCII geometry fallback, its triangle
  vertices are normalized back to logical coordinates together with bitmap and
  atlas placement. This prevents HiDPI/Picture playback from scaling fallback
  question marks twice and scattering them outside the original text line.

System font files, configuration, and family contents can vary by Android
version and OEM. For
deterministic brand typography, package licensed font assets with the app and
register those files or bytes. If text is missing, verify the requested locale
and cluster, confirm that the selected font contains every glyph, and confirm
that the active GLES context supports glyph-atlas textures. A color font being
discovered still does not guarantee support for its particular table format;
inspect text diagnostics when a color glyph cannot be rasterized.

The parser regression corpus includes both small feature-focused fixtures and
complete configurations captured from a Google API 33 AVD and a Redmi K30 /
MIUI 12.5 Android 11 device. The complete files lock hundreds of real fallback
records, aliases, locale lists, TTC indices, and variable instances without
redistributing their font binaries. Provenance and checksums live in
`tests/fixtures/android_font_config/README.md`.

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
gradlew.bat :whatscanvas:assembleRelease
gradlew.bat :app:assembleDebug
gradlew.bat :app:lintDebug
gradlew.bat :app:assembleProfile
gradlew.bat :app:lintProfile
gradlew.bat :app:assembleRelease
```

Install and launch the debug APK:

```bat
adb install -r app\build\outputs\apk\debug\app-debug.apk
adb shell am start -W -n com.whatscanvas.demo/.MainActivity
adb logcat -s WhatsCanvas:V AndroidRuntime:E libc:F
```

Do not use the Debug APK as a performance baseline: AGP/NDK compile its native
targets with `-O0`. The sample's Profile variant remains installable with the
debug key and keeps symbols plus `run-as`/`simpleperf` access, but appends
`-O2 -DNDEBUG` after the NDK Debug defaults. Build and install it with:

```bat
gradlew.bat :app:assembleProfile
adb install -r app\build\outputs\apk\profile\app-profile.apk
```

Always inspect the generated `build.ninja` when changing this configuration:
an earlier attempt placed `-O2` before AGP's trailing `-O0`, producing a package
named Profile that still had Debug performance. The last optimization flag is
the effective one. Use Release for shipping/package validation; Profile exists
only for representative, symbolized measurements.

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

- Android Gradle debug/release builds and lint run in CI, but there is no managed
  emulator or instrumentation gate yet.
- The sample has local density-aware touch handling, but the core renderer does
  not define a cross-platform input abstraction.
- Encoded image decoding is available in the library but is not demonstrated by
  the current Android scene.
- Vulkan Android presentation is not wired into this host.
- Device lifecycle validation is currently manual rather than an instrumentation
  test; core orderly-finalize and no-delete-abandon paths have unit coverage.

## Validated Android Checkpoints

The `0.4.0` close-out was exercised on two physical devices in addition to the
three-ABI build/lint gate:

| Device | OS / GPU | Checks | Result |
| --- | --- | --- | --- |
| Google Pixel 3 | Android 12 / Adreno 630 | cold start, CJK/emoji, CBDT 2.0, HiDPI fallback, Picture playback, pause/resume, 60 Hz pacing | passed; steady-state callback rate about 60.7-60.8 FPS |
| Redmi K30 | Android 11 / MIUI 12.5 / Adreno | full feature scene, retained/raster cache, pause/resume, OEM 50/60 Hz modes | passed; follows the active 49.7-59.6 Hz display mode |

These are engineering checkpoints, not a promise of identical behavior across
all Android releases, vendors, refresh-rate policies, or GPU drivers. Validate
the actual product font set, effects, ABI set, and lifecycle on every shipping
device class.

## Relationship to iOS

Android and iOS should keep separate host-integration documents because their
surface, context, lifecycle, font, and packaging contracts differ. Shared
Canvas behavior belongs in the platform-neutral library documentation.

The current Apple-platform boundary is documented in
[iOS Build Notes](IOS_BUILD_NOTES.md). When an in-repository UIKit/Metal host is
implemented, add `IOS_INTEGRATION.md` beside this guide and keep
`IOS_BUILD_NOTES.md` focused on toolchain and backend build configuration.

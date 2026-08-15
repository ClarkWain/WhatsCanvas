# WhatsCanvas iOS Build Notes

This note records the current iOS-oriented integration path for the GL-family backend. It is a build and host-app contract, not a claim that the repository currently ships a full Xcode sample.

For comparison, Android now has a complete host guide and runnable sample; see
[Android Integration](ANDROID_INTEGRATION.md). Keep this file focused on Apple
toolchain/backend constraints. When a UIKit/Metal sample is added, document its
full lifecycle and packaging contract in a sibling `IOS_INTEGRATION.md` rather
than mixing platform-specific host instructions into the Android guide.

## Current Support Boundary

- Use the OpenGLES library target: `WhatsCanvasOpenGLES`.
- Disable the desktop demo target for iOS builds because the in-repo demo uses GLFW.
- The host application owns the native iOS view, context creation, swapchain presentation, and frame loop.
- The host application must make an OpenGLES context current before calling `Canvas::loadOpenGL`, `Canvas::initializeContext`, or any draw path that initializes the renderer.
- The current GL-family backend expects an OpenGLES 3.0-compatible context.
- Runtime validation on a physical iOS device or simulator remains separate from the desktop GLES compile smoke gate.

## Metal Backend on iOS

WhatsCanvas ships a Metal render backend that is enabled by default on all
Apple platforms, including iOS and tvOS (`-DWHATSCANVAS_ENABLE_METAL=ON`).
The Objective-C++ implementation gates all platform-specific code through
`TARGET_OS_IPHONE` / `TARGET_OS_TV`:

- Storage mode selection uses `MTLStorageModeShared` unconditionally on
  iOS/tvOS (unified memory) and skips the managed-mode
  `synchronizeResource` blit.
- The Cocoa/UIKit surface bridge accepts either a `CAMetalLayer *` handed
  through `NativeSurface::Cocoa` or a `UIView *` whose `+layerClass`
  already returns `CAMetalLayer`. Because `UIView.layer` is read-only,
  the host application is responsible for the `+layerClass` subclass —
  Canvas does not attempt to replace an existing layer.
- The CMake package links `Metal`, `Foundation`, `QuartzCore`, and
  (when found) `CoreGraphics`, `AppKit`, `UIKit` on Apple builds; the
  UIKit link is a no-op on macOS-only configurations because the
  framework is not found there.

The library can therefore be used on iOS through either
`Canvas::Backend::OpenGLES` (existing GLES 3.0 path) or
`Canvas::Backend::Metal`. The two paths coexist in the same target and
are selected at runtime.

## CMake Shape

A minimal iOS configure should keep only the GLES library target enabled:

```sh
cmake -S . -B build-ios \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DWHATSCANVAS_BUILD_OPENGL=OFF \
  -DWHATSCANVAS_BUILD_OPENGLES=ON \
  -DWHATSCANVAS_BUILD_DEMO=OFF \
  -DBUILD_TESTING=OFF \
  -DWHATSCANVAS_INSTALL=ON
cmake --build build-ios --target WhatsCanvasOpenGLES --config Release
```

If the selected iOS toolchain requires explicit GLES framework linkage, provide it through `WHATSCANVAS_OPENGLES_LIBRARIES`, for example:

```sh
cmake -S . -B build-ios \
  -DCMAKE_SYSTEM_NAME=iOS \
  -DWHATSCANVAS_BUILD_OPENGL=OFF \
  -DWHATSCANVAS_BUILD_OPENGLES=ON \
  -DWHATSCANVAS_BUILD_DEMO=OFF \
  -DWHATSCANVAS_OPENGLES_LIBRARIES="-framework OpenGLES"
```

## Host App Responsibilities

The host should follow this order:

1. Create an `EAGLContext` or other platform-provided OpenGLES context.
2. Make the context current on the render thread.
3. Call `wsc::Canvas::loadOpenGL` with the platform proc-address loader used by the app.
4. Create `wsc::Canvas`, call `setSize`, and call `initializeContext`.
5. Render frames through `beginFrame`, draw calls, and `endFrame`.
6. Call `releaseResources` before context loss, background teardown, or view destruction.
7. Call `finalizeContext` after resource release and before destroying the native GL context.
8. Recreate or reinitialize the native GL context, call `loadOpenGL` if required by the loader, then call `initializeContext` again before drawing.

The public context lifecycle methods added for this flow are:

- `Canvas::initializeContext()`
- `Canvas::finalizeContext()`
- `Canvas::isContextInitialized()`
- `Canvas::releaseResources()`

## Validation Checklist

- Configure/build `WhatsCanvasOpenGLES` with desktop OpenGL disabled.
- Confirm `WHATSCANVAS_OPENGL_ES` is defined for the target.
- Confirm shaders use `#version 300 es` and precision qualifiers.
- Confirm desktop-only states remain guarded away from GLES builds.
- Run at least one host-app frame that clears, draws paths/images/text, calls
  `endFrame()`, and presents.
- Exercise background/foreground or view recreation by calling `releaseResources`, `finalizeContext`, then `initializeContext` again after a fresh current context is available.

## Known Gaps

- No in-repository Xcode/iOS sample app is currently checked in.
- No automated iOS simulator/device smoke target is currently registered in CTest.
- Text and font backend parity across iOS and desktop is still tracked separately in the text feature matrix.
- The Metal backend now ships alongside the GLES path on Apple builds, but on-device / simulator smoke coverage is still an open item.

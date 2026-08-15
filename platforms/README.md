# Platform Hosts

This directory contains platform-owned application hosts and integration
bridges. Cross-platform renderer implementation remains in `src/` and public
headers remain in `include/`.

## Layout

| Directory | Responsibility | Status |
| --- | --- | --- |
| `android/` | Gradle application, `GLSurfaceView`, JNI, Android fonts, and OpenGL ES presentation | Runnable sample |
| `ios/` | UIKit host and Metal/OpenGL ES presentation | Planned; current constraints are in `doc/IOS_BUILD_NOTES.md` |
| `web/` | Emscripten/WebGL host and JavaScript bridge | Planned |

Keep platform build systems, manifests, lifecycle adapters, and sample UI in
this directory. Backend-neutral Canvas behavior, tessellation, text layout,
and rendering commands must not be duplicated here.

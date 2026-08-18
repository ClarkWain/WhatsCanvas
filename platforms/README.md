# Platform Hosts

This directory contains platform-owned application hosts and integration
bridges. Cross-platform renderer implementation remains in `src/` and public
headers remain in `include/`.

## Layout

| Directory | Responsibility | Status |
| --- | --- | --- |
| `android/` | Gradle application, `GLSurfaceView`, JNI, Android fonts, and OpenGL ES presentation | Runnable sample |
| `desktop/` | GLFW-based host (`WhatsCanvasDesktopHost`), OpenGL 3.3 core, scene registry, interactive / dump / benchmark modes | Runnable sample |
| `ios/` | UIKit, `CAMetalLayer`, CoreText, lifecycle handling, and UI tests | Runnable Metal sample |
| `web/` | Emscripten/WebGL host and JavaScript bridge | Planned |

Keep platform build systems, manifests, lifecycle adapters, and sample UI in
this directory. Backend-neutral Canvas behavior, tessellation, text layout,
and rendering commands must not be duplicated here.

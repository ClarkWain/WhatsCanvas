# WhatsCanvas Shader Portability Notes

This document tracks the GL-family shader portability rules used by WhatsCanvas.

## Current Rules

- Desktop OpenGL shaders use `#version 330 core`.
- OpenGLES shaders use `#version 300 es` plus default float precision qualifiers.
- Shader version directives are centralized through `src/opengl/GLShaderSource.h`.
- Desktop-only states are guarded away from GLES builds:
  - `GL_FRAMEBUFFER_SRGB`
  - `GL_PROGRAM_POINT_SIZE`
- The OpenGLES CMake target does not link desktop `OpenGL::GL`.
- The GLES-only smoke build is:

```bat
cmake -S . -B build-gles-check -DWHATSCANVAS_BUILD_OPENGL=OFF -DWHATSCANVAS_BUILD_OPENGLES=ON -DWHATSCANVAS_BUILD_DEMO=OFF -DBUILD_TESTING=OFF -DWHATSCANVAS_INSTALL=OFF
cmake --build build-gles-check --target WhatsCanvasOpenGLES --config Debug
```

## Current Shader Families

| Shader family | Portability status |
| --- | --- |
| DrawPoints | Uses shared version directive; skips desktop point-size state under GLES. |
| DrawLines | Uses shared version directive. |
| DrawPath | Uses shared version directive. |
| DrawImage | Uses shared version directive and GLES-compatible texture sampling. |
| DrawText | Uses shared version directive. |
| SpriteBatch | Uses shared version directive. |

## Remaining Runtime Validation

The GLES build proves compile-time portability on the current host toolchain. Device-level runtime validation still belongs in Android/iOS smoke targets because shader compiler behavior can vary across mobile GPUs.

For iOS host integration details, see [iOS Build Notes](IOS_BUILD_NOTES.md).

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
- Any varying or uniform that carries **world-space / logical-canvas
  coordinates** and takes part in a fragment-shader subtraction MUST be
  declared `highp`. `GLShaderSource.h` sets the file-wide default to
  `precision mediump float`, which represents only ~1024 distinct values over
  a large range; subtracting two similarly-large mediump values (as the
  linear-gradient formula
  `dot(vLocalPos - uLinearStart, direction) / dot(direction, direction)`
  requires) collapses to catastrophic precision loss and returns the first
  gradient stop for every fragment. `highp` restores full IEEE-754 single
  precision behavior on GLES; it is silently ignored by desktop
  `#version 330 core`, so the same shader source string is safe for both
  builds. Currently applied to `vLocalPos`, `uLinearStart`, `uLinearEnd`,
  and `uRadialCenter` in `DrawPath`, `DrawImage`, and `DrawText`.
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
| DrawPath | Uses shared version directive; `vLocalPos` and linear/radial gradient endpoint uniforms declared `highp` for large-coordinate correctness. |
| DrawImage | Uses shared version directive, GLES-compatible texture sampling; gradient endpoints and `vLocalPos` declared `highp`. |
| DrawText | Uses shared version directive; gradient endpoints and `vLocalPos` declared `highp`. |
| SpriteBatch | Uses shared version directive. |
| GaussianBlurProgram | Uses shared version directive with explicit `precision highp float` on both stages (blur math needs full precision). |

## Remaining Runtime Validation

The GLES build proves compile-time portability on the current host toolchain. Device-level runtime validation still belongs in Android/iOS smoke targets because shader compiler behavior can vary across mobile GPUs.

For iOS host integration details, see [iOS Build Notes](../platforms/IOS_BUILD_NOTES.md).

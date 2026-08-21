# Web rendering issues and fixes

This document records Web-specific failures found while porting the canonical
scene. Keep it with the platform because the same API differences can affect
future scenes even when native OpenGL ES appears correct.

## Strict element-buffer ownership

**Symptom:** paths disappeared and Chrome reported `GL_INVALID_OPERATION` from
`glDrawElements`.

**Cause:** WebGL 2 enforces the OpenGL ES rule that an element-array buffer is
owned by the currently bound VAO. A shared stream buffer had been uploaded as
an array buffer and later rebound as an element buffer. Desktop drivers had
tolerated the sequence.

**Fix:** `StreamBuffer` now has an immutable GL target, `DrawPath` owns a
dedicated element-array stream, and `SpriteBatch` restores its VAO at every
flush. The browser smoke treats new WebGL/GL-invalid diagnostics as failures.

## Alpha glyph texture format

**Symptom:** normal text appeared as opaque red rectangles.

**Cause:** the native OpenGL path stores glyph coverage in `R8` and uses
texture swizzle to expose it as alpha. Texture swizzle is not part of WebGL 2.

**Fix:** Emscripten builds expand `Alpha8` uploads to white RGBA with coverage
in alpha. Native OpenGL/OpenGL ES retain their existing compact representation.

## Mixed CJK and emoji fallback

**Symptom:** a complete mixed-script line became question marks even though
the Chinese font was registered successfully.

**Cause:** the paint selected the CJK family explicitly, while the symbol
fallback chain had only been attached to the default Latin family. Failure to
resolve one emoji cluster rejected raster shaping for the complete line.

**Fix:** both the Latin and CJK primary families have explicit symbol fallback.
The Web package preloads deterministic Latin/CJK assets plus a licensed
CBDT/CBLC + GSUB Noto Color Emoji subset, and enables HarfBuzz shaping for ZWJ,
modifier, flag, and keycap sequences.

## DPR and viewport scale

**Symptom:** a 1-DPR canvas was soft on high-density screens, or a physical-pixel
viewport made the Web scene look smaller than Android/iOS.

**Cause:** CSS size, drawing-buffer size, and renderer logical size were being
treated as one coordinate space.

**Fix:** CSS pixels define the canonical logical viewport. The backing buffer
is `logical size * DPR`, capped to the supported 1-4 range, and the Canvas gets
the same DPR. The test requires exact 2-DPR buffers for both canonical sizes.

## Background and context lifecycle

**Symptom:** animation jumped after returning to the page, or restored WebGL
content referenced resources from the lost context.

**Cause:** browser background throttling and WebGL context loss are independent
lifecycle events. A renderer cannot assume that requestAnimationFrame pacing
alone recreates GPU state.

**Fix:** visibility pauses drawing and resets the next-frame timestamp; frame
delta is also capped. Context loss abandons renderer and scene GPU resources,
then restoration reloads entry points, initializes the Canvas, and recreates
scene resources. The browser smoke forces both paths and performs a cold reload.

## Similar risks to check in new scenes

- VAO-local state: element buffers and vertex attribute enables/pointers.
- WebGL format restrictions: texture swizzle, sized internal formats, row
  alignment, framebuffer completeness, readback format, and mip completeness.
- Object lifetime across context loss: textures, buffers, VAOs, programs,
  framebuffers, renderbuffers, sync objects, and cached numeric handles.
- Mixed-script fallback from every explicitly selectable primary family, not
  only the default font.
- Logical coordinates versus physical pixels for clips, offscreen targets,
  readback rectangles, screenshots, pointer input, and resize/orientation.
- High-refresh displays: requestAnimationFrame may exceed 60 Hz. The release
  condition is sustained rendering at or above 60 Hz without simulation-time
  jumps, not an artificial 60-Hz cap.

Chrome's WebGL validation and forced context-loss extension cover the browser
API surface used by the canonical scene. They do not prove that unused renderer
features are browser-safe; each new scene must add a deterministic capture that
actually executes its new shader, texture, framebuffer, blend, and readback
paths.

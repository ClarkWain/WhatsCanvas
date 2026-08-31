# Troubleshooting & FAQ

Common issues when integrating WhatsCanvas, and how to fix them. Most stem from
the library's core contract: **it renders; it does not own your window, GL
context, or file I/O.**

## Rendering

### My image draws as a solid black rectangle

`drawImage` **tints** the image by the paint's color, and the default `Paint`
color is **black** — so a default paint renders the image black. Use white to
draw the image untouched:

```cpp
wsc::Paint tint;
tint.setColor(wsc::Color::WHITE);   // original image, no tint
canvas->drawImage(image, x, y, tint);
```

### My linear/radial gradient renders as a solid first color on Android/GLES

Symptom: a shape that shows a full multi-stop gradient on the desktop OpenGL
backend collapses to a uniform first-stop color on Android GLES. Typically
affects shapes drawn at large logical y-coordinates (e.g. cards or overlays
near the bottom of a portrait screen); shapes drawn near the origin, or drawn
inside a `save()/translate()` block that puts them in a local coordinate
frame, render correctly.

Root cause: the GLES fragment shader default is `precision mediump float`,
which cannot represent large canvas coordinates precisely enough for the
gradient formula
`t = dot(vLocalPos - uLinearStart, direction) / dot(direction, direction)`.
Subtracting two similarly-large mediump values collapses to noise, `t` is
clamped to 0, and every fragment samples `uGradientStopColors[0]`.

Fix (already applied in-tree): `vLocalPos`, `uLinearStart`, `uLinearEnd`, and
`uRadialCenter` are declared `highp` in `DrawPath`, `DrawImage`, and
`DrawText`. If you are patching a fork of an older version, apply the same
qualifiers under the `WHATSCANVAS_OPENGL_ES` shader branch. See
[Shader Portability](SHADER_PORTABILITY.md#current-rules).

### Nothing renders (or a crash) with the OpenGL backend

The OpenGL backend never creates a window or GL context — your app (or
GLFW/SDL/Qt) does. The required order is:

1. Create a GL context and **make it current**.
2. `wsc::Canvas::loadOpenGL(loader)` — hand over your platform's proc loader.
3. `canvas.setSize(w, h)` then `canvas.initializeContext()`.
4. `canvas.beginFrame()` → draw → `canvas.endFrame()` **with the context
   current**.

If you skip step 1–2 or draw without a current context, GL calls fail. The
software and Vulkan backends (`Canvas::create(Backend::Software, ...)` /
`Canvas::create(Backend::Vulkan, ...)`) need none of this — call `beginFrame()`
to initialize them lazily before drawing.

### Colors look washed out / semi-transparent blends look wrong

By default WhatsCanvas blends in straight sRGB space. For physically-correct
(linear-space) alpha blending, enable gamma correction:

```cpp
wsc::Canvas::setGammaCorrect(true);
```

The software backend mirrors the GL behaviour exactly, so output matches across
backends.

### My exported PNG is upside-down

`readPixelsRGBA` already returns **top-left-origin** rows. If your image writer
also flips vertically (e.g. `stbi_flip_vertically_on_write(1)`), you get a
double flip. Don't flip again.

### `readPixelsRGBA` returns all zeros (black / empty image)

On the Software backend's normal framebuffer path, a common cause is a
**second** `endFrame()` **after** you already ended the frame. `endFrame()`
consumes the recorded commands, so the second call has nothing to draw while
Software clears that framebuffer to transparent. Vulkan, OpenGL, and a
render-target canvas created with `OffscreenTexture()` retain their existing
target contents on an empty submission, but the extra call is still a lifecycle
error.

The correct offscreen sequence is:

```cpp
canvas->beginFrame();               // initializes lazily; resets queued frame state
canvas->drawRect(/* ... */, paint);
canvas->endFrame();                 // <- exactly once, right before reading
canvas->readPixelsRGBA(pixels);     // or savePixelsPPM("out.ppm")
```

Call `endFrame()` exactly once per frame, and do **not** call `beginFrame()`
after drawing (it discards queued commands before submission). Also confirm you
actually drew inside the canvas bounds with a non-transparent paint color.
OpenGL does not implicitly clear a host-owned framebuffer; clear it explicitly
when a fresh background is required.

## Performance and animation

### Dragging a few objects suddenly drops to 1 FPS

The object count is usually not the cause. Check whether each pointer move
invalidates a large retained `Picture`, calls `drawPictureRasterized` on it,
and uploads a near-full-screen texture again. A cache that is excellent while
idle can be the most expensive path during interaction.

Split static content from the dragged/animated overlay. Keep the smallest
useful invalidation boundary, and draw the moving object last. For repeated
visuals, bake one shared Image atlas and draw source rectangles; keep only
logical state per instance. Measure cache misses and the slowest frame, not
only average FPS. See the [performance tutorial](tutorials/11-performance.md)
and the [Android Spider case study](ANDROID_INTERACTIVE_PERFORMANCE.md).

### Should I convert everything to an Image?

No. Images are a good final representation for stable, repeated visuals, but
turning every object into a separate texture wastes memory and increases
texture switches. Prefer one atlas for related small visuals. Keep vectors for
content that changes frequently, needs resolution-independent scaling, or is
cheap to draw. Bake expensive, stable content once and reuse it.

### Why can `drawPictureRasterized` be slower than direct drawing?

Its first use and every cache miss must render the Picture into a texture and
upload/retain that texture. If the Picture is large, changes often, is drawn at
changing sizes/transforms, or exceeds the cache budget, this setup cost can be
larger than replaying the commands. Rasterize only when many future frames can
reuse the exact result; otherwise use `drawPicture` or direct drawing.

### The FPS counter says 60, but the animation looks like 1–3 frames

Check duration and easing before changing the renderer. At 60 Hz, a 50 ms
animation contains only about three display intervals. A steep quintic easing
or bounce can make only one or two positions visually meaningful. For short
drag snap/place transitions, start around 120–180 ms with a cubic ease-out and
no bounce; test the motion on the target device.

### Dealing cards drops FPS, while normal dragging is smooth

Look for state that accidentally participates in a much larger cache key. A
pressed stock button, Undo availability, or one card animation should not
invalidate a whole header or table texture. Also avoid rasterizing an element
while its scale changes. Draw transient controls and dealing cards in the
dynamic layer from stable images, and profile press, release, and first-use
frames separately.

### A cached radial gradient is no longer centered

Gradient coordinates and cached image bounds are in different coordinate
spaces. If a Picture is cropped to local bounds but the shader still receives
the original global center, the radial gradient shifts. Record the gradient in
local coordinates, compensate for the crop origin, or bake it once at the full
viewport size. Add a screenshot/pixel regression for the center and edges.

### How do I compare WhatsCanvas with native Android Canvas fairly?

Render the same number of visible objects, use the same target size and VSYNC
schedule, share image resources in both paths, and replay the same deterministic
gesture. Report rendered frame count, average and maximum work time, and slow
frames. Do not compare WhatsCanvas full-frame CPU timing with only Android
`View.onDraw` time as if their boundaries were identical. The Spider sample
provides `verify_drag_performance.ps1` and `compare_renderers.ps1` as a working
model.

## Text

### Text doesn't appear

Check, in order:

1. **Paint color** — a default (black) paint on a dark background is invisible.
2. **Font** — for deterministic output, register a font and set the family:
   ```cpp
   canvas->registerFontFace(wsc::FontFace::fromFile(wsc::FontDescriptor("Inter"),
                                                    "assets/fonts/Inter-Regular.ttf"));
   wsc::Paint p; p.setFontFamily("Inter"); p.setTextSize(24.0f);
   ```
3. **Position** — `drawText(text, x, y, paint)` places text at a baseline; make
   sure it is within the canvas.

### Glyphs are missing or fall back to boxes

Register a fallback chain so mixed scripts resolve to the right face:

```cpp
wsc::FontFallbackChain chain("Inter");
chain.addFallbackFamily("Noto Sans CJK");
canvas->setFontFallbackChain(chain);
```

FreeType and HarfBuzz are enabled by default
(`-DWHATSCANVAS_ENABLE_FREETYPE_RASTERIZER=ON`,
`-DWHATSCANVAS_ENABLE_OPENTYPE_SHAPING=ON`). When explicitly disabled or not
found, WhatsCanvas falls back to `stb_truetype` and simple shaping and reports
it in the text-backend diagnostics. The standalone Software target always uses
that built-in fallback stack.

## Backends

### How do I know if Vulkan is available? / graceful fallback

Vulkan is opt-in at build time (`-DWHATSCANVAS_ENABLE_VULKAN=ON` + a Vulkan SDK)
and compiles **into** `WhatsCanvas::OpenGL` (there is no separate `::Vulkan`
target). Probe at runtime and fall back:

```cpp
using Backend = wsc::Canvas::Backend;
std::unique_ptr<wsc::Canvas> canvas =
    wsc::Canvas::isBackendAvailable(Backend::Vulkan)
        ? wsc::Canvas::create(Backend::Vulkan, w, h)
        : wsc::Canvas::create(Backend::Software, w, h);
```

The Vulkan backend renders **off-screen by default**. On Win32 it can also use
`OutputTarget::ToWindow(...)` + `present()`; for portable/headless usage, read
the result with `readPixelsRGBA`.

### I want a binary that links no GPU libraries at all

Build the standalone software target:

```sh
cmake -S . -B build -DWHATSCANVAS_BUILD_OPENGL=OFF -DWHATSCANVAS_BUILD_SOFTWARE=ON
target_link_libraries(MyApp PRIVATE WhatsCanvas::Software)
```

### Context loss on mobile (Android background)

On GL context loss, release and re-initialize:

```cpp
canvas.releaseResources();
// ... after the platform re-creates the context and makes it current ...
canvas.initializeContext();
```

### `present()` returns false / nothing shows in my window

On-screen presentation is platform-dependent. Supported today: software
(Windows GDI + Linux X11), OpenGL (WGL; GLX on Linux), and Vulkan (Windows).
Checklist:

- Call `setOutputTarget(OutputTarget::ToWindow(surface))` and check its return
  value — it is `false` when presentation is unsupported for the current
  backend/platform or the surface has no window handle. Fall back accordingly
  (e.g. `glfwSwapBuffers` for GL, or off-screen + `readPixelsRGBA`).
- Initialize the backend before configuring a window or external target:
  `initializeContext()` is required for Vulkan and for the OpenGL renderer
  after `loadOpenGL(...)`; it is harmless but optional for Software.
- Fill the surface correctly: `platform = NativeSurface::Platform::Win32` and
  `window = <HWND>` (e.g. `glfwGetWin32Window(window)`).
- Create the window **without** a GL context for the software or Vulkan backend
  (`glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API)`); for the OpenGL backend, make
  the GL context current and call `Canvas::loadOpenGL` first.
- Present each frame **after** `endFrame()`: `beginFrame → draw → endFrame → present`.
- If you include `<windows.h>` (or a native GLFW header) in the same file,
  include the `wsc/` headers **first** and define `NOMINMAX`, so the `min`/`max`
  macros do not break WhatsCanvas headers.

See the backend hosts in
[`examples/present`](https://github.com/ClarkWain/WhatsCanvas/tree/master/examples/present)
for working setups.

## Build & packaging

### `find_package(WhatsCanvas ...)` can't be found

- Build a package first (`build.bat --release --package --no-run`) or download a
  release archive, then point CMake at it:
  `cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/package`.
- Use the exact version: `find_package(WhatsCanvas 0.2.0 CONFIG REQUIRED)`.

### Do consumers need GLFW / GLAD / GLM?

No. GLFW is only for the in-repo example windows, GLAD is compiled into the
GL-family backend, and GLM is an internal math dependency. Consumers include and
link only `WhatsCanvas::OpenGL` (or `::Software` / `::OpenGLES`) and `include/wsc/`.

### Windows: `LNK2019 unresolved external symbol __std_min_element_f_` (or `__std_max_element_f_`)

This is an MSVC standard-library (STL) **toolset-version mismatch** when linking a
prebuilt binary. Newer MSVC toolsets dispatch `std::min_element` / `max_element`
(etc.) on trivial types to out-of-line, ABI-versioned SIMD helpers whose symbols
older STL runtimes do not provide, so a library compiled with a newer toolset
fails to link on an older Visual Studio.

WhatsCanvas builds its own binaries with `_USE_STD_VECTOR_ALGORITHMS=0` (scalar
path), so the shipped libraries do **not** reference these version-specific
symbols and link against any VS 2022 STL. If you still hit this — e.g. building
WhatsCanvas yourself, or linking another prebuilt library — fix it by any of:

- Update Visual Studio 2022 so your toolset is at least as new as the one that
  built the binary (VS Installer -> Update), then rebuild.
- Build the offending library from source with your own toolset (identical STL
  on both sides).
- When building a library for redistribution, compile it with
  `-D_USE_STD_VECTOR_ALGORITHMS=0` (MSVC) so its objects avoid the versioned
  helpers.

There is no way to "use a different STL" here: on MSVC the standard library *is*
the MSVC STL. The mismatch is a general C++ binary-compatibility (ABI) issue, not
specific to WhatsCanvas.

## Diagnostics & logging

WhatsCanvas has a built-in logging facility (`wsc/Log.h`) that reports
recoverable problems and failures. By default only `Warning` and `Error`
messages are written to `stderr`.

### See more detail while debugging

Lower the threshold to surface informational and debug messages:

```cpp
#include <wsc/Log.h>

wsc::Log::setLevel(wsc::LogLevel::Debug); // Trace/Debug/Info/Warning/Error
```

### Route logs into your own system

Install a handler to forward every message (level, category, text) wherever you
want — a file, an in-game console, spdlog, etc.:

```cpp
wsc::Log::setHandler([](const wsc::LogMessage &m) {
    myLogger.log(m.level, m.category, m.message);
});
```

Pass `nullptr` to restore the default `stderr` sink. Use
`wsc::Log::setLevel(wsc::LogLevel::Off)` to silence all output.

### Common messages

| Category | Meaning |
|---|---|
| `Image` | An image failed to decode or its texture could not be created. |
| `DrawValidation` | A draw call was skipped (empty vertices, bad dimensions, invalid resource). |
| `GLProgram` / `OpenGL` | Shader compile/link failure or a GL error was detected. |
| `RenderDeviceFactory` | No usable render backend was found. |
| `VulkanRenderDevice` | Vulkan device/instance setup failed, or Vulkan is not compiled in. |
| `Deprecation` | A deprecated API was called (emitted once per call site). |

## Still stuck?

- Skim the [Get Started guide](GETTING_STARTED_AS_LIBRARY.md) and the runnable
  [`tests/package_consumer`](https://github.com/ClarkWain/WhatsCanvas/tree/master/tests/package_consumer).
- Check the [API Reference](API_REFERENCE.md) and [API Stability](API_STABILITY.md).
- Open an issue on [GitHub](https://github.com/ClarkWain/WhatsCanvas/issues).

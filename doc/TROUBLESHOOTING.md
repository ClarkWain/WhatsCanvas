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

### Nothing renders (or a crash) with the OpenGL backend

The OpenGL backend never creates a window or GL context — your app (or
GLFW/SDL/Qt) does. The required order is:

1. Create a GL context and **make it current**.
2. `wsc::Canvas::loadOpenGL(loader)` — hand over your platform's proc loader.
3. `canvas.setSize(w, h)` then `canvas.initializeContext()`.
4. Draw + `canvas.flush()` **with the context current**.

If you skip step 1–2 or draw without a current context, GL calls fail. The
software (`createSoftware`) and Vulkan (`createVulkan`) factories return an
already-initialized canvas and need none of this.

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

FreeType and HarfBuzz are optional (`-DWHATSCANVAS_ENABLE_FREETYPE_RASTERIZER=ON`,
`-DWHATSCANVAS_ENABLE_OPENTYPE_SHAPING=ON`); when absent, WhatsCanvas falls back
to `stb_truetype` and simple shaping and reports it in the text-backend
diagnostics.

## Backends

### How do I know if Vulkan is available? / graceful fallback

Vulkan is opt-in at build time (`-DWHATSCANVAS_ENABLE_VULKAN=ON` + a Vulkan SDK)
and compiles **into** `WhatsCanvas::OpenGL` (there is no separate `::Vulkan`
target). Probe at runtime and fall back:

```cpp
std::unique_ptr<wsc::Canvas> canvas =
    wsc::Canvas::isVulkanAvailable() ? wsc::Canvas::createVulkan(w, h)
                                     : wsc::Canvas::createSoftware(w, h);
```

`createVulkan` renders **off-screen** (no window/surface) — read the result with
`readPixelsRGBA`.

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

On-screen presentation is **experimental**. Supported today: software (Windows
GDI + Linux X11), OpenGL (WGL; GLX on Linux), and Vulkan (Windows). Checklist:

- Call `attachPresentSurface(surface)` once and check its return value — it is
  `false` when presentation is unsupported for the current backend/platform or
  the surface has no window handle. Fall back accordingly (e.g. `glfwSwapBuffers`
  for GL, or `readPixelsRGBA` off-screen).
- Fill the surface correctly: `platform = NativeSurface::Platform::Win32` and
  `window = <HWND>` (e.g. `glfwGetWin32Window(window)`).
- Create the window **without** a GL context for the software or Vulkan backend
  (`glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API)`); for the OpenGL backend, make
  the GL context current and call `Canvas::loadOpenGL` first.
- Present each frame **after** `flush()`: `beginFrame → draw → flush → present`.
- If you include `<windows.h>` (or a native GLFW header) in the same file,
  include the `wsc/` headers **first** and define `NOMINMAX`, so the `min`/`max`
  macros do not break WhatsCanvas headers.

See the `software_present`, `gl_present`, and `vulkan_canvas_present`
[examples](https://github.com/ClarkWain/WhatsCanvas/tree/master/examples) for
working setups.

## Build & packaging

### `find_package(WhatsCanvas ...)` can't be found

- Build a package first (`build.bat --release --package --no-run`) or download a
  release archive, then point CMake at it:
  `cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/package`.
- Use the exact version: `find_package(WhatsCanvas 0.1.11 CONFIG REQUIRED)`.

### Do consumers need GLFW / GLAD / GLM?

No. GLFW is only for the in-repo example windows, GLAD is compiled into the
GL-family backend, and GLM is an internal math dependency. Consumers include and
link only `WhatsCanvas::OpenGL` (or `::Software` / `::OpenGLES`) and `include/wsc/`.

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

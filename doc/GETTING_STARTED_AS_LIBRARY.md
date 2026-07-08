# Using WhatsCanvas as a Library

A task-oriented guide for developers who want to **use** WhatsCanvas in their own
project. For the full capability catalog see the [README](../README.md); for the
exact public symbols see the [API Reference](API_REFERENCE.md).

- **Just want to render something right now?** →
  [1. First pixel in 60 seconds](#1-first-pixel-in-60-seconds) (no GPU, window, or context).
- **Adding it to a real project?** → [3. Add WhatsCanvas to your build](#3-add-whatscanvas-to-your-build).
- **Choosing a backend?** → [2. Pick a backend](#2-pick-a-backend).
- **Curious about Vulkan?** → [4. The Vulkan backend, explained](#4-the-vulkan-backend-explained).

---

## 1. First pixel in 60 seconds

The **software (CPU) backend** is the fastest way to try WhatsCanvas: no OpenGL,
no window, no graphics context. It renders into memory and you read the pixels
back — ideal for a first test, servers, CI, or thumbnails.

```cpp
#include <wsc/wsc.h>

int main()
{
    // Sized and ready to draw — no GL context, no window.
    // The first draw/endFrame initializes the backend lazily.
    auto canvas = wsc::Canvas::create(wsc::Canvas::Backend::Software, 256, 256);

    wsc::Paint bg;
    bg.setColor(wsc::Color(18, 20, 24, 255));
    canvas->drawRect(wsc::RectF(0, 0, 256, 256), bg);

    wsc::Paint fill;
    fill.setColor(wsc::Color(40, 120, 240, 255));
    fill.setAntiAlias(true);
    canvas->drawRoundRect(wsc::RectF(40, 40, 176, 176), 24.0f, fill);

    canvas->endFrame();
    canvas->savePixelsPPM("first.ppm"); // open in an image viewer / convert to PNG
    return 0;
}
```

That is a complete, runnable program. No `loadOpenGL`, and no explicit
`initializeContext` — the first `endFrame()` initializes the software backend
lazily.

> `Canvas::create(...)` returns a `std::unique_ptr<wsc::Canvas>` that is already
> sized. Use `->` to call methods. It is *not* pre-initialized: drawing and
> `endFrame()` initialize it lazily, or call `initializeContext()` yourself if you
> need to read pixels before drawing.

### The frame lifecycle: `beginFrame` / `endFrame`

Drawing is bracketed by a matching pair. The minimal offscreen flow is exactly
four steps:

```cpp
canvas->beginFrame();                       // optional — draws auto-begin a frame
canvas->drawRect(/* ... */, paint);         // record draws
canvas->endFrame();                         // render + make readable (pairs with beginFrame)
canvas->readPixelsRGBA(pixels);             // or savePixelsPPM("out.ppm")
```

`endFrame()` renders the recorded commands onto a **freshly-cleared** framebuffer
and then **consumes** them. So call it **exactly once per frame**, right before
reading back or presenting:

- **Do not call `endFrame()` twice** in a row — a second end with no new draws
  re-clears the buffer and renders nothing, so you'd read back an all-zero
  (black/transparent) image. This is the usual cause of a "black screen".
- **Do not call `beginFrame()` after drawing** — it clears the framebuffer.

One `beginFrame`, your draws, one `endFrame`, then read/present.

---

## 2. Pick a backend

WhatsCanvas separates the **Canvas API** (what you draw) from the **backend**
(where it renders). One factory selects the backend:
`Canvas::create(Backend, width, height)` — returns `nullptr` if that backend is
unavailable in your build/host. Adding a future backend (Metal, D3D) needs no
new API, just a new `Backend` value.

| Backend | `Backend` value | Needs a GL context / window? | Use when |
| --- | --- | --- | --- |
| **Software (CPU)** | `Backend::Software` | No | Headless, servers, tests, thumbnails, "just works" everywhere |
| **OpenGL** | `Backend::OpenGL` | Yes (you own it) | Desktop apps/games with a window (GLFW, SDL, Qt, your engine) |
| **OpenGL ES** | `Backend::OpenGLES` | Yes (you own it) | Mobile / embedded GLES 3.0 |
| **Vulkan** (optional) | `Backend::Vulkan` | No (off-screen) | Vulkan pipelines; `nullptr` when unavailable |

```cpp
using Backend = wsc::Canvas::Backend;

// Explicit backend (sized; call initializeContext() before drawing — the first
// endFrame also initializes lazily):
auto canvas = wsc::Canvas::create(Backend::Software, 256, 256);
canvas->initializeContext();

// Or let WhatsCanvas pick the first available from a preference list:
auto best = wsc::Canvas::create({Backend::Vulkan, Backend::OpenGL, Backend::Software}, 256, 256);

// Query support / which backend you got:
bool hasVk = wsc::Canvas::isBackendAvailable(Backend::Vulkan);
Backend chosen = best->backend();
```

`Canvas::create(...)` is the single entry point for every backend. For the
"I already have a GL context" case, create an OpenGL canvas with size `0, 0` and
set the size yourself: `auto c = Canvas::create(Backend::OpenGL, 0, 0);`. Backend
selection is a **link-time** choice (which library target you link) plus this
**runtime** choice.

### OpenGL: you own the window and context

WhatsCanvas never creates a window or GL context. Your app (or GLFW/SDL/Qt)
creates the context, makes it current, then hands the loader to WhatsCanvas:

```cpp
#include <wsc/wsc.h>
#include <GLFW/glfw3.h>

int main()
{
    glfwInit();
    GLFWwindow *window = glfwCreateWindow(800, 600, "WhatsCanvas", nullptr, nullptr);
    glfwMakeContextCurrent(window);

    // Hand WhatsCanvas your platform's GL loader.
    wsc::Canvas::loadOpenGL(reinterpret_cast<wsc::Canvas::OpenGLProcAddress>(glfwGetProcAddress));

    auto canvasOwner = wsc::Canvas::create(wsc::Canvas::Backend::OpenGL, 800, 600);
    wsc::Canvas &canvas = *canvasOwner;
    canvas.initializeContext();

    while (!glfwWindowShouldClose(window)) {
        wsc::Paint p;
        p.setColor(wsc::Color(40, 120, 240, 255));
        p.setAntiAlias(true);
        canvas.drawRoundRect(wsc::RectF(80, 80, 320, 180), 16.0f, p);
        canvas.endFrame();

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    canvas.releaseResources();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
```

The lifecycle contract for the GL/GLES backends:

1. `Canvas::loadOpenGL(loader)` once, after a context is current.
2. `canvas.setSize(w, h)` then `canvas.initializeContext()`.
3. Draw + `canvas.endFrame()` per frame (with your context current).
4. `canvas.releaseResources()` before tearing down the context. On context loss
   (e.g. Android background), call `releaseResources()` and re-`initializeContext()`.

> The software and Vulkan backends need no `loadOpenGL`. They initialize lazily
> on the first draw/endFrame; call `initializeContext()` explicitly only if you read
> pixels before drawing.

---

## 3. Add WhatsCanvas to your build

### Option A — Use a prebuilt GitHub Release (fastest)

Tagged releases publish per-platform prebuilt packages on the repository's
**Releases** page. Asset names follow:

```
whatscanvas-<os>-release-<version>.zip
# e.g. whatscanvas-win64-release-0.1.14.zip
#      whatscanvas-linux-x64-release-0.1.14.zip
#      whatscanvas-macos-universal-release-0.1.14.zip
```

1. Download the archive for your OS from **Releases** and unzip it. You get:
   - `include/wsc/` — public headers
   - `lib/` — the library binaries
   - `lib/cmake/WhatsCanvas/` — `find_package` config files
2. Point `CMAKE_PREFIX_PATH` at the unzipped folder and consume the target:

```cmake
cmake_minimum_required(VERSION 3.16)
project(MyApp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(WhatsCanvas 0.1.14 CONFIG REQUIRED)

add_executable(MyApp main.cpp)
target_link_libraries(MyApp PRIVATE WhatsCanvas::OpenGL)   # or ::Software / ::OpenGLES
```

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/unzipped/whatscanvas
cmake --build build --config Release
```

> Prebuilt releases are built with the **default** options, i.e. the desktop
> OpenGL family. The optional Vulkan backend is **not** in prebuilt releases —
> to use Vulkan, build from source with `-DWHATSCANVAS_ENABLE_VULKAN=ON`
> (see [section 4](#4-the-vulkan-backend-explained)).

### Option B — Build the package yourself

```sh
# Windows
build.bat --release --package --no-run
# macOS / Linux
./build.sh --release --package --no-run
```

The package lands in `out/package/<Config>/` with the same layout as a release
archive; consume it exactly as in Option A (`CMAKE_PREFIX_PATH=.../out/package/Release`).
A complete, CI-verified minimal consumer lives in
[`tests/package_consumer`](../tests/package_consumer).

### Option C — `add_subdirectory` (vendoring the source)

Drop the repository into your tree (or a submodule) and:

```cmake
add_subdirectory(third_party/WhatsCanvas)
target_link_libraries(MyApp PRIVATE WhatsCanvas::OpenGL)
```

### Which target do I link?

| Target | Backend | Extra dependencies |
| --- | --- | --- |
| `WhatsCanvas::Software` | CPU only | **none** (no OpenGL/Vulkan) |
| `WhatsCanvas::OpenGL` | Desktop GL (+ optional Vulkan, see §4) | system OpenGL |
| `WhatsCanvas::OpenGLES` | GLES 3.0 | system GLES |

#### Software-only (no GPU dependency)

To produce a binary that links **no** graphics libraries at all:

```sh
cmake -S . -B build -DWHATSCANVAS_BUILD_OPENGL=OFF -DWHATSCANVAS_BUILD_SOFTWARE=ON
```

Right for headless services, CI containers, and platforms without a GPU/driver stack.

---

## 4. The Vulkan backend, explained

Vulkan is a **manual, opt-in** backend. Here is exactly how it exists and behaves:

**Opt-in, not automatic.** It is off by default. You enable it at configure time
with a Vulkan SDK present:

```sh
cmake -S . -B build -DWHATSCANVAS_ENABLE_VULKAN=ON
```

If the flag is off, there is no Vulkan code path. If the flag is on but no SDK is
found, it compiles as an **inert stub** (so the build still succeeds), and
`Canvas::isBackendAvailable(Canvas::Backend::Vulkan)` returns `false`.

**It coexists with OpenGL — it is not "either/or".** Vulkan is **not** a separate
library; its code is compiled **into the same `WhatsCanvas::OpenGL` target**. When
enabled, one library contains **both** the OpenGL and Vulkan backends, and you
choose between them **at runtime**:

```cpp
using Backend = wsc::Canvas::Backend;
auto gl = wsc::Canvas::create(Backend::OpenGL, 0, 0);   // OpenGL: loadOpenGL(...) + initializeContext()
auto vk = wsc::Canvas::create(Backend::Vulkan, w, h);   // Vulkan: off-screen
```

Always guard the Vulkan path so it degrades gracefully:

```cpp
using Backend = wsc::Canvas::Backend;
auto canvas = wsc::Canvas::isBackendAvailable(Backend::Vulkan)
                  ? wsc::Canvas::create(Backend::Vulkan, 512, 512)   // may still return nullptr
                  : wsc::Canvas::create(Backend::Software, 512, 512); // fallback
// ... render, then canvas->readPixelsRGBA(...)
```

**Why is Vulkan off-screen only (unlike OpenGL)?** Because the difference is
about **who owns the presentation surface**:

- The **OpenGL** backend renders into the framebuffer of the GL context *you*
  create and make current — so it can draw straight to your window.
- The **Vulkan** backend, through the Canvas API, does **not** own a Vulkan
  surface/swapchain. It renders into an off-screen image and you read it back with
  `readPixelsRGBA` (or use it as a texture). Windowed Vulkan presentation
  (swapchain) currently lives only as a standalone example
  ([`examples/vulkan_present`](../examples/vulkan_present)); it is not wired into
  the `Canvas` API yet.

**Why does the pipeline differ from OpenGL at all?** The OpenGL backend is driven
by immediate GL calls issued per command against the current context. The Vulkan
backend instead encodes the same drawing into a backend-neutral draw list and
submits it through the device with its own command buffers and queues. Same Canvas
API and same visual result — different plumbing underneath.

---

## 5. Common tasks

### Draw text

The default backend discovers common system fonts. For deterministic output,
register font files and a fallback chain:

```cpp
canvas->registerFontFace(wsc::FontFace::fromFile(wsc::FontDescriptor("Inter"),
                                                 "assets/fonts/Inter-Regular.ttf"));
canvas->registerFontFace(wsc::FontFace::fromFile(wsc::FontDescriptor("Noto Sans CJK"),
                                                 "assets/fonts/NotoSansCJK-Regular.ttc", 0));

wsc::FontFallbackChain chain("Inter");
chain.addFallbackFamily("Noto Sans CJK");
canvas->setFontFallbackChain(chain);

wsc::Paint text;
text.setFontFamily("Inter");
text.setTextSize(28.0f);
text.setColor(wsc::Color::WHITE);
canvas->drawText("Hello 字体", 40.0f, 80.0f, text);
```

Optional: `-DWHATSCANVAS_ENABLE_FREETYPE_RASTERIZER=ON` (better glyphs) and
`-DWHATSCANVAS_ENABLE_OPENTYPE_SHAPING=ON` (HarfBuzz shaping). Both degrade
gracefully when the dependency is missing.

### Wrap text in a box (with ellipsis)

```cpp
wsc::Paint body;
body.setFontFamily("Inter");
body.setTextSize(18.0f);
body.setColor(wsc::Color::WHITE);
// bounds, line height, max lines, ellipsize, paint
canvas->drawTextBox("A longer paragraph that wraps across lines...",
                    wsc::RectF(24, 24, 240, 160), 24.0f, 4, true, body);
```

### Draw an image

WhatsCanvas takes pixels or encoded bytes (you own file I/O):

```cpp
wsc::Image image;
// From an already-decoded RGBA8 buffer (width * height * 4 bytes):
image.loadFromRGBA(*canvas, pixels, width, height);
// ...or decode PNG/JPG bytes you have read into memory:
// image.loadFromEncodedMemory(*canvas, bytes.data(), static_cast<int>(bytes.size()));

wsc::Paint tint;                       // paint color tints the image
tint.setColor(wsc::Color::WHITE);      // WHITE = original image, untinted
canvas->drawImage(image, 20.0f, 20.0f, tint);
```

> Images are tinted by the paint color; a **black** paint (the default) renders
> the image black. Use `Color::WHITE` for the original image. See also
> `drawImageFit`, `drawImageNinePatch`, `drawImageRounded`, `drawImageTiled`.

### Gradients

```cpp
wsc::Paint linear;
linear.setLinearGradient(0, 0, 256, 0,
    { wsc::Paint::ColorStop(0.0f, wsc::Color::RED),
      wsc::Paint::ColorStop(0.5f, wsc::Color::GREEN),
      wsc::Paint::ColorStop(1.0f, wsc::Color::BLUE) });
canvas->drawRect(wsc::RectF(0, 0, 256, 64), linear);

wsc::Paint radial;
radial.setRadialGradient(128, 128, 96, wsc::Color(255, 200, 40, 255),
                                        wsc::Color(40, 20, 80, 255));
canvas->drawCircle(128, 128, 96, radial);
```

### Drop shadows (true Gaussian blur)

```cpp
wsc::Paint s;
s.setColor(wsc::Color(90, 150, 235, 255));
// blur radius, dx, dy, shadow color
s.setShadowLayer(24.0f, 6.0f, 8.0f, wsc::Color(0, 0, 0, 160));
canvas->drawRoundRect(wsc::RectF(60, 60, 150, 100), 22.0f, s);
```

### Clipping (anti-aliased arbitrary paths)

```cpp
canvas->save();
wsc::Path clip;
clip.addCircle(128, 128, 90);
canvas->clipPath(clip);                 // smooth AA edges
canvas->drawRect(wsc::RectF(0, 0, 256, 256), fill);
canvas->restore();
```

### Transforms and state

```cpp
canvas->save();
canvas->translate(128, 128);
canvas->rotate(0.4f);
canvas->scale(1.5f, 1.5f);
canvas->drawRect(wsc::RectF(-40, -40, 80, 80), fill);
canvas->restore();                      // undoes translate/rotate/scale + clip
```

### Off-screen layers (`saveLayer`)

```cpp
wsc::Paint layerPaint;
layerPaint.setAlpha(128);               // the whole layer composited at 50%
canvas->saveLayer(wsc::RectF(0, 0, 256, 256), layerPaint);
canvas->drawCircle(100, 128, 60, fill);
canvas->drawCircle(156, 128, 60, fill); // overlaps blend inside the layer, then group-composite
canvas->restore();
```

### Dashed strokes

```cpp
wsc::Paint dash;
dash.setStyle(wsc::Paint::Style::STROKE);
dash.setStrokeWidth(4.0f);
dash.setColor(wsc::Color::WHITE);
dash.setDashPathEffect({ 12.0f, 6.0f }, 0.0f);   // on, off intervals + phase
canvas->drawLine(20, 40, 236, 40, dash);
```

### Read the result back

```cpp
std::vector<unsigned char> rgba;
canvas->endFrame();
canvas->readPixelsRGBA(rgba);        // tightly-packed, top-left-origin RGBA8
canvas->savePixelsPPM("frame.ppm");  // or feed `rgba` to your own PNG encoder
```

### Choose where frames go — `setOutputTarget`

A single "output axis" decides where a canvas delivers each frame. Set it once
with `setOutputTarget`, then use one frame loop everywhere:
`beginFrame → draw → endFrame → present`. `present()` swaps/blits for a **Window**
target and is a no-op for the others; read pixels with `readPixelsRGBA`.

| `OutputTarget` | Where the frame goes | Deliver with |
|---|---|---|
| `Offscreen()` *(default)* | canvas-owned image | `readPixelsRGBA` |
| `OffscreenTexture()` | canvas-owned image, usable as a texture (`drawImage`) | `readPixelsRGBA` / as `ITextureSource` |
| `ToWindow(surface)` | an OS window (library owns the swapchain/blit) | `present()` |
| `GLFramebuffer(fbo, w, h)` | a host-owned GL framebuffer (embed) | your engine |
| `VulkanImageTarget(image, fmt, w, h)` | a host-owned `VkImage` (embed) | your engine |

`setOutputTarget` returns `false` when a target is unsupported for the current
backend/platform, so you can fall back. On-screen present is implemented for
**software (Windows GDI + Linux X11)**, **OpenGL (WGL; GLX on Linux)** and
**Vulkan** (Windows, validated).

**Present to a window** (WhatsCanvas does not own the window — you create it and
hand over the native handle):

```cpp
using Backend = wsc::Canvas::Backend;
auto canvas = wsc::Canvas::create(Backend::Software, width, height);   // or Backend::OpenGL / Backend::Vulkan

wsc::NativeSurface surface;
surface.platform = wsc::NativeSurface::Platform::Win32;
surface.window   = /* HWND, e.g. glfwGetWin32Window(window) */;

if (canvas->setOutputTarget(wsc::OutputTarget::ToWindow(surface))) {  // false if unsupported
    while (running) {
        canvas->beginFrame();
        /* draw ... */
        canvas->endFrame();
        canvas->present();                 // swaps/blits to the window; resizeOutput(w,h) on resize
    }
}
```

Runnable demos:
[`software_present`](../examples/software_present),
[`gl_present`](../examples/gl_present),
[`vulkan_canvas_present`](../examples/vulkan_canvas_present).

**Embed into an existing renderer** — draw into *your* GPU target instead of a
window (no `present()`; your engine composites/presents its own target):

```cpp
// OpenGL: your context must be current and Canvas::loadOpenGL called (section 2).
auto canvasOwner = wsc::Canvas::create(wsc::Canvas::Backend::OpenGL, width, height);
wsc::Canvas &canvas = *canvasOwner;
canvas.initializeContext();
canvas.setOutputTarget(wsc::OutputTarget::GLFramebuffer(myFbo, width, height));

while (running) {
    canvas.beginFrame();
    /* draw ... */
    canvas.endFrame();                 // rendered into myFbo; your engine uses/presents it
}
```

```cpp
// Vulkan: allocate an R8G8B8A8_UNORM VkImage (COLOR_ATTACHMENT usage) on the
// canvas's device, obtained via the interop accessors.
auto canvas = wsc::Canvas::create(wsc::Canvas::Backend::Vulkan, width, height);
VkDevice dev = static_cast<VkDevice>(canvas->vulkanDevice());
VkImage  hostImage = /* vkCreateImage(dev, ... R8G8B8A8_UNORM, COLOR_ATTACHMENT ...) + bind memory */;

canvas->setOutputTarget(
    wsc::OutputTarget::VulkanImageTarget(reinterpret_cast<void *>(hostImage),
                                         VK_FORMAT_R8G8B8A8_UNORM, width, height));

while (running) {
    canvas->beginFrame();
    /* draw ... */
    canvas->endFrame();                // hostImage now holds the rendered frame
}
```

See [`tests/VulkanWrapExternalTests.cpp`](../tests/VulkanWrapExternalTests.cpp)
for a complete, runnable Vulkan example (image allocation + readback check).

---

## 6. Verify before shipping

Recommended local checks before publishing an integration:

```bat
ctest -C Release -L unit --output-on-failure
cmd /c scripts\smoke_test.bat
cmd /c scripts\text_pixel_regression.bat
cmd /c scripts\opengles_build_smoke.bat
cmd /c scripts\package_consumer_smoke.bat
```

See `doc/REGRESSION_BASELINES.md` for the baseline policy and
`doc/API_STABILITY.md` for the public API boundary.

---

## 7. Where to go next

- [API Reference](API_REFERENCE.md) — every public symbol.
- [API Stability](API_STABILITY.md) — what is guaranteed stable.
- [Text Feature Matrix](TEXT_FEATURE_MATRIX.md) — text/font capabilities.
- [Vulkan backend status](vulkan-backend-status.md) — enabling Vulkan.
- Runnable examples: [`examples/`](../examples) and [`tests/package_consumer`](../tests/package_consumer).

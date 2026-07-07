# WhatsCanvas Integration Guide

A task-oriented guide for developers who want to **use** WhatsCanvas in their own
project. If you are looking for the full capability catalog, see the
[README](../README.md); if you want the exact public symbols, see the
[API Reference](API_REFERENCE.md).

- **Just want to render something right now?** Start with
  [1. First pixel in 60 seconds](#1-first-pixel-in-60-seconds) — it needs no GPU,
  window, or graphics context.
- **Adding WhatsCanvas to a real project?** Jump to
  [3. Add WhatsCanvas to your build](#3-add-whatscanvas-to-your-build).
- **Choosing a backend?** See [2. Pick a backend](#2-pick-a-backend).

---

## 1. First pixel in 60 seconds

The **software (CPU) backend** is the fastest way to try WhatsCanvas: it needs no
OpenGL, no window, and no graphics context. It renders into memory and you read
the pixels back — ideal for a first test, servers, CI, or thumbnails.

```cpp
#include <wsc/wsc.h>

int main()
{
    // Sized and ready to draw — no GL context, no window.
    auto canvas = wsc::Canvas::createSoftware(256, 256);

    wsc::Paint bg;
    bg.setColor(wsc::Color(18, 20, 24, 255));
    canvas->drawRect(wsc::RectF(0, 0, 256, 256), bg);

    wsc::Paint fill;
    fill.setColor(wsc::Color(40, 120, 240, 255));
    fill.setAntiAlias(true);
    canvas->drawRoundRect(wsc::RectF(40, 40, 176, 176), 24.0f, fill);

    canvas->flush();
    canvas->savePixelsPPM("first.ppm"); // open in an image viewer / convert to PNG
    return 0;
}
```

That is a complete, runnable program. No `initializeContext`, no `loadOpenGL`.

> `createSoftware` returns a `std::unique_ptr<wsc::Canvas>` that is already sized
> and initialized. Use `->` to call methods.

---

## 2. Pick a backend

WhatsCanvas separates the **Canvas API** (what you draw) from the **backend**
(where it renders). Pick the backend that matches your environment:

| Backend | Create with | Needs a GL context / window? | Use when |
| --- | --- | --- | --- |
| **Software (CPU)** | `Canvas::createSoftware(w, h)` | No | Headless, servers, tests, thumbnails, "just works" everywhere |
| **OpenGL** | `Canvas()` + `loadOpenGL(...)` | Yes (you own it) | Desktop apps/games with a window (GLFW, SDL, Qt, your engine) |
| **OpenGL ES** | `WhatsCanvas::OpenGLES` target + `loadOpenGL(...)` | Yes (you own it) | Mobile / embedded GLES 3.0 |
| **Vulkan** (optional) | `Canvas::createVulkan(w, h)` | No (off-screen) | Vulkan pipelines; falls back gracefully when unavailable |

Backend selection is a **link-time** choice (which library target you link) plus a
**runtime** choice (which factory you call). The software backend links no GPU
libraries at all — see [`WhatsCanvas::Software`](#software-only-no-gpu-dependency).

```cpp
// Runtime fallback pattern: prefer Vulkan, else CPU.
std::unique_ptr<wsc::Canvas> canvas =
    wsc::Canvas::isVulkanAvailable() ? wsc::Canvas::createVulkan(256, 256)
                                     : wsc::Canvas::createSoftware(256, 256);
```

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

    wsc::Canvas canvas;
    canvas.setSize(800, 600);
    canvas.initializeContext();

    while (!glfwWindowShouldClose(window)) {
        wsc::Paint p;
        p.setColor(wsc::Color(40, 120, 240, 255));
        p.setAntiAlias(true);
        canvas.drawRoundRect(wsc::RectF(80, 80, 320, 180), 16.0f, p);
        canvas.flush();

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
3. Draw + `canvas.flush()` per frame (with your context current).
4. `canvas.releaseResources()` before tearing down the context. On context loss
   (e.g. Android background), call `releaseResources()` and re-`initializeContext()`.

---

## 3. Add WhatsCanvas to your build

### Option A — `find_package` (recommended for released consumers)

Build a consumable package once:

```sh
# Windows
build.bat --release --package --no-run
# macOS / Linux
./build.sh --release --package --no-run
```

The package lands in `out/package/<Config>/` with `include/`, `lib/`, and
`lib/cmake/WhatsCanvas/`. Then in your project:

```cmake
cmake_minimum_required(VERSION 3.16)
project(MyApp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(WhatsCanvas CONFIG REQUIRED)

add_executable(MyApp main.cpp)
target_link_libraries(MyApp PRIVATE WhatsCanvas::OpenGL)   # or ::Software / ::OpenGLES
```

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/WhatsCanvas/out/package/Release
cmake --build build --config Release
```

A complete minimal consumer lives in
[`tests/package_consumer`](../tests/package_consumer) and is verified in CI.

### Option B — `add_subdirectory` (vendoring the source)

Drop the repository into your tree (or a submodule) and:

```cmake
add_subdirectory(third_party/WhatsCanvas)
target_link_libraries(MyApp PRIVATE WhatsCanvas::OpenGL)
```

### Which target do I link?

| Target | Backend | Extra dependencies |
| --- | --- | --- |
| `WhatsCanvas::Software` | CPU only | **none** (no OpenGL/Vulkan) |
| `WhatsCanvas::OpenGL` | Desktop GL | system OpenGL |
| `WhatsCanvas::OpenGLES` | GLES 3.0 | system GLES |
| `WhatsCanvas::Vulkan`* | Vulkan | Vulkan SDK at build time |

\* Vulkan is opt-in: configure with a Vulkan SDK present. See
[Vulkan backend status](vulkan-backend-status.md).

#### Software-only (no GPU dependency)

To produce a binary that links **no** graphics libraries at all, build the
software target:

```sh
cmake -S . -B build -DWHATSCANVAS_BUILD_OPENGL=OFF -DWHATSCANVAS_BUILD_SOFTWARE=ON
```

This is the right choice for headless services, CI containers, and platforms
without a GPU/driver stack.

---

## 4. Common tasks

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
> the image black. Use `Color::WHITE` for the original image.

### Gradients, shadows, clipping

```cpp
// Radial gradient fill
wsc::Paint g;
g.setRadialGradient(128, 128, 96, wsc::Color(255, 200, 40, 255), wsc::Color(40, 20, 80, 255));
canvas->drawCircle(128, 128, 96, g);

// True Gaussian drop shadow
wsc::Paint s;
s.setColor(wsc::Color(90, 150, 235, 255));
s.setShadowLayer(24.0f, 6.0f, 8.0f, wsc::Color(0, 0, 0, 160));
canvas->drawRoundRect(wsc::RectF(60, 60, 150, 100), 22.0f, s);

// Anti-aliased path clip
canvas->save();
wsc::Path clip; clip.addCircle(128, 128, 90);
canvas->clipPath(clip);
canvas->drawRect(wsc::RectF(0, 0, 256, 256), fill);
canvas->restore();
```

### Read the result back

```cpp
std::vector<unsigned char> rgba;
canvas->flush();
canvas->readPixelsRGBA(rgba);        // tightly-packed top-left-origin RGBA8
canvas->savePixelsPPM("frame.ppm");  // or write your own PNG/encoder
```

---

## 5. Where to go next

- [API Reference](API_REFERENCE.md) — every public symbol.
- [API Stability](API_STABILITY.md) — what is guaranteed stable.
- [Text Feature Matrix](TEXT_FEATURE_MATRIX.md) — text/font capabilities.
- [Getting Started as a Library](GETTING_STARTED_AS_LIBRARY.md) — packaging details.
- [Vulkan backend status](vulkan-backend-status.md) — enabling Vulkan.
- Runnable examples: [`examples/`](../examples) and [`tests/package_consumer`](../tests/package_consumer).

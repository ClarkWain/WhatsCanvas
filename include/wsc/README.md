# WhatsCanvas C++ integration

This directory is the complete public C++ interface for WhatsCanvas. Include
`<wsc/wsc.h>` for the common drawing API, or include individual headers to
reduce compile time. Advanced font providers, detailed rendering statistics,
and logging are intentionally opt-in through `<wsc/FontResolver.h>`,
`<wsc/CanvasStats.h>`, and `<wsc/Log.h>`. WhatsCanvas requires C++17.

## Link the installed package

The installed CMake package carries the include path, compile definitions,
platform frameworks, and transitive libraries required by each renderer. Point
`CMAKE_PREFIX_PATH` at the extracted WhatsCanvas package and select only the
renderer component your application needs:

```cmake
find_package(WhatsCanvas 0.8 CONFIG REQUIRED COMPONENTS Software)
target_link_libraries(my_app PRIVATE WhatsCanvas::Software)
```

Available package targets are:

| Package target | Runtime backends | Host requirement |
| --- | --- | --- |
| `WhatsCanvas::Software` | `Backend::Software` | None |
| `WhatsCanvas::OpenGL` | `Backend::OpenGL`, and optional `Vulkan`/`Metal` support included by that package build | A current GL context for OpenGL only |
| `WhatsCanvas::OpenGLES` | `Backend::OpenGLES` | A current GLES context |

Prefer the imported CMake targets over linking a library filename manually.
They preserve required system libraries and platform frameworks. On Windows,
place the package DLLs from `bin/` beside the executable when using a shared
package.

## First frame: no window or GPU required

```cpp
#include <wsc/wsc.h>

int main()
{
    auto canvas = wsc::Canvas::create(
        wsc::Canvas::Backend::Software, 256, 256);
    if (!canvas) return 1;

    canvas->beginFrame();
    canvas->drawColor(wsc::Color::WHITE);

    wsc::Paint fill;
    fill.setColor(wsc::Color(40, 120, 240));
    canvas->drawRoundRect(wsc::RectF(40, 40, 176, 176), 24, fill);

    canvas->endFrame();
    return canvas->savePixelsPPM("first.ppm") ? 0 : 2;
}
```

`beginFrame()` initializes the backend lazily. Calling `initializeContext()`
explicitly is useful when the application wants initialization failure before
entering its frame loop.

## Frame and lifetime contract

Use a Canvas and the images created for it from one rendering thread. The API
does not promise concurrent access to one Canvas. For OpenGL/OpenGL ES, that
thread must own a current compatible context whenever WhatsCanvas initializes,
draws, presents, or releases GPU resources.

The normal frame sequence is:

```cpp
canvas->beginFrame();  // reset per-frame state and command recording
// draw calls
canvas->endFrame();    // submit; required before readback or texture reuse
canvas->present();     // only needed for a configured window/output target
```

Balance every `save()` or `saveLayer()` with `restore()`. Destroy Images before
their Canvas when practical. For an orderly GL teardown, make the context
current, call `finalizeContext()`, then destroy the Canvas. If the context was
lost and cannot be made current, call `abandonContext()` instead; CPU-side
state remains available for initialization on a replacement context.

## OpenGL and OpenGL ES hosts

Create and make the host context current, provide its procedure loader once,
then create the matching backend:

```cpp
if (!wsc::Canvas::loadOpenGL(myGetProcAddress)) return false;

auto canvas = wsc::Canvas::create(
    wsc::Canvas::Backend::OpenGL, framebufferWidth, framebufferHeight);
if (!canvas || !canvas->initializeContext()) return false;
```

The application owns the context. A host-owned default framebuffer is not
implicitly cleared; clear it explicitly or draw a full-canvas background each
frame. Use `Backend::OpenGLES` with `WhatsCanvas::OpenGLES` on mobile/Web hosts.

## Logical coordinates and high-DPI output

Canvas sizes and output-target sizes are physical pixels. Drawing coordinates
are also physical pixels until a device pixel ratio is set. For a high-DPI
window, pass the physical drawable size and set the content scale:

```cpp
canvas->setSize(drawableWidth, drawableHeight);
canvas->setDevicePixelRatio(contentScale);
// Draw below in logical coordinates.
```

`setDevicePixelRatio()` resets the current transform to its DPR-scaled base.
Call it before scene transforms, normally after resize and before drawing.

## Images and fonts

Image upload functions copy or decode the supplied bytes before returning.
They return `false` for invalid data, dimensions, backend mismatch, or resource
creation failure. Externally wrapped GL/Metal textures remain owned by the
caller and must belong to the Canvas backend/device.

Text uses the portable font backend by default and can use native DirectWrite
or CoreText when present. Register application fonts before selecting them:

```cpp
auto face = wsc::FontFace::fromFile(
    wsc::FontDescriptor("My UI"), "assets/MyUI-Regular.ttf");
if (!canvas->registerFontFace(face)) return false;

wsc::Paint text;
text.setFontFamily("My UI");
text.setTextSize(18);
canvas->drawText("Hello", 20, 36, text);
```

## Failures and diagnostics

Factories and resource operations report expected failures with `nullptr` or
`false`; query `Canvas::isBackendAvailable()` before offering an optional
backend. WhatsCanvas logs warnings and errors to `stderr` by default. Install a
handler to connect diagnostics to the host application:

```cpp
wsc::Log::setHandler([](const wsc::LogMessage &message) {
    myLog(message.category, message.message);
});
```

See the Doxygen comments beside each public declaration for coordinate units,
ownership, failure behavior, backend restrictions, and performance notes.

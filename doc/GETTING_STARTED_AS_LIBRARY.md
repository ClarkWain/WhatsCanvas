# Using WhatsCanvas as a Library

This guide is the shortest path for embedding WhatsCanvas in another C++ project. It focuses on the public package surface instead of repository-internal examples.

## Build a Consumable Package

Windows:

```bat
build.bat --release --package --no-run
```

macOS / Linux:

```sh
./build.sh --release --package --no-run
```

The packaged output is written under `out/package/<Config>/` and contains:

- `include/wsc/`: public headers
- `lib/`: library binaries and CMake package files
- `lib/cmake/WhatsCanvas/`: `find_package` config files

## CMake Consumer

Point `CMAKE_PREFIX_PATH` at the package directory, then consume the exported target:

```cmake
cmake_minimum_required(VERSION 3.16)
project(MyApp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(WhatsCanvas 0.1.11 CONFIG REQUIRED)

add_executable(MyApp main.cpp)
target_link_libraries(MyApp PRIVATE WhatsCanvas::OpenGL)
```

Configure:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/WhatsCanvas/package
cmake --build build --config Release
```

## Minimal OpenGL Use

WhatsCanvas does not create a window or GL context for you. The host application owns the context and must make it current before initializing or drawing.

```cpp
#include <wsc/wsc.h>

int main()
{
    // After creating a platform OpenGL context and making it current:
    wsc::Canvas::loadOpenGL(reinterpret_cast<wsc::Canvas::OpenGLProcAddress>(glfwGetProcAddress));

    wsc::Canvas canvas(800, 600);
    canvas.initializeContext();

    wsc::Paint paint;
    paint.setColor(wsc::Color(40, 120, 240, 255));
    paint.setAntiAlias(true);

    wsc::Paint background;
    background.setColor(wsc::Color(18, 20, 24, 255));
    canvas.drawRect(wsc::RectF(0, 0, 800, 600), background);

    canvas.drawRoundRect(wsc::RectF(80, 80, 320, 180), 16.0f, paint);
    canvas.flush();

    canvas.releaseResources();
    return 0;
}
```

## Text and Fonts

The default text backend can discover common system fonts. For deterministic application output, register explicit font files and set a fallback chain:

```cpp
wsc::FontFace regular = wsc::FontFace::fromFile(
    wsc::FontDescriptor("Inter"),
    "assets/fonts/Inter-Regular.ttf");
wsc::FontFace cjk = wsc::FontFace::fromFile(
    wsc::FontDescriptor("Noto Sans CJK"),
    "assets/fonts/NotoSansCJK-Regular.ttc",
    0);

canvas.registerFontFace(regular);
canvas.registerFontFace(cjk);

wsc::FontFallbackChain chain("Inter");
chain.addFallbackFamily("Noto Sans CJK");
canvas.setFontFallbackChain(chain);

wsc::Paint text;
text.setFontFamily("Inter");
text.setTextSize(28.0f);
text.setColor(wsc::Color::WHITE);
canvas.drawText("Hello 字体", 40.0f, 80.0f, text);
```

Optional CMake switches:

```sh
cmake -S . -B build \
  -DWHATSCANVAS_ENABLE_FREETYPE_RASTERIZER=ON \
  -DWHATSCANVAS_ENABLE_OPENTYPE_SHAPING=ON
```

FreeType improves glyph metrics/rasterization. HarfBuzz enables OpenType shaping. If either dependency is unavailable, WhatsCanvas keeps a fallback path and reports diagnostics where applicable.

## OpenGLES Target

Build with:

```sh
cmake -S . -B build-gles -DWHATSCANVAS_BUILD_OPENGLES=ON
cmake --build build-gles --target WhatsCanvasOpenGLES --config Release
```

Consumers link `WhatsCanvas::OpenGLES` instead of `WhatsCanvas::OpenGL`. The host still owns the EGL/platform context and proc-address loader.

## Verification Before Shipping

Recommended local checks before publishing an integration:

```bat
ctest -C Release -L unit --output-on-failure
cmd /c scripts\smoke_test.bat
cmd /c scripts\text_pixel_regression.bat
cmd /c scripts\opengles_build_smoke.bat
```

Use `doc/REGRESSION_BASELINES.md` for the baseline policy and `doc/API_STABILITY.md` for the public API boundary.

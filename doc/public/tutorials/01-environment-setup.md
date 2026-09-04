# Chapter 1: Environment Setup and First Frame

> Goal of this chapter: set up a WhatsCanvas development environment from scratch, write a first program, render one offscreen frame, and write out an image file.

For the Chinese version, see [`zh/01-environment-setup.md`](./zh/01-environment-setup.md).

---

## 1.1 What Is WhatsCanvas?

WhatsCanvas is a C++17 2D rendering library that sits between NanoVG (lightweight but limited) and Skia (powerful but large). It ships an HTML-Canvas-like API:

- **Canvas** — Drawing surface, manages frame begin/end
- **Paint** — Paint attributes (color, gradient, stroke, ...)
- **Path** — 2D geometric path

It supports 5 render backends: Software (pure CPU), OpenGL, OpenGL ES, Vulkan, and Metal.

---

## 1.2 Getting WhatsCanvas

There are three main options:

### Option 1: Precompiled GitHub Release Package (Recommended for Newcomers)

Download a package for your platform from [Releases](https://github.com/ClarkWain/WhatsCanvas/releases):

**Desktop:**

- Windows: `whatscanvas-win64-release-1.1.0.zip`
- Linux: `whatscanvas-linux-x64-release-1.1.0.zip`
- macOS: `whatscanvas-macos-universal-release-1.1.0.zip`

Desktop package layout after extraction:

```
whatscanvas-win64-release-1.1.0/
├── include/wsc/          # Headers
├── lib/                  # Static / shared libraries
├── bin/                  # DLLs (Windows shared builds)
└── lib/cmake/WhatsCanvas/ # CMake config files
```

**Mobile:**

- Android: `whatscanvas-android-release-1.1.0.aar`
  - Prefab AAR containing public headers and OpenGL ES libraries for `armeabi-v7a`, `arm64-v8a`, and `x86_64`
  - Consume via Gradle; see the [Android Integration Guide](../platforms/ANDROID_INTEGRATION.md)
- iOS: `whatscanvas-ios-release-1.1.0.zip`
  - Static Metal/CoreText XCFramework with an `arm64` device slice and an `arm64`/`x86_64` simulator slice
  - Drag it into the Frameworks group of your Xcode project; see the [iOS Build Notes](../platforms/IOS_BUILD_NOTES.md)

### Option 2: Build from Source

```bash
git clone --recursive https://github.com/ClarkWain/WhatsCanvas.git
cd WhatsCanvas

# Windows (VS 2022)
build.bat --release --package --no-run

# Linux / macOS
sh ./build.sh --release --package --no-run
```

After the build finishes, the install tree is under `out/package/Release/`.

---

## 1.3 Create Your First Project

Create the following project layout:

```
my_first_wsc/
├── CMakeLists.txt
└── main.cpp
```

### CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.16)
project(MyFirstWSC LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find WhatsCanvas
find_package(WhatsCanvas 1.1.0 CONFIG REQUIRED)

add_executable(MyFirstWSC main.cpp)
target_link_libraries(MyFirstWSC PRIVATE WhatsCanvas::Software)
```

### main.cpp

```cpp
#include <wsc/wsc.h>

int main()
{
    // 1. Create a Canvas: Software backend, 256x256
    auto canvas = wsc::Canvas::create(wsc::Canvas::Backend::Software, 256, 256);
    if (!canvas || !canvas->initializeContext()) {
        return 1;
    }

    // 2. Begin a frame
    canvas->beginFrame();

    // 3. Configure a paint: blue fill
    wsc::Paint fill;
    fill.setColor(wsc::Color(40, 120, 240, 255));  // RGBA
    fill.setAntiAlias(true);

    // 4. Draw a rounded rectangle
    canvas->drawRoundRect(wsc::RectF(40, 40, 176, 176), 24.0f, fill);

    // 5. End the frame
    canvas->endFrame();

    // 6. Write out to a PPM file
    return canvas->savePixelsPPM("first.ppm") ? 0 : 1;
}
```

---

## 1.4 Build and Run

```bash
# Configure (point CMAKE_PREFIX_PATH at your WhatsCanvas install)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/path/to/whatscanvas-win64-release-1.1.0

# Build
cmake --build build --config Release

# Run
./build/Release/MyFirstWSC    # Windows
./build/MyFirstWSC            # Linux/macOS
```

Running the executable produces `first.ppm` in the current directory; open it with any viewer that supports PPM to see a blue rounded rectangle.

> **Tip**: On Windows, if you use a shared-build precompiled package, prepend the `bin/` directory to `PATH` before running, or copy the DLLs next to the executable:
> ```bat
> set "PATH=C:\path\to\whatscanvas\bin;%PATH%"
> build\Release\MyFirstWSC.exe
> ```

---

## 1.5 Code Walkthrough

Let us go through the 24 lines one by one:

| Step | Code | Explanation |
|:----:|------|-------------|
| 1 | `Canvas::create(Backend::Software, 256, 256)` | Create an offscreen 256x256 Canvas rendered on the CPU |
| 2 | `canvas->initializeContext()` | Initialize the render context (no GPU required for Software) |
| 3 | `canvas->beginFrame()` | Start recording draw commands for a frame |
| 4 | `fill.setColor(...)` | Set paint color to blue (R=40, G=120, B=240, A=255) |
| 5 | `fill.setAntiAlias(true)` | Enable anti-aliasing for smoother edges |
| 6 | `drawRoundRect(RectF, radius, paint)` | Draw a rounded rectangle with a 24px corner radius |
| 7 | `canvas->endFrame()` | End the frame and execute all draw commands |
| 8 | `savePixelsPPM("first.ppm")` | Read pixels back and save as PPM |

### Frame Lifecycle

WhatsCanvas drawing follows a **frame loop** pattern:

```
beginFrame() → draw commands → endFrame() → [present() or readPixels()]
```

- **Offscreen rendering** (this chapter): after `endFrame()`, call `savePixelsPPM()` or `readPixelsRGBA()` to obtain the pixels
- **Windowed rendering** (Chapter 9): after `endFrame()`, call `present()` to display on screen

---

## 1.6 About the Software Backend

The Software backend is the CPU reference implementation of WhatsCanvas:

- **No GPU required**: no dependency on OpenGL / Vulkan / Metal
- **Deterministic output**: identical input produces identical pixels across machines
- **Use cases**: unit tests, CI environments, offscreen image generation, screenshot comparison
- **Limitation**: slower than GPU backends, not suitable for real-time rendering of large scenes

For learning and validation, the Software backend is the best starting point.

---

## 1.7 Summary

This chapter covered:

- [x] Understanding what WhatsCanvas is and its core concepts
- [x] Getting and configuring the WhatsCanvas library
- [x] Writing your first program and rendering successfully
- [x] The frame lifecycle

**Next chapter**: [Basic Shape Drawing](./02-basic-shapes.md) — learn to draw rectangles, circles, lines, and other primitives.

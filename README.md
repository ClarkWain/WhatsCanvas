# PrismCanvas

PrismCanvas is a small C++17 canvas playground that builds a Skia-like 2D drawing surface on top of OpenGL. The current demo focuses on path tessellation, strokes, gradients, clipping, transforms, image sampling, text primitives, readback, and simple regression hooks. The architecture is intentionally renderer-facing, so Metal and Vulkan backends can be added without changing the Canvas-facing API shape.

## Highlights

- Immediate-mode `Canvas` API for points, lines, paths, rectangles, rounded rectangles, circles, ovals, arcs, images, and text.
- `Paint` state for stroke/fill styles, alpha, gradients, blend modes, image sampling, tile modes, dash effects, corner effects, and text alignment.
- Save/restore, transforms, clipping, saveLayer-style offscreen composition, hit testing, pixel readback, PPM capture, and FNV-1a pixel hashes.
- OpenGL 3.3 backend with GLFW windowing, GLAD loading, GLM math, STB image/text helpers, and Polyline2D stroke meshes.
- Third-party source projects are pulled with Git submodules.

## Repository Name

Recommended GitHub repository name: `PrismCanvas`.

## Requirements

- CMake 3.16 or newer.
- A C++17 compiler.
- Windows: Visual Studio 2022 with the Desktop C++ workload.
- macOS/Linux: OpenGL development libraries and a toolchain supported by GLFW.

## Clone

```bash
git clone --recurse-submodules <repo-url>
cd PrismCanvas
```

If the repository was cloned without submodules:

```bash
git submodule update --init --recursive
```

## Build

Windows:

```bat
build.bat --no-run
build.bat
```

macOS/Linux:

```bash
chmod +x build.sh
./build.sh --no-run
./build.sh
```

The demo target is `PrismCanvasDemo`.

## Third-Party Dependencies

Submodules:

- GLFW: window and OpenGL context creation.
- GLM: matrix and vector math.
- STB: image loading and lightweight demo text generation.
- Polyline2D: stroke mesh generation.

Generated source kept in-tree:

- GLAD: generated OpenGL 3.3 core loader files under `third_party/glad`.

## Debug Hooks

The demo supports a few environment variables for quick rendering checks:

```bash
CPPDEMO_CAPTURE_PPM=build/capture.ppm ./build/PrismCanvasDemo
CPPDEMO_PRINT_PIXEL_HASH=1 ./build/PrismCanvasDemo
CPPDEMO_EXPECT_PIXEL_HASH=<uint64> ./build/PrismCanvasDemo
```

Pixel hashes are exact and can vary by GPU, driver, MSAA behavior, and platform. Treat them as a fast local regression aid rather than a portable golden image format.

## Roadmap

- Extract the Canvas core into a reusable library target.
- Add backend abstraction points for Metal and Vulkan.
- Replace the current lightweight text path with real font metrics, shaping, and glyph atlas rendering.
- Add automated render tests and a small UI layer on top of Canvas.
# WhatsCanvas

English | [中文](README_zh.md)

[![CI](https://github.com/ClarkWain/WhatsCanvas/actions/workflows/cross-platform-validation.yml/badge.svg)](https://github.com/ClarkWain/WhatsCanvas/actions/workflows/cross-platform-validation.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Version](https://img.shields.io/badge/version-0.8.1-informational.svg)](CHANGELOG.md)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C.svg)](CMakeLists.txt)
[![Documentation](https://img.shields.io/badge/docs-online-success.svg)](https://clarkwain.github.io/WhatsCanvas/)

WhatsCanvas is an embeddable 2D rendering library written in C++17, aimed at native applications. It provides an HTML Canvas-style `Canvas` / `Paint` / `Path` API that covers multilingual text, layer filters, image rendering, offscreen rendering, and pixel readback. Unlike a full UI framework, it focuses on core rendering logic and does not ship built-in controls, layout, input events, or accessibility support; it is also not a source-compatible implementation of HTML Canvas.

This project aims to bridge the gap between minimal drawing libraries (such as NanoVG) and heavyweight graphics engines (such as Skia), providing a lightweight alternative that is easy to integrate, understand, and validate.

> **Need a complete UI framework?** [WhatsUI](https://github.com/ClarkWain/WhatsUI) is a real downstream project built on WhatsCanvas. It provides a retained-mode C++17 UI layer with a Fluent 2 design system, controls, layout, input, focus, overlays, deterministic visual tests, and native desktop windows. WhatsUI is designed for portable native desktop UI and currently uses Windows as its primary delivery and validation platform.

![WhatsCanvas image filters showcase](images/image-filter-showcase.png)

> The image above was rendered by the WhatsCanvas desktop OpenGL backend and read directly back from the framebuffer at 1920 × 1080; it is not a design mockup or UI screenshot.

## Is it Right for Your Project?

| Concern | Current Status |
| --- | --- |
| **Applicability** | Custom UIs in native apps and browsers, tool/data interfaces, HUDs, 2D game render layers, offscreen image generation on servers or in test environments. |
| **API & Language** | C++17; the public API is located in `include/wsc/`, with the entry point being `#include <wsc/wsc.h>`. |
| **Render Backends** | OpenGL, pure CPU Software; optional OpenGL ES, Vulkan, and Metal (macOS/iOS). The Web host compiles the OpenGL ES path to WebAssembly/WebGL 2; WebGPU is not yet implemented. |
| **Platform Status** | Windows, Linux, and macOS run continuous builds and unit tests; release packages cover Windows x64, Linux x64, and macOS universal. Android has a three-ABI GLES sample and physical-device checkpoints. iOS has an in-repository Metal/CoreText host validated on simulator and hardware, including orientation, lifecycle, cold-start, API Validation, and 60 fps checkpoints. Web has an Emscripten/WebGL 2 host with automated browser lifecycle, DPR, context-restore, and visual-parity checks. |
| **Text Capabilities** | Font discovery and fallback, CJK/RTL, UAX #9, line breaking and ellipsis, glyph atlas, COLR/CPAL v0 and common COLRv1 paint graphs; FreeType/HarfBuzz serve the portable path, with selectable DirectWrite on Windows and CoreText on Apple platforms. |
| **Integration** | vcpkg overlay port, CMake `find_package`, `add_subdirectory`, or portable installation directories generated from source. |
| **Footprint** | Not header-only. Supports linking only against `WhatsCanvas::Software`, `::OpenGL`, `::OpenGLES`, or Apple-only `::Metal` based on backend; see [Footprint and Dependencies](#footprint-and-dependencies) for reference. |
| **Maturity** | Current version `0.8.1`, still pre-1.0. Public API boundaries, cross-platform CI, pixel regression, package-consumer integration tests, and auditable performance baselines are in place; upgrade and platform risks should still be evaluated against the boundaries described below. |
| **License** | MIT; components in `third_party/` follow their respective licenses. |

**When to Choose WhatsCanvas?**
If you want a unified Canvas-style API for CPU/GPU rendering, multilingual text, and common UI effects, and you value snapshot determinism, pixel-level regression testing, and source readability, WhatsCanvas is a good fit.

**When to Look Elsewhere?**
If your project heavily relies on a ready-made UI control system, requires native WebGPU support, needs strict color management, needs document/PDF generation, involves complex rich-text editing, or requires a mature rendering library already in a long-term ABI-stable release line (1.0+), then WhatsCanvas may not be the right fit today.

## 60 Seconds to Draw the First Frame

The Software backend does not need a window, GL context, or GPU resources bound, which makes it well suited for initial API validation:

```cpp
#include <wsc/wsc.h>

int main()
{
    auto canvas = wsc::Canvas::create(wsc::Canvas::Backend::Software, 256, 256);
    if (!canvas || !canvas->initializeContext()) {
        return 1;
    }

    canvas->beginFrame();

    wsc::Paint fill;
    fill.setColor(wsc::Color(40, 120, 240, 255));
    fill.setAntiAlias(true);
    canvas->drawRoundRect(wsc::RectF(40, 40, 176, 176), 24.0f, fill);

    canvas->endFrame();
    return canvas->savePixelsPPM("first.ppm") ? 0 : 1;
}
```

The exact snippet above is checked in as a standalone CMake project at [`examples/hello_world/`](examples/hello_world/) and is built alongside the tree when demos are enabled.

You can download precompiled packages directly from [Releases](https://github.com/ClarkWain/WhatsCanvas/releases), or generate them locally using the following commands, then link the corresponding target library in your application:

```cmake
cmake_minimum_required(VERSION 3.16)
project(MyApp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
find_package(WhatsCanvas 0.8.1 CONFIG REQUIRED)

add_executable(MyApp main.cpp)
target_link_libraries(MyApp PRIVATE WhatsCanvas::Software)
```

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/path/to/WhatsCanvas/package
cmake --build build --config Release
./build/MyApp
```

Visual Studio multi-config generators typically run from `build\Release\MyApp.exe`. The program writes `first.ppm` to the current working directory; open it with an image viewer that supports PPM, or convert it to PNG. WhatsCanvas offers RGBA readback and PPM debug output, but does not embed PNG/JPEG encoders.

Official Windows packages are shared builds. Before running, add the `bin` directory from the package to your `PATH`, or copy the DLLs next to your application's executable:

```bat
set "PATH=C:\path\to\whatscanvas\bin;%PATH%"
build\Release\MyApp.exe
```

For advanced features like in-window OpenGL, OpenGL ES, Vulkan, font registration, or host render targets, please refer directly to **[Using WhatsCanvas as a Library](doc/GETTING_STARTED_AS_LIBRARY.md)**. This guide covers the complete context lifecycle and provides standalone runnable consumer examples.

## Get and Build

### Using Precompiled Packages

Tagged release assets are named `whatscanvas-<platform>-release-<version>.zip`, e.g., `whatscanvas-win64-release-0.8.1.zip`. The package layout is:

```text
include/wsc/                 Public headers
lib/                         Available rendering libraries
bin/                         Runtime libraries for shared builds (if available)
lib/cmake/WhatsCanvas/       find_package configurations
```

Android tags additionally publish
`whatscanvas-android-demo-profile-<version>.apk`. This three-ABI sample is
debug-signed and optimized for evaluation/profiling; it is not a production AAR
or application signing deliverable. See the [Android Integration Guide](doc/ANDROID_INTEGRATION.md).

The targets provided by the precompiled packages may differ across platforms. In practice, verify the required targets exist via CMake:

```cmake
find_package(WhatsCanvas 0.8.1 CONFIG REQUIRED)
if (NOT TARGET WhatsCanvas::Software)
    message(FATAL_ERROR "This package does not contain the Software backend")
endif()
```

Note that the current precompiled packages are not identically configured across the three major platforms:

| Release Asset | Delivery form and targets | Font/Vulkan Config |
| --- | --- | --- |
| Windows x64 | shared; OpenGL, OpenGL ES, Software | FreeType, HarfBuzz shaping ENABLED; Vulkan option ENABLED (loader/driver/device still required at runtime) |
| Linux x64 | static; OpenGL, Software | FreeType, HarfBuzz shaping ENABLED; Vulkan DISABLED |
| macOS universal | static; OpenGL, Software | FreeType, HarfBuzz shaping ENABLED; Vulkan DISABLED |

The FreeType/HarfBuzz configuration applies to the GL-family targets; `WhatsCanvas::Software` keeps its independent CPU-only delivery and continues to use the bundled `stb_truetype` and simple shaping.

Windows packages are built with the VS 2022 toolchain. For production integration, match the platform, architecture, configuration, and C/C++ runtime; if a different target set or dependency combination is required, build from source.

The exact build parameters for official packages are recorded in the [package-release workflow](.github/workflows/package-release.yml). A local `--package` build uses the defaults described below and therefore does not exactly reproduce the Windows official-package configuration.

### vcpkg

The repository ships a tested overlay port. vcpkg itself is a prerequisite and is not bundled with WhatsCanvas. For example, from a Windows Command Prompt opened in the WhatsCanvas checkout:

```bat
git clone https://github.com/microsoft/vcpkg.git ..\vcpkg
..\vcpkg\bootstrap-vcpkg.bat
..\vcpkg\vcpkg.exe install whatscanvas --overlay-ports=.\ports
```

If `vcpkg` is already on `PATH`, install the default OpenGL, Software, and text feature set with:

```sh
vcpkg install whatscanvas --overlay-ports=./ports
```

For a CPU-only build with no OpenGL, FreeType, or HarfBuzz dependency:

```sh
vcpkg install "whatscanvas[core,software]" --overlay-ports=./ports
```

Then configure your application with the vcpkg toolchain and consume the renderer you need:

```cmake
find_package(WhatsCanvas CONFIG REQUIRED COMPONENTS OpenGL)
target_link_libraries(MyApp PRIVATE WhatsCanvas::OpenGL)
```

Use `COMPONENTS Software` and `WhatsCanvas::Software` for CPU-only rendering. The overlay is usable immediately from this repository; inclusion in the central vcpkg registry is a separate upstream review process.

### Building from Source

Building from source requires CMake 3.16+, a C++17 compiler, and a complete Git submodule environment. The root build process defaults to enabling OpenGL, Software, demos, tests, and benchmarks while outputting static libraries. Moreover, running the OpenGL demo additionally necessitates that the host system has valid GLFW system graphics development libraries pre-installed.

```sh
git clone --recursive https://github.com/ClarkWain/WhatsCanvas.git
cd WhatsCanvas
```

Windows (VS 2022):

```bat
build.bat --no-run
```

macOS / Linux:

```bash
sh ./build.sh --no-run
```

Generate a Release directory structure suitable for `find_package`:

```bat
build.bat --release --package --no-run
```

```bash
sh ./build.sh --release --package --no-run
```

The installation directory is located at `out/package/Release/`. For standard Windows multi-config builds, the demos typically reside in `build/Debug/` or `build/Release/`; the default single-config build in Unix systems is generally found in `build/`.

If you have already cloned the repository, execute `git submodule update --init --recursive` to refresh submodule states. Build scripts also pull missing submodules automatically upon execution, so please ensure network connectivity on the first build; if a fully offline build is necessary, be sure to fetch all submodule codebases in advance. Common system dependencies needed in Ubuntu to compile the demos include `libgl1-mesa-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev`.

Linux installed-font discovery uses Fontconfig when `pkg-config` can find the `fontconfig` development package. Install `pkg-config libfontconfig1-dev` plus at least one readable system font package such as `fonts-dejavu-core` when you want `FontSystem::discoverInstalledFontFaces()` and the default fallback aliases to be active. CI jobs that must fail instead of skipping when discovery is unavailable can run the font tests with `WHATSCANVAS_REQUIRE_SYSTEM_FONT_DISCOVERY=1`.

Package-manager builds can set `WHATSCANVAS_USE_SYSTEM_DEPENDENCIES=ON` to use
registry-provided GLAD, GLM, stb, FreeType, and HarfBuzz. On Linux,
`WHATSCANVAS_X11` controls presentation support explicitly: `AUTO` preserves
source-build discovery, `ON` requires X11, and `OFF` guarantees a headless
build without probing the host for X11.

### As Source Subdirectory

```cmake
set(WHATSCANVAS_BUILD_OPENGL ON CACHE BOOL "")
set(WHATSCANVAS_BUILD_SOFTWARE ON CACHE BOOL "")
set(WHATSCANVAS_BUILD_DEMO OFF CACHE BOOL "")
set(WHATSCANVAS_BUILD_BENCHMARKS OFF CACHE BOOL "")
# WhatsCanvas follows CMake's global BUILD_TESTING option. Set it OFF here only
# if the parent project does not need tests from any subproject.
# set(BUILD_TESTING OFF CACHE BOOL "")
add_subdirectory(third_party/WhatsCanvas)
target_link_libraries(MyApp PRIVATE WhatsCanvas::OpenGL)
```

If you only need CPU rendering without GPU dependencies, you can reduce the build footprint:

```sh
cmake -S . -B build \
  -DWHATSCANVAS_BUILD_OPENGL=OFF \
  -DWHATSCANVAS_BUILD_SOFTWARE=ON \
  -DWHATSCANVAS_BUILD_DEMO=OFF \
  -DWHATSCANVAS_BUILD_BENCHMARKS=OFF \
  -DBUILD_TESTING=OFF
```

## Backends and Platform Boundaries

| Backend | CMake target | Default state | Host requirement | Current boundaries |
| --- | --- | --- | --- | --- |
| **Software** | `WhatsCanvas::Software` | Enabled | No GPU or Graphics API | Deterministic CPU reference implementation, suitable for headless, tests, screenshots, and fallback. |
| **OpenGL 3.3 Core** | `WhatsCanvas::OpenGL` | Enabled, primary cross-platform GL path | App creates the GL context and keeps it current, provides the proc address | Main GL rendering path for desktop applications. |
| **OpenGL ES 3.0** | `WhatsCanvas::OpenGLES` | Disabled | Host EGL/GLES context | Independent target; Linux Mesa executes build and filter pixel gates; mobile devices require host-side verification. |
| **Vulkan** | Built into `WhatsCanvas::OpenGL` | Disabled | Vulkan SDK for source build; loader, driver, and device for running | Offscreen by default; Win32 supports Canvas window presentation; window surfaces for other platforms are still evolving. |
| **Metal** | `WhatsCanvas::Metal` (standalone) or built into `WhatsCanvas::OpenGL` | Standalone target opt-in; OpenGL host enables it on Apple | Metal-capable macOS/iOS/tvOS device | Offscreen rendering, external `MTLTexture` interop, and `CAMetalLayer` presentation without linking OpenGL ES. |

Vulkan is enabled with `-DWHATSCANVAS_ENABLE_VULKAN=ON`. Metal remains available inside the Apple OpenGL target and can also be built independently with `-DWHATSCANVAS_BUILD_METAL=ON -DWHATSCANVAS_BUILD_OPENGL=OFF`, producing `WhatsCanvas::Metal` without a GL dependency.

For OpenGL/OpenGL ES, the application inherently manages the window and context; Software, Vulkan, and Metal require no external GL context. All backends are initialized via `Canvas::create(Backend, width, height)`, which yields `nullptr` upon failure, naturally accommodating graceful fallbacks:

```cpp
using Backend = wsc::Canvas::Backend;
auto canvas = wsc::Canvas::create(
    {Backend::Vulkan, Backend::Metal, Backend::OpenGL, Backend::Software}, width, height);
if (!canvas) {
    return 1;
}
```

Platform Validation Status:

**Testing conventions**: "Unit tests" in the table mainly cover headless logic and contract checks; "Pixel gate" actually starts the corresponding graphics backend and compares pixels against the reference output; "Release package" only means the build, packaging, and package-consumer integration flow succeeded—it does not imply full real-device or window-rendering validation on target hardware.

**Desktop host**: [`platforms/desktop/`](platforms/desktop/README.md) builds `WhatsCanvasDesktopHost`, a GLFW + OpenGL 3.3 host that drives portable scene implementations. It supports interactive, headless PPM dump, and benchmark modes and consumes the same canonical viewport and stress-scene code as Android, iOS, and Web.

| Platform | Automated coverage | Notes |
| --- | --- | --- |
| Windows x64 | MSVC unit tests, package consumption, OpenGL/Software; release matrix can enable GLES, Vulkan, FreeType, HarfBuzz | DirectWrite text backend optional; Vulkan window presentation supports Win32. |
| Linux x64 | GCC build, unit tests, OpenGL/GLES filter pixel gates, package consumption | Automated GL scenarios use Mesa/Xvfb; GLX window presentation from source lacks continuous verification. |
| macOS x86_64/arm64 | Unit tests, Metal pixel/contract gates, and universal release packages | Metal is enabled by default and supports offscreen rendering plus `CAMetalLayer` presentation; system OpenGL remains available. |
| iOS / Android | [iOS UIKit/Metal/CoreText sample](platforms/ios/README.md), iOS lifecycle UI test, [Android GLSurfaceView/JNI sample](platforms/android/README.md), and Android integration guide | The iOS host is simulator-validated in portrait/landscape, background/resume, and cold launch. Android builds three ABIs and has Pixel 3/Redmi K30 checkpoints. Both still require target-hardware validation before shipping. |
| Web | [Emscripten/WebGL 2 host](platforms/wasm/README.md), headless-browser lifecycle/DPR/context-restore checks, and 14 visual-parity captures | Source build; no WebGPU backend or prebuilt release archive yet. |

See [Android Integration](doc/ANDROID_INTEGRATION.md), [Cross-Platform Validation Matrix](doc/CROSS_PLATFORM_VALIDATION_MATRIX.md), [iOS Build Notes](doc/IOS_BUILD_NOTES.md), and [Vulkan Backend Status](doc/vulkan-backend-status.md) for detailed statuses.

## Capability Overview

| Area | Main capabilities | Representative API |
| --- | --- | --- |
| Geometry & Path | Points, lines, rects, rounded rects, circles/ellipses/arcs, curved paths, fill/stroke hit-test, dashes, path effects | `drawPath`, `measureStrokeBounds`, `hitTestPathFill` |
| Paint | Fill/stroke, analytical anti-aliasing, linear/radial multi-stop gradients, 14 blend modes, true Gaussian drop shadow, sampling quality, color matrix | `Paint`, `setBlendMode`, `setShadowLayer` |
| Canvas State | Save/restore, matrix transform, rect/anti-aliased path clipping, offscreen layers, quick reject | `clipPath`, `saveLayer`, `quickReject` |
| Images | PNG/JPEG decoding, raw RGBA, external textures, partial updates, contain/cover, 9-patch, rounded/circular clipping, tiling | `Image`, `drawImageFit`, `wrapExternalTexture` |
| Layer Filters | Content/backdrop blur, inner shadow, frosted glass, saturation/brightness/contrast/grain, color matrix, and offset chain | `ImageFilter`, `ImageFilterChain`, `LayerOptions` |
| Text | System fonts, fallback, weight/slant, CJK/RTL, line breaking/ellipsis, letter spacing, stroked/shadowed/gradient text, text-on-path | `FontManager`, `drawTextBox`, `drawTextOnPath` |
| Output & Interop | Offscreen images, render-target canvas, GL framebuffer, external Vulkan images/Metal textures, sync/async RGBA readback, window present | `OutputTarget`, `wrapExternalTexture`, `readPixelsRGBAAsync`, `present` |
| Diagnostics | Pixel hash/PPM, backend and font diagnostics, render stats, resource and atlas stats | `computePixelsHashRGBA`, `RenderStats` |

### Text Implementation Details

The default cross-platform text pipeline covers UTF-8 layout, UAX #29 extended grapheme clusters, cluster-safe wrapping, font fallback, space-less CJK line breaking, Unicode 17.0.0 bi-directional text support, and source-aware glyph atlas construction. Installed-font snapshots can be refreshed on a live `Canvas` without losing explicitly registered fonts or fallback chains. The archived UAX #9 conformance run covers **861,948 cases, all passing, with 0 skipped and 0 failed**.

Note that bi-directional text processing is more than script-level shaping. Scripts such as Arabic and Indic involve complex glyph substitution and reordering, which rely on HarfBuzz for full support. These features are enabled by default on all GL-family targets; before shipping, check package diagnostics and run regression tests with your actual fonts and content. Disabling the option or falling back to simple shaping because of missing dependencies is not equivalent to full HarfBuzz shaping and should not be treated as one.

- `WHATSCANVAS_ENABLE_FREETYPE_RASTERIZER=ON` (default): Prefers FreeType for glyph lookup, metrics, kerning, and rasterization; falls back to `stb_truetype` if unavailable.
- `WHATSCANVAS_ENABLE_OPENTYPE_SHAPING=ON` (default): Enables HarfBuzz OpenType shaping for the public portable text path, including locale/direction input, global `Paint::setFontFeature` controls, and collection face selection; uses simple shaping + kerning if unavailable or disabled.
- DirectWrite is optional on Windows. Apple builds expose a native CoreText backend for measurement, line breaking, system fallback, features/variations, decoration, and cached grayscale RGBA rasterization.
- COLR/CPAL v0, the common COLRv1 paint graph used by Android Noto Color Emoji,
  and CBLC index-format 1 + CBDT image-format 17 PNG glyphs are supported when
  FreeType is enabled. Other CBDT/CBLC formats, SBIX, SVG, and exact advanced
  COLRv1 composite modes remain subsequent work.

![WhatsCanvas font fallback, CJK, BiDi text, and text-on-path](images/text-rendering-showcase.png)

Check the [Text Feature Matrix](doc/TEXT_FEATURE_MATRIX.md) and [Text Sharpness & HiDPI](doc/TEXT_SHARPNESS_AND_HIDPI.md) for the full feature matrix.

## Performance: Evidence and Applicability

<!-- PERFORMANCE_CLAIM baseline=benchmarks/baselines/nanovg-win-i7-8700-gtx1060/matrix-summary.json wins=26 losses=0 inconclusive=1 quality=27/27 -->

In the currently archived **Windows, Core i7-8700, GTX 1060, 1920 × 1080, Release, OpenGL** quality-matched benchmark matrix in the repository, WhatsCanvas compared to NanoVG GL3 has **26 leads, 0 losses, 1 tie**, with **27 pixel quality verifications passed**.

Audit metadata: Windows 10, NVIDIA 560.94, MSVC 19.43, OpenGL 3.3; warms up 5 frames per process and measures 30 frames, each cell uses 2 ABBA blocks, 4 new processes per end, and 10,000 bootstraps; NanoVG commit is `ce3bf745eb2d2dbc14a50bf2446783f691ac4353`. The matrix was archived on 2026-07-29 at WhatsCanvas commit `0358151`, with quality thresholds and one-click reproduction commands detailed in the baseline README.

| Scenario | Matrix scope | Archived result |
| --- | --- | --- |
| AA Geometry | 256–4,096 shapes; static, dynamic data, dynamic structures | 8 leads, 1 tie; max frame time decreased by 26.7% |
| Images | 64–1,024 images; up to 32 textures; rounded corners and state variations | 9/9 leads; max frame time decreased by 58.5% |
| Dynamic Text | 64–1,024 draws; text, font size, and state variations | 9/9 leads; max frame time decreased by 32.0% |

These numbers only reflect the specific hardware, driver, backend, and workload described above; they should not be extrapolated to other GPUs, the Software backend, the Vulkan backend, mobile devices, or your production environment. The repository keeps per-frame JSONL data, pixel-diff results, ABBA process pairing details, and 95% confidence intervals so the run can be audited and reproduced. Before making architectural decisions, re-run the matrix with workloads that are representative of your own use case.

- [Full Methodology and Results](doc/PERFORMANCE_BENCHMARKS.md)
- [NanoVG Parameter Matrix and Raw Baselines](benchmarks/baselines/nanovg-win-i7-8700-gtx1060/README.md)
- [Cross-Library Benchmark Spec](doc/CROSS_LIBRARY_BENCHMARKS.md)

## Footprint and Dependencies

By "lightweight" we mean that backends can be linked separately, the public API surface is small, and the library does not take over the host application's windowing or event loop—not that it is header-only.

A clean build snapshot recorded for `0.3.0` using **VS 2022 x64, static Release, default FreeType/HarfBuzz enabled** can serve as a historical volume reference:

| Content | File footprint |
| --- | ---: |
| 16 public headers | ~ 74 KiB |
| `WhatsCanvasSoftware.lib` | ~ 4.67 MiB |
| `WhatsCanvasOpenGL.lib` | ~ 7.59 MiB |
| Packaged `freetype.lib` | ~ 1.78 MiB |
| Packaged `harfbuzz.lib` | ~ 4.49 MiB |

The figures above are the raw sizes of the static archives themselves and cannot simply be summed to predict the final executable size delta. Linker dead-code stripping, embedded debug info, LTO level, C/C++ runtime choice, font implementation (FreeType vs. `stb_truetype`), the Vulkan module toggle, and static vs. shared linking all materially affect the shipped size. For an accurate estimate, strip unused targets in your actual toolchain and measure the resulting binary directly.

Dependency model:

- The Software target does not link OpenGL/Vulkan; core image decoding and portable font fallback come from components inside the repository. "Deterministic" here means the repository's fixed implementation and inputs can serve as a regression baseline, with no promise of permanent pixel-for-pixel consistency across different OSs, compilers, or versions.
- OpenGL / OpenGL ES targets require platform graphics libraries; WhatsCanvas does not force the application to use GLFW; GLFW is only used for repository demos and some tests.
- FreeType, HarfBuzz, and Vulkan can all be stripped at build time. Root CMake and `--package` default to FreeType `ON`, HarfBuzz shaping `ON`, and Vulkan `OFF`. For minimal text dependencies, set `WHATSCANVAS_PACKAGE_ENABLE_FREETYPE=0` and disable HarfBuzz via `WHATSCANVAS_CMAKE_EXTRA_ARGS=-DWHATSCANVAS_ENABLE_OPENTYPE_SHAPING=OFF`. Verify CMake cache or package diagnostics before formal release.

## Maturity and Engineering Quality

WhatsCanvas is more than just "able to draw pixels". The engineering and automated checks that ship with the repository include:

- Cross-platform CI across Windows, Linux, and macOS; dedicated builds and pixel gates for OpenGL ES, Vulkan, and Metal.
- Software golden-image baselines, filter parity across OpenGL/OpenGL ES/Vulkan/Metal, strict hash regressions, and fuzzy PPM regressions.
- API reference freshness checks, release/version consistency checks, and package-consumer plus example-build checks.
- Synchronous/asynchronous pixel readback, deterministic first-frame timing, render stats, resource tracking, and reproducible benchmarks.
- Support boundaries for public headers and CMake targets are documented in [API Stability](doc/API_STABILITY.md); release history is in [CHANGELOG](CHANGELOG.md).

Risks to keep in mind:

- The version is still pre-1.0 (`0.8.x`); read the CHANGELOG and run package-consumer tests before upgrading.
- The capability tables in this README are not a parity guarantee for every backend × platform combination; consult the feature matrices for filters, text, and output targets, and validate the combination you actually use.
- Vulkan is opt-in and not the default backend; cross-platform window presentation and broader pixel coverage are still being extended.
- The Android GLSurfaceView/JNI host builds both Arm ABIs plus `x86_64`; Pixel 3 and Redmi K30 checkpoints cover rendering, fonts, lifecycle, and pacing, but broad device coverage and AAR packaging remain open. The iOS Metal/CoreText sample has simulator and device checkpoints, while signing/distribution remain host-owned. The WebAssembly/WebGL 2 host is source-built and browser-tested; WebGPU and a prebuilt Web release archive are not yet available.
- Real-time GPU rendering results may vary with drivers; use Software as the deterministic baseline and use tolerance-based comparison for GPU regressions.
- `Canvas` should be used from within its rendering / context thread; current public documentation does not promise concurrent access to a single instance, and no cross-thread contract is defined for sharing images, fonts, or external textures across Canvas instances.

## Examples

The repository includes a minimal starter, a package consumer, unified
Software/OpenGL/Vulkan/Metal presentation hosts, a WebAssembly/WebGL 2 host,
and two full games. Focused
visual regression harnesses live under `tests/visual` rather than `examples`.

<table>
<tr>
<td width="50%" align="center"><a href="examples/game/tetris"><img src="images/tetris.jpg" alt="WhatsCanvas Tetris example" width="100%"></a><br><b>Tetris</b> — Layouts, text panels, blocks, and state overlays</td>
<td width="50%" align="center"><a href="examples/game/racer"><img src="images/racer.png" alt="WhatsCanvas Racer example" width="100%"></a><br><b>Racer</b> — Scrolling scenes, clipping, HUDs, and animations</td>
</tr>
</table>

To build Tetris separately on Windows:

```bat
cd examples\game\tetris
build.bat --no-run
```

## Verify Your Integration

Run core unit tests from the repository root:

```bat
ctest --test-dir build -C Debug -L unit --output-on-failure
```

```bash
ctest --test-dir build -C Debug -L unit --output-on-failure
```

The command above builds and runs the tests labeled `unit`. Additional higher-level verifications:

```bat
cmd /c scripts\smoke_test.bat
cmd /c scripts\text_pixel_regression.bat
cmd /c scripts\opengles_build_smoke.bat
cmd /c scripts\package_consumer_smoke.bat
cmd /c scripts\release_preflight.bat
```

```bash
sh ./scripts/smoke_test.sh
sh ./scripts/text_pixel_regression.sh
sh ./scripts/opengles_build_smoke.sh
sh ./scripts/package_consumer_smoke.sh
sh ./scripts/release_preflight.sh
```

The pre-release preflight covers API references, versions, unit tests, and package consumers, but does not replace full GPU/visual regressions. Baseline update guidelines are detailed in [Regression Baseline Policy](doc/REGRESSION_BASELINES.md).

## Documentation Navigation

Start from the **[Online Documentation](https://clarkwain.github.io/WhatsCanvas/)** or proceed via these entry points:

| Purpose | Documentation |
| --- | --- |
| First-time integration | [Using WhatsCanvas as a Library](doc/GETTING_STARTED_AS_LIBRARY.md) |
| Android host integration | [Android Integration Guide](doc/ANDROID_INTEGRATION.md) |
| Look up APIs | [Public API Reference](doc/API_REFERENCE.md) · [Visual API Gallery](doc/visual-api-gallery.md) |
| Evaluate API stability | [API Stability](doc/API_STABILITY.md) · [CHANGELOG](CHANGELOG.md) |
| Text and fonts | [Text Feature Matrix](doc/TEXT_FEATURE_MATRIX.md) · [Web / Async Font Integration](doc/WEB_FONT_INTEGRATION.md) · [Font Discovery Design](doc/WHATS_CANVAS_VS_FLUTTER_FONT_DISCOVERY.md) · [DirectWrite](doc/DIRECTWRITE_TEXT_BACKEND.md) |
| Layer effects | [Image Filters](doc/IMAGE_FILTERS.md) · [Shadow Model](doc/SHADOW_MODEL.md) · [Blend Modes](doc/BLEND_MODE_AUDIT.md) |
| Backends and platforms | [Vulkan Status](doc/vulkan-backend-status.md) · [Shader Portability](doc/SHADER_PORTABILITY.md) · [Troubleshooting](doc/TROUBLESHOOTING.md) |
| Performance and validation | [Performance Benchmarks](doc/PERFORMANCE_BENCHMARKS.md) · [Visual Regression](doc/VISUAL_REGRESSION.md) |
| Architecture & Contributing | [Architecture](doc/architecture/README.md) · [Contributing](CONTRIBUTING.md) |

## Roadmap

WhatsCanvas is currently focused on cross-backend pixel consistency, text rendering quality, broader Vulkan, Web, and device coverage, and more reproducible performance benchmarks. Longer-term directions include WebGPU, prebuilt Web distribution, additional CBDT/CBLC bitmap formats, SBIX, SVG, and full COLRv1 compositing. These directions are still in planning and should not be treated as available features today.

## License

WhatsCanvas is released under the [MIT License](LICENSE). Third-party components like FreeType, HarfBuzz, GLFW, and stb follow their respective licenses.

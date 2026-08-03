# WhatsCanvas

English | [中文](README_zh.md)

[![CI](https://github.com/ClarkWain/WhatsCanvas/actions/workflows/cross-platform-validation.yml/badge.svg)](https://github.com/ClarkWain/WhatsCanvas/actions/workflows/cross-platform-validation.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Version](https://img.shields.io/badge/version-0.1.19-informational.svg)](CHANGELOG.md)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C.svg)](CMakeLists.txt)
[![Documentation](https://img.shields.io/badge/docs-online-success.svg)](https://clarkwain.github.io/WhatsCanvas/)

WhatsCanvas is an embeddable 2D rendering library written in C++17, designed specifically for native applications. It provides an HTML Canvas-style `Canvas` / `Paint` / `Path` API, with comprehensive support for multilingual text, layer filters, image rendering, offscreen rendering, and pixel readback. Distinct from complete UI frameworks, it focuses on core rendering logic, without built-in controls, layouts, input events, or accessibility support, nor is it a source-compatible implementation of HTML Canvas.

This project aims to bridge the gap between "minimalist base drawing libraries" and "heavyweight graphics engines (like Skia)", providing a lightweight alternative that is easy to integrate, understand, and validate.

![WhatsCanvas rendering showcase](images/image-filter-showcase.png)

> The image above is a `1920 × 1080` frame rendered by the WhatsCanvas desktop OpenGL backend and read directly back from the framebuffer, not a design mockup or UI screenshot.

## Is it Right for Your Project?

| Concern | Current Status |
| --- | --- |
| **Applicability** | Custom UIs in native apps, tool/data interfaces, HUDs, 2D game render layers, offscreen image generation on servers or in test environments. |
| **API & Language** | C++17; the public API is located in `include/wsc/`, with the entry point being `#include <wsc/wsc.h>`. |
| **Render Backends** | OpenGL, pure CPU Software; optional OpenGL ES and Vulkan. Metal / WebGPU are not yet implemented. |
| **Platform Status** | Windows, Linux, and macOS run continuous builds and unit tests; release packages cover Windows x64, Linux x64, and macOS universal. Mobile integration is currently primarily through OpenGL ES hosts, which does not equal complete device matrix coverage. |
| **Text Capabilities** | Font discovery and fallback, CJK/RTL, UAX #9, line breaking and ellipsis, glyph atlas, COLR/CPAL v0; FreeType and HarfBuzz shaping are enabled by default for OpenGL/OpenGL ES. |
| **Integration** | CMake `find_package`, `add_subdirectory`, or generating portable installation directories from source. |
| **Footprint** | Not header-only. Supports linking only against `WhatsCanvas::Software`, `::OpenGL`, or `::OpenGLES` based on backend; see [Footprint and Dependencies](#footprint-and-dependencies) for reference. |
| **Maturity** | Current version `0.1.19`, still pre-1.0; public API boundaries, cross-platform CI, pixel regression, package consumption tests, and auditable performance baselines have been established. Upgrade and platform risks should still be evaluated against the boundaries below. |
| **License** | MIT; components in `third_party/` follow their respective licenses. |

**When to Choose WhatsCanvas?**
If you want to use a unified Canvas-style API for CPU/GPU rendering, multilingual text, and common UI effects, and value snapshot determinism, pixel-level regression testing, and source code readability, WhatsCanvas is an excellent choice.

**When to Look Elsewhere?**
If your project heavily relies on ready-made UI control systems, needs to run in a browser, demands native support for Metal / WebGPU, requires highly comprehensive color management, needs document/PDF generation, involves complex rich text editing, or insists on using old rendering libraries with long-term ABI stability, then WhatsCanvas might not meet your needs at this time.

## 60 Seconds to draw the first frame

The Software backend doesn't need to be bound to a window, a GL context, or GPU resources, making it perfect for initial API validation:

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

You can download precompiled packages directly from [Releases](https://github.com/ClarkWain/WhatsCanvas/releases), or generate them locally using the following commands, then link the corresponding target library in your application:

```cmake
cmake_minimum_required(VERSION 3.16)
project(MyApp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
find_package(WhatsCanvas 0.1.19 CONFIG REQUIRED)

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

Tagged release assets are named `whatscanvas-<platform>-release-<version>.zip`, e.g., `whatscanvas-win64-release-0.1.19.zip`. The package contains:

```text
include/wsc/                 Public headers
lib/                         Available rendering libraries
bin/                         Runtime libraries for shared builds (if available)
lib/cmake/WhatsCanvas/       find_package configurations
```

The modules provided by the precompiled packages may differ across platforms. In actual use, it's recommended to strictly check through CMake if the required targets exist:

```cmake
find_package(WhatsCanvas 0.1.19 CONFIG REQUIRED)
if (NOT TARGET WhatsCanvas::Software)
    message(FATAL_ERROR "This package does not contain the Software backend")
endif()
```

Note that the precompiled packages currently released do not offer completely identical "unified" distributions across the three major platforms:

| Release Asset | Delivery form and targets | Font/Vulkan Config |
| --- | --- | --- |
| Windows x64 | shared; OpenGL, OpenGL ES, Software | FreeType, HarfBuzz shaping ENABLED; Vulkan option ENABLED (loader/driver/device still required at runtime) |
| Linux x64 | static; OpenGL, Software | FreeType, HarfBuzz shaping ENABLED; Vulkan DISABLED |
| macOS universal | static; OpenGL, Software | FreeType, HarfBuzz shaping ENABLED; Vulkan DISABLED |

The FreeType/HarfBuzz configurations apply to GL-family targets; `WhatsCanvas::Software` continues to use the built-in `stb_truetype` and simple shaping to maintain its independent CPU-only delivery.

Windows packages are compiled using the VS 2022 toolchain. Formal integration should precisely align the platform, architecture, configurations, and C/C++ runtime; if different targets or combinations of dependencies are required, a build from source is highly recommended.

The exact build parameters for official Windows packages are documented in the [package-release workflow](.github/workflows/package-release.yml); performing a local `--package` build will adopt the default settings listed below and won't blindly mimic the full configuration of the official Windows release.

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
| **OpenGL 3.3 Core** | `WhatsCanvas::OpenGL` | Enabled, primary GPU path | App creates and keeps GL context current, injects proc address | Main real-time rendering path for desktop applications. |
| **OpenGL ES 3.0** | `WhatsCanvas::OpenGLES` | Disabled | Host EGL/GLES context | Independent target; Linux Mesa executes build and filter pixel gates; mobile devices require host-side verification. |
| **Vulkan** | Built into `WhatsCanvas::OpenGL` | Disabled | Vulkan SDK for source build; loader, driver, and device for running | Offscreen by default; Win32 supports Canvas window presentation; window surfaces for other platforms are still evolving. |

Vulkan is enabled with `-DWHATSCANVAS_ENABLE_VULKAN=ON`. It does not have an independent package target currently: the code is compiled into `WhatsCanvas::OpenGL`, which still declares system OpenGL dependencies, using `Backend::Vulkan` to select the device at runtime.

For OpenGL/OpenGL ES, the application inherently manages the window and context; Software and offscreen Vulkan operations require no GL context. All backends are initialized via `Canvas::create(Backend, width, height)`, which yields `nullptr` upon failure, naturally accommodating graceful fallbacks:

```cpp
using Backend = wsc::Canvas::Backend;
auto canvas = wsc::Canvas::create(
    {Backend::Vulkan, Backend::OpenGL, Backend::Software}, width, height);
if (!canvas) {
    return 1;
}
```

Platform Validation Status:

**Testing Conventions**: "Unit tests" in the table primarily focus on headless environment logic and contract verification; "Pixel gate" practically runs the respective graphics backend and matches it against reference outputs; the "Release package" just reflects successfully completing the compilation, packaging, and consumer debugging pipeline—it is not equivalent to comprehensive evaluation against genuine embedded setups or real window rendering workloads on designated devices.

| Platform | Automated coverage | Notes |
| --- | --- | --- |
| Windows x64 | MSVC unit tests, package consumption, OpenGL/Software; release matrix can enable GLES, Vulkan, FreeType, HarfBuzz | DirectWrite text backend optional; Vulkan window presentation supports Win32. |
| Linux x64 | GCC build, unit tests, OpenGL/GLES filter pixel gates, package consumption | Automated GL scenarios use Mesa/Xvfb; GLX window presentation from source lacks continuous verification. |
| macOS x86_64/arm64 | Unit tests and universal release packages | Uses system OpenGL; Metal rendering backend is not yet implemented. |
| iOS / Android | OpenGL ES target and iOS integration notes | A regular HIL CI pipeline is absent; verification on target devices is vehemently recommended before embedding. |
| Web | Not supported | WebAssembly / WebGL 2 bridging is still planned. |

See [Cross-Platform Validation Matrix](doc/CROSS_PLATFORM_VALIDATION_MATRIX.md), [iOS Build Notes](doc/IOS_BUILD_NOTES.md), and [Vulkan Backend Status](doc/vulkan-backend-status.md) for detailed statuses.

## Capability Overview

| Area | Main capabilities | Representative API |
| --- | --- | --- |
| Geometry & Path | Points, lines, rects, rounded rects, circles/ellipses/arcs, curved paths, fill/stroke hit-test, dashes, path effects | `drawPath`, `measureStrokeBounds`, `hitTestPathFill` |
| Paint | Fill/stroke, analytical anti-aliasing, linear/radial multi-stop gradients, 14 blend modes, true Gaussian drop shadow, sampling quality, color matrix | `Paint`, `setBlendMode`, `setShadowLayer` |
| Canvas State | Save/restore, matrix transform, rect/anti-aliased path clipping, offscreen layers, quick reject | `clipPath`, `saveLayer`, `quickReject` |
| Images | PNG/JPEG decoding, raw RGBA, external textures, partial updates, contain/cover, 9-patch, rounded/circular clipping, tiling | `Image`, `drawImageFit`, `wrapExternalTexture` |
| Layer Filters | Content/backdrop blur, inner shadow, frosted glass, saturation/brightness/contrast/grain, color matrix, and offset chain | `ImageFilter`, `ImageFilterChain`, `LayerOptions` |
| Text | System fonts, fallback, weight/slant, CJK/RTL, line breaking/ellipsis, letter spacing, stroked/shadowed/gradient text, text-on-path | `FontManager`, `drawTextBox`, `drawTextOnPath` |
| Output & Interop | Offscreen images, render-target canvas, GL framebuffer, external Vulkan image, sync/async RGBA readback, window present | `OutputTarget`, `readPixelsRGBAAsync`, `present` |
| Diagnostics | Pixel hash/PPM, backend and font diagnostics, render stats, resource and atlas stats | `computePixelsHashRGBA`, `RenderStats` |

### Text Implementation Details

The default cross-platform text processing architecture encompasses UTF-8 layout, font fallback, space-less CJK line breaks, Unicode 17.0.0 bi-directional text support, and glyph atlas building. To date, the archived UAX #9 full conformity test result boasts an impressive **861,948 exact matches, 0 skips, and 0 failures**.

It’s crucial to note that bi-directional text processing involves much more than trivial script-shaping. For languages spanning Arabic, Indic, and other demanding phonograms populated by intricate ligature substitutions, the rendering motor leans heavily on HarfBuzz. While typically illuminated by default within GL-family targets, users are vehemently advised to diagnose the internal properties upon booting, alongside conducting diligent regression trials with accurate business materials prior to launch. Any haphazard disabling of this component or degradation to simple shaping workflows should categorically not be treated as a viable equivalent in real-world typographical precision.

- `WHATSCANVAS_ENABLE_FREETYPE_RASTERIZER=ON` (default): Prefers FreeType for glyph lookup, metrics, kerning, and rasterization; falls back to `stb_truetype` if unavailable.
- `WHATSCANVAS_ENABLE_OPENTYPE_SHAPING=ON` (default): Enables HarfBuzz OpenType shaping; uses simple shaping + kerning if unavailable or disabled.
- DirectWrite adapter is optional on Windows; CoreText adapter is not yet implemented.
- COLR/CPAL v0 is supported; CBDT/CBLC, SBIX, SVG, and COLR v1 paint graphs remain subsequent endeavors.

![WhatsCanvas font fallback, CJK, BiDi text, and text-on-path](images/text-rendering-showcase.png)

Check the [Text Feature Matrix](doc/TEXT_FEATURE_MATRIX.md) and [Text Sharpness & HiDPI](doc/TEXT_SHARPNESS_AND_HIDPI.md) for full contracts.

## Performance: Evidence and Applicability

<!-- PERFORMANCE_CLAIM baseline=benchmarks/baselines/nanovg-win-i7-8700-gtx1060/matrix-summary.json wins=26 losses=0 inconclusive=1 quality=27/27 -->

In the currently archived **Windows, Core i7-8700, GTX 1060, 1920 × 1080, Release, OpenGL** identical-quality matrix in the repository, WhatsCanvas compared to NanoVG GL3 has **26 leads, 0 losses, 1 tie**, with **27 pixel quality verifications passed**.

Audit metadata: Windows 10, NVIDIA 560.94, MSVC 19.43, OpenGL 3.3; warms up 5 frames per process and measures 30 frames, each cell uses 2 ABBA blocks, 4 new processes per end, and 10,000 bootstraps; NanoVG commit is `ce3bf745eb2d2dbc14a50bf2446783f691ac4353`. The matrix was archived on 2026-07-29 at WhatsCanvas commit `0358151`, with quality thresholds and one-click reproduction commands detailed in the baseline README.

| Scenario | Matrix scope | Archived result |
| --- | --- | --- |
| AA Geometry | 256–4,096 shapes; stable, dynamic data, dynamic structures | 8 leads, 1 tie; max frame time decreased by 26.7% |
| Images | 64–1,024 images; up to 32 textures; rounded corners and state variations | 9/9 leads; max frame time decreased by 58.5% |
| Dynamic Text | 64–1,024 draws; text, font size, and state variations | 9/9 leads; max frame time decreased by 32.0% |

We emphasize that the performance telemetry documented here faithfully mirrors outcomes tied to a strictly defined hardware blueprint, driver set, backend, and prescribed workloads. Users should sternly avoid arbitrarily projecting these yields onto disparate GPU topologies, pure software pipelines, the Vulkan branch, myriad mobile architectures, and naturally, their authentic production environment. To sustain analytical rigor, the repository meticulously preserves untampered diagnostics spanning granular JSONL summaries framing every frame, visual degradation analyses, explicit ABBA process pairing mechanics, and mathematically grounded 95% confidence intervals metric trails. It is firmly urged to subject the library to a representative real-world payload profiling prior to sealing architectural choices.

- [Full Methodology and Results](doc/PERFORMANCE_BENCHMARKS.md)
- [NanoVG Parameter Matrix and Raw Baselines](benchmarks/baselines/nanovg-win-i7-8700-gtx1060/README.md)
- [Cross-Library Benchmark Contracts](doc/CROSS_LIBRARY_BENCHMARKS.md)

## Footprint and Dependencies

When we employ the term "lightweight" in our narrative, its substantive focus is pinned firmly onto highly versatile decoupled backends, a meticulously tightened macroscopic library interface, and an unswerving discipline of abstaining from forcefully usurping the application's prevailing window management anatomy—it is imperative not to conflate this architecture with simple "header-only" constructs.

A clean build snapshot of the current repository `0.1.19` using **VS 2022 x64, static Release, default FreeType/HarfBuzz enabled** can serve as a volume reference:

| Content | File footprint |
| --- | ---: |
| 16 public headers | ~ 74 KiB |
| `WhatsCanvasSoftware.lib` | ~ 4.67 MiB |
| `WhatsCanvasOpenGL.lib` | ~ 7.59 MiB |
| Packaged `freetype.lib` | ~ 1.78 MiB |
| Packaged `harfbuzz.lib` | ~ 4.49 MiB |

Note that the figures itemized above solely articulate the bare byte-scales of foundational static archives and vehemently cannot be blindly summed up to compute the conclusive executable asset payload. Intrinsically, complex build engineering permutations—be it erudite linker dead-code stripping, diagnostic symbol generation protocols, LTO (Link Time Optimization) escalations, dialectal font logic assimilation, the toggle state of the Vulkan subsystem, and the definitive stance delineating dynamic verses static bindings—will ubiquitously govern your concluding installation payload dimensions. Before engaging in meticulous volume evaluation endeavors, we earnestly advise narrowing compile scopes iteratively within your functional toolchain to exclusively curate the required targets, deriving your decisive footprint strictly bound to your definitive binary output.

Dependency model:

- The Software target does not link OpenGL/Vulkan; core image decoding and portable font fallback come from components inside the repository. "Deterministic" here means the repository's fixed implementation and inputs can serve as a regression baseline, with no promise of permanent pixel-for-pixel consistency across different OSs, compilers, or versions.
- OpenGL / OpenGL ES targets require platform graphics libraries; WhatsCanvas does not force the application to use GLFW; GLFW is only used for repository demos and some tests.
- FreeType, HarfBuzz, and Vulkan can all be stripped at build time. Root CMake and `--package` default to FreeType `ON`, HarfBuzz shaping `ON`, and Vulkan `OFF`. For minimal text dependencies, set `WHATSCANVAS_PACKAGE_ENABLE_FREETYPE=0` and disable HarfBuzz via `WHATSCANVAS_CMAKE_EXTRA_ARGS=-DWHATSCANVAS_ENABLE_OPENTYPE_SHAPING=OFF`. Verify CMake cache or package diagnostics before formal release.

## Maturity and Engineering Quality

WhatsCanvas' boundary pushes radically further than merely "getting designs visibly painted onto screens." The repository is solidly backed by an exceptionally rigorous automation fortification grid encompassing:

- Cross-platform CI across Windows, Linux, and macOS; dedicated builds/pixel gates for OpenGL ES and Vulkan.
- Software golden image workflows, rigorous filter parity across OpenGL/OpenGL ES/Vulkan environments, alongside stringent hash and mathematically fuzzed PPM visual regressions testing protocols.
- Verifiable API reference freshness mappings, uncompromised release artifact consistency checks, and unyielding validation tests centering install-package consumer builds.
- Dedicated synchronous/asynchronous pixel readbacks, deterministic fixed timing protocols targeting first frames, cohesive resource analytics tracking inclusive render statistics, and thoroughly reproducible benchmarks tests.
- Support thresholds correlating API headers to CMake targets transparently mapped inside [API Stability](doc/API_STABILITY.md), with iteration archives in [CHANGELOG](CHANGELOG.md).

Risks still to be defined:

- The version remains `0.1.x`; read the CHANGELOG and run consumer tests before upgrading.
- The capability table in the README is not a sweeping parity promise for every backend × platform configuration; check specific feature matrices for filters, text, and output targets to authenticate your chosen stack.
- Vulkan remains an opt-in non-default backend; comprehensive window-presentation surface coverage alongside grander cross-platform pixel equivalency bounds are progressively widening.
- Native implementations centering Metal, WebGPU, and WebAssembly lack realization. CoreText mapping logic correspondingly demands future development cycles.
- Real-time renders rendered via active GPUs could suffer driver fluctuations; the Software rendering mechanism remains the preferred deterministic benchmark, with visual tolerance algorithms applied broadly when analyzing GPU output boundaries.
- Context-coupled objects native to the `Canvas` hierarchy should reside within confined rendering threads; as it stands, active technical manifests refuse multi-thread instance operations and presently lack robust safety architectures catering strictly to sharing raw font resources, graphical assets, or texture instances between decoupled runtime threads.

## Examples

The repository includes a root demo, API snippets, package consumers, Software/OpenGL/Vulkan present examples, and two full games:

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

Unit items in CTest map to corresponding test targets exactly. Popular extended verifications encapsulate:

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
| Look up APIs | [Public API Reference](doc/API_REFERENCE.md) · [Visual API Gallery](doc/visual-api-gallery.md) |
| Evaluate API stability | [API Stability](doc/API_STABILITY.md) · [CHANGELOG](CHANGELOG.md) |
| Text and fonts | [Text Feature Matrix](doc/TEXT_FEATURE_MATRIX.md) · [DirectWrite](doc/DIRECTWRITE_TEXT_BACKEND.md) |
| Layer effects | [Image Filters](doc/IMAGE_FILTERS.md) · [Shadow Model](doc/SHADOW_MODEL.md) · [Blend Modes](doc/BLEND_MODE_AUDIT.md) |
| Backends and platforms | [Vulkan Status](doc/vulkan-backend-status.md) · [Shader Portability](doc/SHADER_PORTABILITY.md) · [Troubleshooting](doc/TROUBLESHOOTING.md) |
| Performance and validation | [Performance Benchmarks](doc/PERFORMANCE_BENCHMARKS.md) · [Visual Regression](doc/VISUAL_REGRESSION.md) |
| Architecture & Contributing | [Architecture](doc/architecture/README.md) · [Contributing](CONTRIBUTING.md) |

## Roadmap Boundaries

WhatsCanvas presently directs its most aggressive developmental maneuvers into four foundational tenets: exhaustive pixel-for-pixel consensus polishing amongst dynamic operating backends, an unyielding qualitative climb regarding rich typography synthesis, assertive expansions maximizing Vulkan's deployability bounds, and a profound reinforcement surrounding irrefutably reproducible benchmarking pipelines. Looking steadily towards the horizon, aspirations encompassing assimilation drives into the WebAssembly / WebGL 2 habitats, organic architectural adaptations linking Metal and WebGPU engines, together with harnessing dramatically augmented color glyph format palettes, uniformly nestle inside our active long term deployment strategies. Be unequivocally aware that these proactive milestones remain firmly pinned into a speculative capacity planning phase and expressly must not be mistakenly perceived as readily executable tooling implementations.

## License

WhatsCanvas is released under the [MIT License](LICENSE). Third-party components like FreeType, HarfBuzz, GLFW, stb, and polyline2d follow their respective licenses.

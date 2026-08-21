# WhatsCanvas Desktop

This directory contains the desktop host for WhatsCanvas. It builds a single
executable, `WhatsCanvasDesktopHost`, that opens a GLFW window, creates a
WhatsCanvas OpenGL 3.3 core-profile context, and runs any registered
[`IScene`](src/IScene.h) through the public `wsc::Canvas` API. The same Scene
interface will be shared by the Android, iOS and Web hosts, so all four
platforms display and can regression-test the same visual content.

## Scope

- OpenGL 3.3 core-profile rendering through `WhatsCanvas::OpenGL`.
- One GLFW-based host implementation (`hosts/glfw/GlfwHost`) that handles
  window/context creation, high-DPI content-scale, resize, VSYNC and orderly
  teardown.
- A scene registry (`SceneCatalog`) that dispatches by name.
- One bundled scene: `feature_showcase` — the same 8-card feature matrix that
  the Android host draws (text, path, clip, arcs, transform, shadow, image,
  motion), including the retained-`Picture` + dynamic-overlay split.
- Optional headless dump mode that renders N frames to an off-screen framebuffer
  and writes a PPM, suitable for pixel-regression golden comparison.
- Native text selection on supported desktop systems: CoreText on macOS and
  DirectWrite on Windows, with the portable text backend on other hosts.

The top-level CTest suite includes a Software-backed desktop smoke test. Native
OpenGL window and driver validation still require a machine with a display.
Additional hosts (Cocoa+Metal, Win32 native) and scenes are planned as
follow-up work.

## Build

The host is built together with the top-level tree via CMake when
`WhatsCanvas::OpenGL` is available:

```powershell
cmake -S . -B build
cmake --build build --target WhatsCanvasDesktopHost --config Release
```

To skip it, configure with `-DWHATSCANVAS_BUILD_DESKTOP_PLATFORM=OFF`.

The executable is written under `build/<Config>/WhatsCanvasDesktopHost.exe`
(Windows) or `build/<Config>/WhatsCanvasDesktopHost` (Linux/macOS).

## Run

```powershell
# Interactive window with the default 8-card scene:
WhatsCanvasDesktopHost

# Explicit scene / size:
WhatsCanvasDesktopHost --scene=feature_showcase --w=1600 --h=900

# Enumerate registered scenes:
WhatsCanvasDesktopHost --list-scenes

# Headless: render one frame, write PPM (useful for CI diff):
WhatsCanvasDesktopHost --scene=feature_showcase --w=1280 --h=720 `
    --dump-png=out.ppm --frames=1

# Deterministic visual-parity frame:
WhatsCanvasDesktopHost --scene=feature_showcase --w=786 --h=377 --dpr=3 `
    --dump-png=feature_showcase.ppm --time=1.25
```

On macOS, interactive windows use the Retina framebuffer and map Canvas
coordinates through the display scale. Dump and benchmark dimensions are
physical pixels, so `--w=1280 --h=720` always produces and measures a
1280 x 720 framebuffer rather than a 2x backing store.

The feature showcase uses the Android demo's measured logical viewports
(786 x 377 landscape and 393 x 759 portrait) as reference canvases. Each
reference canvas is aspect-fitted, horizontally centered and scaled as one
unit. Text, strokes, radii, spacing and card geometry therefore retain the
same proportions instead of independently reflowing at desktop window sizes.
See [RENDERING_PARITY.md](RENDERING_PARITY.md) for the diagnosis and validation
record.
The multi-platform contract and new-scene workflow are documented in
[VISUAL_PARITY.md](../../docs/VISUAL_PARITY.md).
Generate the complete DPR 3 reference set for the shared matrix with:

```sh
platforms/desktop/capture_visual_parity.sh \
  --host <build-directory>/WhatsCanvasDesktopHost
```

## Adding a new scene

1. Create `src/scenes/YourScene.h/.cpp` implementing `IScene`.
2. Register it in [`src/SceneCatalog.cpp`](src/SceneCatalog.cpp).
3. Add the source to `CMakeLists.txt`.

The Scene interface (`onCanvasReady`, `onLayout`, `onFrame`, `onCanvasReleasing`)
is deliberately platform-agnostic; the same file is intended to be linked into
the Android/iOS/Web hosts once a `platforms/shared/scenes/` extraction lands.

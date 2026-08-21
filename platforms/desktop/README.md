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
- Four bundled scenes: the retained `feature_showcase` plus shared
  `text_stress`, `geometry_stress` and `compositing_stress` regressions.
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
WhatsCanvasDesktopHost --scene=feature_showcase --w=800 --h=400 --dpr=3 `
    --dump-png=feature_showcase.ppm --time=1.25

# Exercise a responsive layout standard outside the primary pixel gate:
WhatsCanvasDesktopHost --scene=feature_showcase `
    --viewport-standard=tablet_4_3 --w=768 --h=1024 `
    --backend=software --dump-png=tablet.ppm --frames=1
```

On macOS, interactive windows use the Retina framebuffer and map Canvas
coordinates through the display scale. Dump and benchmark dimensions are
physical pixels, so `--w=1280 --h=720` always produces and measures a
1280 x 720 framebuffer rather than a 2x backing store.

The feature showcase uses a device-neutral 2:1 logical standard (800 x 400
landscape and 400 x 800 portrait). Each reference canvas is aspect-fitted,
horizontally centered and scaled as one unit. Text, strokes, radii, spacing and
card geometry therefore retain the same proportions instead of independently
reflowing at desktop window sizes.
The `--viewport-standard` option also accepts `phone_16_9`, `tablet_4_3` and
`desktop_16_10` for layout-conformance coverage. `legacy_android` is available
only to reproduce captures made before the neutral standard was adopted.
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

1. Put reusable drawing code under `platforms/shared/scenes/`.
2. Add a small `IScene` adapter and register it in
   [`src/SceneCatalog.cpp`](src/SceneCatalog.cpp).
3. Register the same id and sample in all hosts and the visual contract.

The Scene interface (`onCanvasReady`, `onLayout`, `onFrame`, `onCanvasReleasing`)
is deliberately platform-agnostic. The stress scenes are already shared
directly by Android, iOS, Desktop and Web.

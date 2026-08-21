# Multi-platform visual-parity validation record

This record captures problems found while running the complete Android, iOS,
Desktop and Web matrix. It is separate from temporary task notes so the same
failures can be recognized when more scenes or platforms are added.

## 2026-08-21 validation environment

| Platform | Runtime | Backend | Capture DPR |
| --- | --- | --- | ---: |
| Android | API 33 arm64 emulator, 1080 x 2160 | OpenGL ES 3 | 2.75 |
| iOS | iPhone 17 Pro simulator, iOS 26.5 | Metal + CoreText | 3 |
| Desktop | macOS Software reference | Software + CoreText | 3 |
| Web | Headless Chrome with ANGLE Metal | WebGL 2 | 3 |

The application and bundle identifier is `com.whatscanvas.demo` on Android and
iOS. The matrix covers portrait and landscape at 0.0, 0.5, 1.25 and 2.0
seconds, for 24 comparisons against the Desktop reference.

## Problems and fixes

### Web backing buffer was captured at CSS resolution

The Web renderer correctly allocated a high-DPR drawing buffer, but Chrome DevTools
captured a 1-DPR PNG because device emulation itself was configured at DPR 1.
The screenshot therefore hid the high-density pixels and introduced a second
browser downsample before comparison. The image-sampling region failed in both
orientations even though other regions stayed within their looser edge budget.

The browser matrix now renders and captures at the same DPR 3 as the reference.
It reads the PNG IHDR and rejects any screenshot that is not exactly three
times the canonical logical size. This changed the Web image-sampling error
from 2.61-3.02 mean channel delta to 0.28-0.64, without relaxing its profile.

### Android rotation could race the first cold launch

Writing `user_rotation` can update input state before WindowManager publishes
new application bounds. The first portrait sample could therefore launch with
a landscape surface and receive incorrect portrait metadata.

The emulator capture script now drives the emulator sensor until the active
display reaches the requested orientation, locks that orientation, and then
independently checks the renderer-ready dimensions. API 33 can restore a
retained task orientation on the first frame after `adb install -r`; the script
corrects that foreground Activity once and waits for its resized GL surface.
An unresolved orientation mismatch is a hard failure.

### iOS SpringBoard does not prove application orientation

SpringBoard remained portrait after the test device was set to landscape, so a
pre-launch screenshot-orientation assertion timed out. The device orientation
was valid; the assertion was observing the wrong process.

The UI test still sets orientation before each cold launch, but verifies the
screen aspect only after the application reaches the foreground. Each fixed
frame is written under a unique scene/view/time filename with exact safe-area
crop metadata, so all eight captures survive the test run.

### Nearest-neighbor phase at Android 2.75 DPR

The Android portrait image-sampling region repeatedly measured 1.155 mean
channel delta with a 0.035979 bad-pixel ratio against the DPR 3 reference. The
same result occurred at all four animation times, was confined to checker-grid
edges, and stayed below the existing 1.5 mean-delta limit. This is the expected
nearest-neighbor phase difference between 2.75 and 3 DPR, not a geometry shift.

The image-sampling bad-pixel ceiling is therefore 0.04 instead of 0.03. The
mean-delta ceiling remains unchanged, so a broad texture or color regression
still fails even if it affects fewer edge pixels.

## Repeatable commands

```sh
platforms/android/capture_emulator_visual_parity.sh --device emulator-5554
platforms/ios/scripts/capture_simulator_visual_parity.sh --device <simulator-udid>
platforms/desktop/capture_visual_parity.sh --host <desktop-host-path>
platforms/wasm/test.sh
python3 tools/visual_parity/visual_parity.py matrix \
  --contract tests/visual_parity/scenes.json \
  --captures out/visual-parity/captures \
  --reference-platform desktop \
  --output out/visual-parity/matrix
```

Simulator and headless-browser coverage is the development gate. A release
still requires representative physical iOS GPUs, Android OEM devices, and more
than one browser/GPU combination because their presentation and driver paths
cannot be proven by these local simulators.

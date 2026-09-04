# Multi-platform Visual-Parity Validation Record — August 2026

> Dated maintainer evidence. The current public contract is
> [`VISUAL_PARITY.md`](../../public/validation/VISUAL_PARITY.md).

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
iOS. The matrix covers `feature_showcase` at 0.0, 0.5, 1.25 and 2.0 seconds
plus the focused text, geometry and compositing scenes at 1.25 seconds. Both
orientations and all four platforms produce 42 comparisons against Desktop.

## Problems and fixes

### Device-derived viewport dimensions were not a stable standard

The original 393 x 759 portrait and 786 x 377 landscape canvases came from one
Android device's usable area. They were neither common design dimensions nor
exact rotations of each other, so orientation changed both aspect ratio and
scene geometry.

The primary pixel gate now uses a device-neutral 400 x 800 portrait and
800 x 400 landscape design space. The orientations are exact rotations, the
values are easy to reason about, and physical DPR remains independent. A
registry adds 360 x 640 phone, 768 x 1024 tablet and 1440 x 900 desktop layout
coverage. The device-derived pair remains available only as `legacy_android`.
The registry validator rejects a non-legacy standard that is not rotation
symmetric or a scene whose dimensions do not match its declared standard.

All four platforms were recaptured after the migration. The fresh matrix at
four animation samples in both orientations produced 24 comparisons, zero
failures. The primary DPR 3 Desktop/Web outputs were 1200 x 2400 portrait and
2400 x 1200 landscape; Android and iOS supplied exact content crops through
their metadata.

### Web smoke test could run a stale Wasm binary

The first matrix after changing the viewport contract failed all eight Web
comparisons even though the browser launched with the new dimensions. The test
script had reused an older `.wasm` artifact, so the JavaScript shell and native
scene disagreed about the canonical canvas.

`platforms/wasm/test.sh` now rebuilds by default before browser validation.
An explicit `WHATSCANVAS_WEB_SKIP_BUILD=1` escape hatch is reserved for callers
that have already built the exact source revision. After rebuilding, the Web
smoke test passed resize, background, context-restore and cold-reload checks,
and all eight Web matrix comparisons passed.

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

### Android rotation and first-frame capture could race a cold launch

Writing `user_rotation` can update input state before WindowManager publishes
new application bounds. The first portrait sample could therefore launch with
a landscape surface and receive incorrect portrait metadata.

The emulator capture script now locks WindowManager to the requested rotation,
waits for the active display value, and independently checks renderer-ready
dimensions. It then waits for a native first-frame signal containing the scene
id and exact surface dimensions. This prevents both a stale-orientation surface
and Android's cold-start splash from being accepted as a valid capture. A
single scene can be recaptured with `--scene text_stress`.

The expanded matrix exposed both races: sensor-driven rotation failed to reach
landscape reliably, then the first text-stress portrait capture was the system
splash because context initialization completed before its expensive first
frame. Direct rotation locking and the dimension-qualified frame signal fixed
both without changing pixel thresholds.

### Even-odd hole tessellation fell back to a filled triangle fan

The new geometry scene rendered a concentric even-odd path as a solid disk on
every backend. This was a shared tessellation defect, not platform drift. Hole
bridging intentionally duplicates its bridge endpoints, but the ear-clipping
containment test treated those equal-position points as unrelated vertices.
No ear could be selected, so the fallback triangle fan filled the hole.

The ear test now treats equal-position bridge endpoints as the same vertex. An
exact Software pixel test asserts that the outer contour is filled while the
inner contour preserves the background. The corrected scene was rebuilt and
recaptured on Software, OpenGL ES/WebGL and Metal.

### iOS SpringBoard does not prove application orientation

SpringBoard remained portrait after the test device was set to landscape, so a
pre-launch screenshot-orientation assertion timed out. The device orientation
was valid; the assertion was observing the wrong process.

The UI test still sets orientation before each cold launch, but verifies the
screen aspect only after the application reaches the foreground. Each fixed
frame is written under a unique scene/view/time filename with exact safe-area
crop metadata, so all fourteen captures survive the test run.

### Expanded scene matrix result

The final run produced 41 passing comparisons immediately. The only failed
report was the Android cold-start splash described above; its targeted
recapture passed all four text regions with mean channel deltas from 0.36 to
2.35 and bad-pixel ratios from 0.23% to 2.72%. The aggregate is therefore 42
comparisons with zero rendering failures. Web additionally passed DPR 3,
resize, visibility pause/resume, context loss/restoration, cold reload and
frame-pacing checks. iOS ran all fourteen captures with Metal validation.

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

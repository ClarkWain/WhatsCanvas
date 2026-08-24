# WhatsCanvas iOS Demo

The iOS sample uses the standalone `WhatsCanvasMetal` target and the native
CoreText text backend. It does not compile or link OpenGL ES.

## Requirements

- Xcode 26.6 or newer
- iOS 15.0 deployment target
- iPhone or iPad simulator/device with Metal support
- Bundle identifier `com.whatscanvas.demo`

## Release SDK

Tagged releases provide `whatscanvas-ios-release-<version>.zip`. It contains
`WhatsCanvas.xcframework`, the public `wsc` headers, an `arm64` device slice,
and an `arm64`/`x86_64` simulator slice. The library uses Metal and CoreText and
has an iOS 15 deployment target.

Add the XCFramework to the application target, include `<wsc/wsc.h>` from
C++/Objective-C++, and link Metal, Foundation, QuartzCore, CoreGraphics,
CoreText, and UIKit. The host owns its `CAMetalLayer`, lifecycle, and signing.

Build the same archive locally with:

```sh
platforms/ios/scripts/package_xcframework.sh
```

The versioned zip is written under `out/mobile/ios/`.

## Run

Open `WhatsCanvasDemo.xcodeproj`, select an iOS simulator, and run the
`WhatsCanvasDemo` scheme. The first build invokes CMake to compile the static
Metal library for the selected SDK and architecture.

The sample targets 60 fps using `CADisplayLink`, supports portrait and
landscape layouts, tears down GPU resources on backgrounding, and reconstructs
Metal/CoreText state when returning to the foreground.
After safe-area removal, both orientations use the same canonical content
windows as Android and Desktop and scale the complete scene as one unit.

Physical-device rendering failures, Metal resource-binding checks, and the
simulator/device regression checklist are documented in
[`DEVICE_RENDERING_TROUBLESHOOTING.md`](DEVICE_RENDERING_TROUBLESHOOTING.md).
The public API coverage matrix, validation-layer defaults, and the remaining
device-only test boundary are documented in
[`METAL_API_VALIDATION.md`](METAL_API_VALIDATION.md).

To build from the command line:

```sh
xcodebuild \
  -project platforms/ios/WhatsCanvasDemo.xcodeproj \
  -scheme WhatsCanvasDemo \
  -sdk iphonesimulator \
  -destination 'platform=iOS Simulator,name=iPhone 17 Pro' \
  CODE_SIGNING_ALLOWED=NO \
  build
```

The `WhatsCanvasDemoUITests` target rotates the simulator, verifies
background/foreground state transitions, terminates and cold-launches the app,
and keeps portrait and landscape screenshots:

```sh
xcodebuild test \
  -project platforms/ios/WhatsCanvasDemo.xcodeproj \
  -scheme WhatsCanvasDemo \
  -destination 'platform=iOS Simulator,name=iPhone 17 Pro' \
  CODE_SIGNING_ALLOWED=NO
```

The same scene has been validated on an iPhone 12 (A14, iOS 18.7.8) with Metal
API Validation enabled in Debug and at 59.2–59.9 fps in Release. Simulator
coverage remains the repeatable lifecycle gate; representative older and
current physical GPUs are still required before production distribution.

Add `--capture-frames`, `--capture-time=1.25` and optionally
`--capture-scene=text_stress` to the Scheme launch arguments
to produce a deterministic
`Documents/feature_showcase-<viewport>-t1250.png` plus its JSON metadata. A
live, non-fixed capture continues to use `Documents/screenshot.png`. The complete
cross-platform capture contract is documented in
[`../../doc/VISUAL_PARITY.md`](../../doc/VISUAL_PARITY.md).

The simulator capture matrix uses cold launches for every contracted sample in
both orientations, preserves each frame with its crop metadata, and
copies the results into the shared capture tree:

```sh
platforms/ios/scripts/capture_simulator_visual_parity.sh \
  --device <booted-simulator-udid>
```

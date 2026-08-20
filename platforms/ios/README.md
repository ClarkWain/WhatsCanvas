# WhatsCanvas iOS Demo

The iOS sample uses the standalone `WhatsCanvasMetal` target and the native
CoreText text backend. It does not compile or link OpenGL ES.

## Requirements

- Xcode 26.6 or newer
- iOS 15.0 deployment target
- iPhone or iPad simulator/device with Metal support
- Bundle identifier `con.whatscanvas.demo`

## Run

Open `WhatsCanvasDemo.xcodeproj`, select an iOS simulator, and run the
`WhatsCanvasDemo` scheme. The first build invokes CMake to compile the static
Metal library for the selected SDK and architecture.

The sample targets 60 fps using `CADisplayLink`, supports portrait and
landscape layouts, tears down GPU resources on backgrounding, and reconstructs
Metal/CoreText state when returning to the foreground.

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
